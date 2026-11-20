// net::Session against the mock server's SOE endpoint (tests/mock_server/soe_endpoint.h): the session layer's
// behaviour on a lossy, duplicating, reordering link, which the scripted fates make deterministic. The sessions
// here use no multi-buffer hold (hold_time 0, as the login server's) so each packet is its own datagram and a
// scripted fate applies to exactly one packet.

#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <thread>

#include "mock_server/harness.h"
#include "net/connection/game_connection.h"
#include "net/session/soe/session.h"
#include "net/session/soe/framing.h"
#include "platform/udp_socket.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

AppPacket app(std::uint16_t opcode, std::uint8_t tag) { return AppPacket(opcode, {static_cast<std::byte>(tag)}); }

std::uint8_t tag_of(const AppPacket& p) { return p.data.empty() ? 0 : std::to_integer<std::uint8_t>(p.data[0]); }

struct SessionRig {
    SoeEndpoint server;
    std::unique_ptr<ISession> session;
    std::vector<AppPacket> got; // what the session delivered, in order

    explicit SessionRig(soe::Options options = quick(), soe::SessionParams params = SoeEndpoint::default_params())
        : server(params), session(soe::create_session(std::move(options))) {}

    static soe::Options quick() {
        soe::Options o;
        o.hold_time = 0ms;
        return o;
    }

    void step() {
        session->update();
        server.poll();
        while (auto p = session->receive()) got.push_back(std::move(*p));
    }
    // A few passes a millisecond apart: only for assertions that something does NOT happen.
    void run_for_steps(int n) {
        for (int i = 0; i < n; ++i) {
            step();
            std::this_thread::sleep_for(1ms);
        }
    }
    bool until(const std::function<bool()>& done, std::chrono::milliseconds limit = 5s) {
        return pump([&] { step(); }, done, limit);
    }
    bool connect() {
        session->connect("127.0.0.1", server.port());
        return until([&] { return session->state() == SessionState::Connected; });
    }
    std::vector<std::uint8_t> tags() const {
        std::vector<std::uint8_t> out;
        for (const auto& p : got) out.push_back(tag_of(p));
        return out;
    }
    std::vector<std::uint8_t> server_tags() const {
        std::vector<std::uint8_t> out;
        for (const auto& p : server.inbox) out.push_back(tag_of(p));
        return out;
    }
};

using Tags = std::vector<std::uint8_t>;

} // namespace

TEST_CASE("mock session: handshake adopts the server's parameters") {
    SessionRig rig;
    REQUIRE(rig.connect());
    CHECK(rig.server.sessions() == 1);
    CHECK(rig.server.requested_max_packet() == 512);
    CHECK(rig.session->error().empty());
    // Reliable packets in both directions use the negotiated CRC.
    rig.session->send(app(0x4242, 1));
    rig.server.send_reliable(app(0x4343, 2));
    REQUIRE(rig.until([&] { return !rig.server.inbox.empty() && !rig.got.empty(); }));
    CHECK(rig.server.inbox.front().opcode == 0x4242);
    CHECK(rig.got.front().opcode == 0x4343);
    CHECK(rig.server.stats.bad_datagrams == 0);
}

TEST_CASE("mock session: duplicated datagrams are delivered once and acknowledged again") {
    SessionRig rig;
    REQUIRE(rig.connect());
    rig.server.send_reliable(app(0x100, 1));
    rig.server.script_outgoing({Fate::Duplicate});
    rig.server.send_reliable(app(0x100, 2));
    rig.server.send_reliable(app(0x100, 3));
    rig.server.send_reliable(app(0x100, 4)); // a marker: reliable delivery is in order, so once 4 is here the rest ran
    REQUIRE(rig.until([&] { return rig.got.size() >= 4; }));
    CHECK(rig.tags() == Tags{1, 2, 3, 4});
    REQUIRE(rig.until([&] { return rig.server.last_cumulative_ack() == std::optional<std::uint16_t>(3); }));
    // The duplicate (an old sequence) is dropped and answered with the cumulative Ack again.
    CHECK(rig.got.size() == 4);
}

TEST_CASE("mock session: reordered datagrams are held until the gap closes, with out-of-order acks") {
    SessionRig rig;
    REQUIRE(rig.connect());
    rig.server.script_outgoing({Fate::Hold, Fate::Hold});
    rig.server.send_reliable(app(0x100, 1)); // seq 0, held
    rig.server.send_reliable(app(0x100, 2)); // seq 1, held
    rig.server.send_reliable(app(0x100, 3)); // seq 2 arrives first
    REQUIRE(rig.until([&] { return rig.server.is_acked(2); }));
    CHECK(rig.got.empty()); // nothing is delivered ahead of sequence 0
    CHECK_FALSE(rig.server.last_cumulative_ack().has_value());
    REQUIRE_FALSE(rig.server.ack_log.empty());
    CHECK(rig.server.ack_log.back().kind == AckEvent::Kind::OutOfOrder);
    CHECK(rig.server.ack_log.back().sequence == 2);

    rig.server.release_held(true); // 2 and 1 arrive in the wrong order
    REQUIRE(rig.until([&] { return rig.got.size() == 3; }));
    CHECK(rig.tags() == Tags{1, 2, 3});
    REQUIRE(rig.until([&] { return rig.server.last_cumulative_ack() == std::optional<std::uint16_t>(2); }));
    bool early1 = false;
    for (const auto& a : rig.server.ack_log) early1 |= a.kind == AckEvent::Kind::OutOfOrder && a.sequence == 1;
    CHECK(early1);
}

TEST_CASE("mock session: a lost datagram stalls delivery until the server resends it") {
    SessionRig rig;
    REQUIRE(rig.connect());
    rig.server.script_outgoing({Fate::Drop});
    rig.server.send_reliable(app(0x100, 1)); // lost
    rig.server.send_reliable(app(0x100, 2));
    rig.server.send_reliable(app(0x100, 3));
    REQUIRE(rig.until([&] { return rig.server.is_acked(1) && rig.server.is_acked(2); }));
    CHECK_FALSE(rig.server.is_acked(0));
    CHECK(rig.got.empty());
    CHECK(rig.server.unacked() == 1);

    // EQEmu resends whatever is unacknowledged; only the lost packet is.
    CHECK(rig.server.resend_unacked() == 1);
    REQUIRE(rig.until([&] { return rig.got.size() == 3; }));
    CHECK(rig.tags() == Tags{1, 2, 3});
    REQUIRE(rig.until([&] { return rig.server.unacked() == 0; }));
    CHECK(rig.server.last_cumulative_ack() == std::optional<std::uint16_t>(2));
}

TEST_CASE("mock session: the client resends a lost reliable packet as soon as a later one is acknowledged") {
    SessionRig rig;
    REQUIRE(rig.connect());
    // UdpReliableChannel_GiveTime's fast resend (docs/protocol/session.md, Resend): a packet last sent before the
    // first send of the newest acknowledged packet goes out again at the next pass, without waiting for the timer.
    rig.server.script_incoming({Fate::Drop, Fate::Deliver});
    rig.session->send(app(0x200, 1)); // sent alone, lost
    REQUIRE(rig.until([&] { return rig.server.stats.dropped_in == 1; }));
    rig.session->send(app(0x200, 2)); // a later datagram that gets through
    REQUIRE(rig.until([&] { return rig.server.inbox.size() == 2; }, 3s));
    CHECK(rig.server_tags() == Tags{1, 2}); // in order, once each
    // Sequence 0 arrived at least twice (the lost copy and the resend), sequence 1 once.
    CHECK(rig.server.reliable_arrivals.at(0) >= 2);
    CHECK(rig.server.reliable_arrivals.at(1) == 1);
    CHECK(rig.server.stats.early_in == 1);
}

TEST_CASE("mock session: a duplicated client datagram is delivered once") {
    SessionRig rig;
    REQUIRE(rig.connect());
    rig.server.script_incoming({Fate::Duplicate});
    rig.session->send(app(0x200, 1));
    REQUIRE(rig.until([&] { return rig.server.stats.duplicates_in == 1; }));
    CHECK(rig.server_tags() == Tags{1});
}

TEST_CASE("mock session: combined datagrams, unreliable packets and keepalives") {
    SessionRig rig;
    REQUIRE(rig.connect());
    rig.server.send_reliable_combined({app(0x100, 1), app(0x100, 2), app(0x100, 3)});
    rig.server.send_unreliable(app(0x101, 9));
    rig.server.send_keepalive();
    rig.server.send_reliable(app(0x100, 4));
    REQUIRE(rig.until([&] { return rig.got.size() == 5; }));
    CHECK(rig.tags() == Tags{1, 2, 3, 9, 4}); // an unreliable packet is delivered as it arrives
    // One cumulative Ack covers the whole combined datagram.
    REQUIRE(rig.until([&] { return rig.server.last_cumulative_ack() == std::optional<std::uint16_t>(3); }));

    // The client combines too: small packets sent together can share a datagram, and arrive as separate packets.
    rig.session->send(app(0x300, 1));
    rig.session->send(app(0x300, 2));
    rig.session->send(app(0x300, 3), false);
    REQUIRE(rig.until([&] { return rig.server.inbox.size() == 3; }));
    CHECK(rig.server.inbox.size() == 3);
}

TEST_CASE("mock session: a large payload is fragmented in both directions") {
    SessionRig rig;
    REQUIRE(rig.connect());
    std::vector<std::byte> big(5000);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::byte>((i * 2654435761u) >> 24);
    rig.server.send_reliable(AppPacket(0x1234, big));
    rig.session->send(AppPacket(0x4321, big));
    REQUIRE(rig.until([&] { return !rig.got.empty() && !rig.server.inbox.empty(); }));
    CHECK(rig.got.front().data == big);
    CHECK(rig.server.inbox.front().data == big);
    CHECK(rig.server.next_sequence() > 5); // 5000 bytes do not fit in a 512-byte datagram
}

TEST_CASE("mock session: compression and XOR passes, 2 CRC bytes, a smaller packet size") {
    soe::SessionParams params = SoeEndpoint::default_params();
    params.passes = {soe::EncodePass::Compression, soe::EncodePass::Xor};
    params.max_packet_size = 256;
    SessionRig rig(SessionRig::quick(), params);
    REQUIRE(rig.connect());
    std::vector<std::byte> zeros(3000); // compresses well
    rig.server.send_reliable(AppPacket(0x1234, zeros));
    rig.session->send(AppPacket(0x4321, zeros));
    REQUIRE(rig.until([&] { return !rig.got.empty() && !rig.server.inbox.empty(); }));
    CHECK(rig.got.front().data == zeros);
    CHECK(rig.server.inbox.front().data == zeros);
    CHECK(rig.server.stats.bad_datagrams == 0);
}

TEST_CASE("mock session: the server closing the session, OP_OutOfSession and a clean client close") {
    {
        SessionRig rig;
        REQUIRE(rig.connect());
        rig.server.send_disconnect();
        REQUIRE(rig.until([&] { return rig.session->state() == SessionState::Disconnected; }));
        CHECK(rig.session->error() == "the server closed the session");
    }
    {
        SessionRig rig;
        REQUIRE(rig.connect());
        rig.server.send_out_of_session();
        REQUIRE(rig.until([&] { return rig.session->state() == SessionState::Disconnected; }));
        CHECK(rig.session->error().find("OP_OutOfSession") != std::string::npos);
    }
    {
        // disconnect() drains the reliable queue first, then sends SessionDisconnect.
        SessionRig rig;
        REQUIRE(rig.connect());
        rig.session->send(app(0x500, 1));
        rig.session->disconnect();
        CHECK(rig.session->state() == SessionState::Disconnecting);
        REQUIRE(rig.until([&] { return rig.session->state() == SessionState::Disconnected; }));
        REQUIRE(rig.until([&] { return rig.server.client_disconnected(); }));
        CHECK(rig.server_tags() == Tags{1});
        CHECK(rig.session->error().empty());
    }
}

TEST_CASE("mock session: link death needs silence; a keepalive is traffic") {
    soe::Options opt = SessionRig::quick();
    opt.idle_timeout = 300ms; // the real one is 180 s (world and zone)
    ManualClock clock;
    opt.clock = clock.source();
    SessionRig rig(opt);
    REQUIRE(rig.connect());
    clock.hold();
    // A keepalive every 80 ms of the session's time keeps a 300 ms limit from expiring for 640 ms.
    for (int i = 0; i < 8; ++i) {
        clock.advance(80ms);
        auto before = rig.session->stats().datagrams_received;
        rig.server.send_keepalive();
        REQUIRE(rig.until([&] { return rig.session->stats().datagrams_received > before; }));
    }
    CHECK(rig.session->state() == SessionState::Connected);
    // Then nothing arrives: a millisecond short of the limit is still alive, the limit itself is link death
    // (InternalGiveTime @0x8c638e).
    clock.advance(299ms);
    rig.step();
    CHECK(rig.session->state() == SessionState::Connected);
    clock.advance(1ms);
    REQUIRE(rig.until([&] { return rig.session->state() == SessionState::Disconnected; }));
    CHECK(rig.session->error().find("timed out") != std::string::npos);
}

TEST_CASE("mock session: the default link-death limit is the original's 180 s of silence") {
    // UdpManager Params 0x12 for world and zone (NetMgr +0x50 = 180000; UdpConnection +0x15c). Login and chat keep
    // their own limits (their Options).
    CHECK(soe::Options{}.idle_timeout == 180s);
    CHECK(ConnectionOptions{}.zone_protocol.session_config.protocol == "soe");
    soe::Options opt = SessionRig::quick();
    opt.stat_interval = 0ms; // the mock answers a stat request, which would be traffic
    ManualClock clock;
    opt.clock = clock.source();
    SessionRig rig(opt);
    REQUIRE(rig.connect());
    clock.hold();
    // The silence is counted from a packet received on the held clock: the connect's last receive came while the
    // clock still ran, and the time until the hold would shorten the limit.
    auto before = rig.session->stats().datagrams_received;
    rig.server.send_keepalive();
    REQUIRE(rig.until([&] { return rig.session->stats().datagrams_received > before; }));
    // 15 s, the keepalive delay, is not link death: the client sends a keepalive and the link stays up.
    clock.advance(15s);
    REQUIRE(rig.until([&] { return rig.server.stats.keepalives_in == 1; }));
    CHECK(rig.session->state() == SessionState::Connected);
    clock.advance(180s - 15s - 1ms);
    rig.run_for_steps(20);
    CHECK(rig.session->state() == SessionState::Connected);
    clock.advance(1ms);
    REQUIRE(rig.until([&] { return rig.session->state() == SessionState::Disconnected; }));
    CHECK(rig.session->error().find("nothing received for 180 s") != std::string::npos);
}

TEST_CASE("mock session: a keepalive goes out after the keepalive delay without sending, not while sending") {
    soe::Options opt = SessionRig::quick();
    opt.keepalive_interval = 200ms; // the original's 15 s (UdpManager Params 0xe)
    ManualClock clock;
    opt.clock = clock.source();
    SessionRig rig(opt);
    REQUIRE(rig.connect());
    clock.hold();
    // Something sent at the held time: the delay counts from there.
    rig.session->send(app(0x500, 1));
    REQUIRE(rig.until([&] { return rig.server.inbox.size() == 1; }));
    // Traffic every 150 ms of the session's time: never 200 ms without a datagram, so no keepalive.
    for (std::uint8_t i = 2; i < 6; ++i) {
        clock.advance(150ms);
        rig.session->send(app(0x500, i));
        REQUIRE(rig.until([&] { return rig.server.inbox.size() == i; }));
    }
    rig.run_for_steps(20);
    CHECK(rig.server.stats.keepalives_in == 0);
    CHECK(rig.session->stats().keepalives_sent == 0);
    // Then silence: one keepalive at 200 ms, the next 200 ms after that one.
    clock.advance(199ms);
    rig.run_for_steps(20);
    CHECK(rig.server.stats.keepalives_in == 0);
    clock.advance(1ms);
    REQUIRE(rig.until([&] { return rig.server.stats.keepalives_in == 1; }));
    rig.run_for_steps(20);
    CHECK(rig.server.stats.keepalives_in == 1);
    clock.advance(200ms);
    REQUIRE(rig.until([&] { return rig.server.stats.keepalives_in == 2; }));
    CHECK(rig.session->stats().keepalives_sent == 2);
    CHECK(rig.session->state() == SessionState::Connected);
}

TEST_CASE("mock session: keepalive_interval 0 sends none") {
    soe::Options opt = SessionRig::quick();
    opt.keepalive_interval = 0ms;
    ManualClock clock;
    opt.clock = clock.source();
    SessionRig rig(opt);
    REQUIRE(rig.connect());
    clock.hold();
    clock.advance(10000ms);
    rig.run_for_steps(20);
    CHECK(rig.server.stats.keepalives_in == 0);
}

TEST_CASE("mock session: connect() again starts a fresh session with its own sequence numbers") {
    SessionRig rig;
    REQUIRE(rig.connect());
    rig.session->send(app(0x500, 1));
    REQUIRE(rig.until([&] { return rig.server.inbox.size() == 1; }));
    rig.session->disconnect();
    REQUIRE(rig.until([&] { return rig.session->state() == SessionState::Disconnected; }));

    rig.got.clear();
    REQUIRE(rig.connect());
    CHECK(rig.server.sessions() == 2);
    rig.server.send_reliable(app(0x600, 7)); // the server's sequence numbers start over as well
    rig.session->send(app(0x500, 2));
    REQUIRE(rig.until([&] { return rig.got.size() == 1 && rig.server.inbox.size() == 2; }));
    CHECK(tag_of(rig.got[0]) == 7);
    CHECK(rig.server_tags() == Tags{1, 2});
    CHECK(rig.server.reliable_arrivals.count(0) == 1); // the new session's first reliable packet is sequence 0
}

TEST_CASE("mock session: datagrams that fail the CRC do not keep a dead link alive") {
    soe::Options opt = SessionRig::quick();
    opt.idle_timeout = 300ms;
    opt.stat_interval = 0ms;
    ManualClock clock;
    opt.clock = clock.source();
    SessionRig rig(opt);
    REQUIRE(rig.connect());
    clock.hold();
    auto before = rig.session->stats().datagrams_received;
    rig.server.send_keepalive();
    REQUIRE(rig.until([&] { return rig.session->stats().datagrams_received > before; }));
    // Garbage from the server's address and port: a protocol packet and an application one, neither with a good CRC.
    const soe::Bytes garbage[] = {
        {std::byte{0}, std::byte{soe::op::Packet}, std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78}},
        {std::byte{0x42}, std::byte{0x42}, std::byte{0x42}, std::byte{0x42}, std::byte{0x42}},
    };
    for (int i = 0; i < 3; ++i) {
        clock.advance(80ms);
        auto bad = rig.session->stats().bad;
        rig.server.send_datagram(garbage[i % 2]);
        REQUIRE(rig.until([&] { return rig.session->stats().bad > bad; }));
    }
    CHECK(rig.session->state() == SessionState::Connected);
    // 300 ms after the keepalive, the last datagram that was the server's, the link is dead.
    clock.advance(60ms);
    REQUIRE(rig.until([&] { return rig.session->state() == SessionState::Disconnected; }));
    CHECK(rig.session->error().find("timed out") != std::string::npos);
}

TEST_CASE("mock session: received packets the owner does not read are bounded; passing the bound ends the session") {
    soe::Options opt = SessionRig::quick();
    opt.max_received_bytes = 6000;
    SessionRig rig(opt);
    REQUIRE(rig.connect());
    // Read as they come, many times the bound goes through.
    for (std::uint8_t i = 1; i <= 20; ++i) {
        rig.server.send_reliable(AppPacket(0x300, soe::Bytes(1000, std::byte{i})));
        REQUIRE(rig.until([&] { return rig.got.size() == i; }));
    }
    CHECK(rig.session->state() == SessionState::Connected);
    // Not read (the owner stalled): the service thread queues them until the bound, then closes the session, since
    // dropping a reliable packet would leave the game out of step with the server.
    for (std::uint8_t i = 1; i <= 10; ++i) rig.server.send_reliable(AppPacket(0x300, soe::Bytes(1000, std::byte{i})));
    REQUIRE(pump([&] { rig.session->update(); rig.server.poll(); },
                 [&] { return rig.session->state() == SessionState::Disconnected; }, 5s));
    CHECK(rig.session->error().find("too much received data") != std::string::npos);
    // What was queued before the bound can still be read (step() reads it).
    REQUIRE(rig.until([&] { return rig.server.client_disconnected(); }));
    std::size_t queued = rig.got.size() - 20;
    CHECK(queued >= 1);
    CHECK(queued < 10);
}

TEST_CASE("mock session: a first fragment without its total length ends the session") {
    SessionRig rig;
    REQUIRE(rig.connect());
    rig.server.send_reliable(app(0x300, 1));
    REQUIRE(rig.until([&] { return rig.got.size() == 1; }));
    // A two-byte first fragment, built by hand (a stock server always writes the 4-byte total).
    std::uint16_t seq = rig.server.next_sequence();
    soe::Bytes packet{std::byte{0}, std::byte{soe::op::Fragment}, static_cast<std::byte>(seq >> 8),
                      static_cast<std::byte>(seq & 0xff), std::byte{0}, std::byte{0}};
    rig.server.send_datagram(soe::encode_datagram(rig.server.params(), packet));
    REQUIRE(rig.until([&] { return rig.session->state() == SessionState::Disconnected; }));
    CHECK(rig.session->error().find("without its total length") != std::string::npos);
    CHECK(rig.got.size() == 1);
    REQUIRE(rig.until([&] { return rig.server.client_disconnected(); }));
}

TEST_CASE("mock session: a SessionResponse with the connect code is taken from another address of the server") {
    // A raw stand-in for the server on 127.0.0.1, its secondary address 127.0.0.2 (EQEmu binds 0.0.0.0 and answers
    // from the address its route picks), and another port of 127.0.0.1. A reply without the connect code is ignored.
    std::string error;
    platform::UdpSocket server, other_port, other_host;
    const auto here = platform::Endpoint::v4(127, 0, 0, 1, 0);
    REQUIRE(server.open(platform::Endpoint::Family::V4, error, &here));
    REQUIRE(other_port.open(platform::Endpoint::Family::V4, error, &here));
    const auto there = platform::Endpoint::v4(127, 0, 0, 2, 0);
    if (!other_host.open(platform::Endpoint::Family::V4, error, &there)) {
        MESSAGE("127.0.0.2 cannot be bound here (" << error << "): the other-address half is not checked");
    }
    auto session = soe::create_session(SessionRig::quick());
    session->connect("127.0.0.1", server.local_endpoint()->port);

    std::array<std::byte, 2048> buf{};
    platform::Endpoint client;
    std::optional<std::size_t> got;
    REQUIRE(pump([&] { session->update(); got = server.recv_from(buf, client); }, [&] { return got.has_value(); }, 5s));
    REQUIRE(*got >= 14);
    ByteReader req(std::span<const std::byte>(buf.data(), *got));
    req.skip(6);
    const std::uint32_t code = req.be<std::uint32_t>();
    auto response = [&] {
        ByteWriter w;
        w.le<std::uint8_t>(0);
        w.le<std::uint8_t>(soe::op::SessionResponse);
        w.be<std::uint32_t>(code);
        w.be<std::uint32_t>(0x11223344u);
        w.le<std::uint8_t>(2);
        w.le<std::uint8_t>(0);
        w.le<std::uint8_t>(0);
        w.be<std::uint32_t>(512);
        return w.take();
    };
    // Another connect code: ignored, from any address.
    auto wrong = response();
    wrong[2] ^= std::byte{1};
    REQUIRE(other_port.send_to(client, wrong));
    for (int i = 0; i < 50; ++i) {
        session->update();
        std::this_thread::sleep_for(1ms);
    }
    CHECK(session->state() == SessionState::Connecting);
    // The server's other address (or, where 127.0.0.2 cannot be bound, another port of its own): adopted.
    REQUIRE((other_host.is_open() ? other_host : other_port).send_to(client, response()));
    CHECK(pump([&] { session->update(); }, [&] { return session->state() == SessionState::Connected; }, 5s));
}

TEST_CASE("mock session: a host name is looked up off the caller's thread, and the session connects after it") {
    // connect() is called from the frame (login, world, zone): it returns at once, Connecting, and the lookup's
    // answer starts the handshake. "localhost" comes from the hosts file, with no name server.
    SessionRig rig;
    rig.session->connect("localhost", rig.server.port());
    CHECK(rig.session->state() == SessionState::Connecting);
    REQUIRE(rig.until([&] { return rig.session->state() == SessionState::Connected; }, 10s));
    rig.session->send(app(0x1234, 7));
    REQUIRE(rig.until([&] { return !rig.server.inbox.empty(); }));
    CHECK(rig.server_tags() == Tags{7});
}

TEST_CASE("mock session: a lookup still running when the connect times out ends the connect") {
    // The connect timeout counts the lookup too. A name that never answers is stood in for by a clock that jumps
    // past the timeout before the lookup can have come back.
    soe::Options opt = SessionRig::quick();
    opt.service_interval = 0ms; // update() alone services it
    opt.connect_timeout = 100ms;
    ManualClock clock;
    opt.clock = clock.source();
    auto session = soe::create_session(opt);
    clock.hold();
    session->connect("localhost", 9);
    clock.advance(200ms);
    session->update();
    // Either the lookup came back first (then the SessionRequest went out and the timeout ended it) or not yet.
    CHECK(session->state() == SessionState::Disconnected);
    CHECK_FALSE(session->error().empty());
}

TEST_CASE("mock session: a lookup that came back after the timeout is not reported as a name that did not resolve") {
    // The clock is held, so nothing times out while the lookup of "localhost" runs (on a thread of its own, so it is
    // waited for in real time, without servicing the session). Then the clock jumps past the timeout: the answer is
    // taken first, so the connect fails as no session response (nothing listens), not as "cannot resolve". A slow
    // resolver leaves the lookup unfinished; each try waits longer.
    std::string last_error;
    for (auto wait : {50ms, 250ms, 1000ms, 3000ms}) {
        soe::Options opt = SessionRig::quick();
        opt.service_interval = 0ms; // update() alone services it
        opt.connect_timeout = 100ms;
        ManualClock clock;
        opt.clock = clock.source();
        auto session = soe::create_session(opt);
        clock.hold();
        session->connect("localhost", 9);
        std::this_thread::sleep_for(wait);
        clock.advance(200ms);
        session->update();
        REQUIRE(session->state() == SessionState::Disconnected);
        last_error = session->error();
        if (!last_error.starts_with("cannot resolve")) break;
    }
    CHECK(last_error.starts_with("no session response"));
}
