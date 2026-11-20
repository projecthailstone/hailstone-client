// Protocol-neutral login service and SOE session fuzz tests.
// RoF2 packet decoder sweeps live in tests/rof2/fuzz_decoders_test.cpp.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <zlib.h>

#include "core/byte_io.h"
#include "fuzz_patterns.h"
#include "fuzz_support.h"
#include "net/connection/login_client.h"
#include "net/protocol/login/codec.h"
#include "net/protocol/login/opcodes.h"
#include "net/session/soe/framing.h"
#include "net/session/soe/session.h"
#include "platform/udp_socket.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::net::login_codec;
using fuzz::Bytes;
using fuzz::Rng;
using fuzz::Throws;

// ---- login-server replies --------------------------------------------------------------------------------------

TEST_CASE("fuzz: login reply parsers report errors by exception and never crash") {
    fuzz::QuietLog quiet;
    // Login replies: u32 sequence, flags (byte 4 = 1: the body is zlib-compressed), then the body; the callers catch
    // std::exception and fail the login (LoginClient::handle). Compressed seeds make the inflate path reachable.
    auto compressed = [](Rng& rng, std::size_t body_size, std::size_t fill) {
        Bytes body = fuzz::random_bytes(rng, body_size);
        for (std::size_t i = fill; i < body.size(); ++i) body[i] = std::byte{0};
        uLongf cap = compressBound(static_cast<uLong>(body.size()));
        std::vector<Bytef> z(cap);
        compress(z.data(), &cap, reinterpret_cast<const Bytef*>(body.data()), static_cast<uLong>(body.size()));
        Bytes pkt(10, std::byte{0});
        pkt[4] = std::byte{1};
        pkt.insert(pkt.end(), reinterpret_cast<std::byte*>(z.data()), reinterpret_cast<std::byte*>(z.data()) + cap);
        return pkt;
    };
    Rng seed_rng(3);
    std::vector<Bytes> seeds = fuzz::patterns();
    for (std::size_t size : {40, 200, 3000}) seeds.push_back(compressed(seed_rng, size, size / 2));
    seeds.push_back(compressed(seed_rng, 100000, 0)); // a ratio-1000 stream: inflation must stay bounded
    auto sweep = [&](const char* name, auto&& parse) {
        for (const auto& pat : seeds)
            for (auto n : fuzz::truncation_lengths(std::min<std::size_t>(pat.size(), 1024)))
                fuzz::run_case(name, Bytes(pat.begin(), pat.begin() + static_cast<std::ptrdiff_t>(std::min(n, pat.size()))), Throws::Errors,
                               [&](std::span<const std::byte> d) { (void)parse(d); });
        Rng rng(0x10610);
        for (int i = 0; i < 600; ++i) {
            Bytes in = fuzz::mutate(seeds[rng.below(seeds.size())], rng);
            in.resize(std::min<std::size_t>(in.size(), 4096));
            fuzz::run_case(name, in, Throws::Errors, [&](std::span<const std::byte> d) { (void)parse(d); });
        }
    };
    sweep("parse_handshake_reply", [](std::span<const std::byte> d) { return parse_handshake_reply(d); });
    sweep("parse_login_reply", [](std::span<const std::byte> d) { return parse_login_reply(d); });
    sweep("parse_server_list", [](std::span<const std::byte> d) { return parse_server_list(d); });
    sweep("parse_play_reply", [](std::span<const std::byte> d) { return parse_play_reply(d); });
    sweep("reply_body", [](std::span<const std::byte> d) { return reply_body(login_op::LoginAccepted, d); });
}

// ---- SOE session layer -----------------------------------------------------------------------------------------

namespace {

using soe::ByteSpan;

soe::SessionParams params_with(bool compressed, bool xor_pass, std::uint8_t crc_bytes) {
    soe::SessionParams p;
    p.encode_key = 0x12345678u;
    p.crc_bytes = crc_bytes;
    p.passes = {compressed ? soe::EncodePass::Compression : soe::EncodePass::None,
                xor_pass ? soe::EncodePass::Xor : soe::EncodePass::None};
    p.max_packet_size = 512;
    return p;
}

// Known-good protocol packets (before the encode passes): the seeds the mutations start from.
std::vector<Bytes> soe_seeds() {
    std::vector<Bytes> out;
    auto params = params_with(true, true, 2);
    Rng rng(77);
    Bytes payload = fuzz::random_bytes(rng, 1300);
    std::uint16_t seq = 0;
    for (auto& f : soe::make_reliable(payload, seq, params)) out.push_back(f);
    Bytes small = soe::encode_app(AppPacket(0x1234, fuzz::random_bytes(rng, 20)));
    out.push_back(soe::make_reliable(small, seq, params).front());
    out.push_back(soe::make_ack(soe::op::Ack, 5));
    out.push_back(soe::make_ack(soe::op::OutOfOrderAck, 9));
    out.push_back(soe::make_session_request(0xabcdef01u, 512));
    out.push_back(soe::make_disconnect(0x01020304u));
    std::vector<Bytes> parts = {out[0], soe::make_ack(soe::op::Ack, 1), soe::make_ack(soe::op::Ack, 2)};
    for (auto& p : parts) p.resize(std::min<std::size_t>(p.size(), 200));
    out.push_back(soe::make_combined(parts));
    // 00 19 app-combined: u8 length, then 0xff + u16 BE, then 0xff 0xff 0xff + u32 BE.
    Bytes ac = {std::byte{0}, std::byte{0x19}, std::byte{3}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{0xff},
                std::byte{0}, std::byte{2}, std::byte{4}, std::byte{5}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
                std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}, std::byte{6}};
    out.push_back(ac);
    return out;
}

void feed_all(const Bytes& packet, soe::InboundStream& stream, std::vector<Bytes>& delivered) {
    (void)stream.push(packet, delivered);
    (void)soe::split_combined(packet);
    (void)soe::split_app_combined(packet);
    (void)soe::decode_app(packet);
    (void)soe::decode_app(packet, 1);
    (void)soe::parse_session_response(packet);
    (void)soe::is_encodable(packet);
}

} // namespace

TEST_CASE("fuzz: SOE framing functions survive truncated and mutated packets") {
    fuzz::QuietLog quiet;
    const auto seeds = soe_seeds();
    Rng rng(0x50e);
    for (const auto& seed : seeds) {
        std::vector<Bytes> inputs;
        for (std::size_t n = 0; n <= seed.size(); ++n) inputs.emplace_back(seed.begin(), seed.begin() + static_cast<std::ptrdiff_t>(n));
        for (int i = 0; i < 400; ++i) inputs.push_back(fuzz::mutate(seed, rng));
        for (const auto& in : inputs)
            fuzz::run_case("soe framing", in, Throws::Never, [&](std::span<const std::byte> d) {
                Bytes copy(d.begin(), d.end());
                // Fresh stream each time, and one that is already mid-sequence, so both the in-order and the
                // buffered-out-of-order paths run.
                for (std::uint16_t first : {std::uint16_t{0}, std::uint16_t{0xfffe}, std::uint16_t{5}}) {
                    soe::InboundStream stream(first);
                    std::vector<Bytes> delivered;
                    feed_all(copy, stream, delivered);
                    feed_all(copy, stream, delivered);
                }
            });
    }
}

TEST_CASE("fuzz: a stream fed random fragments and out-of-order packets stays bounded") {
    fuzz::QuietLog quiet;
    Rng rng(0xf4a6);
    soe::InboundStream stream(0);
    std::vector<Bytes> delivered;
    for (int i = 0; i < 20000; ++i) {
        ByteWriter w;
        w.le<std::uint8_t>(0);
        w.le<std::uint8_t>(rng.chance(50) ? soe::op::Fragment : soe::op::Packet);
        w.be<std::uint16_t>(static_cast<std::uint16_t>(stream.next() + rng.below(6)));
        if (rng.chance(30)) w.be<std::uint32_t>(rng.chance(50) ? 0xffffffffu : static_cast<std::uint32_t>(rng.below(100000)));
        auto body = fuzz::random_bytes(rng, rng.below(300));
        w.bytes(body);
        Bytes pkt = w.take();
        fuzz::run_case("inbound stream", pkt, Throws::Never, [&](std::span<const std::byte> d) {
            (void)stream.push(d, delivered);
        });
        delivered.clear();
        CHECK(stream.buffered() < 20000);
    }
}

TEST_CASE("fuzz: datagram decode (CRC, XOR and compression passes) rejects garbage without throwing") {
    fuzz::QuietLog quiet;
    const auto seeds = soe_seeds();
    Rng rng(0xda7a);
    for (bool compressed : {false, true})
        for (bool x : {false, true})
            for (std::uint8_t crc : std::initializer_list<std::uint8_t>{0, 2, 4}) {
                auto params = params_with(compressed, x, crc);
                for (const auto& seed : seeds) {
                    Bytes wire = soe::encode_datagram(params, seed);
                    std::vector<Bytes> inputs;
                    for (std::size_t n = 0; n <= wire.size(); n += 1 + n / 40) inputs.emplace_back(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(n));
                    for (int i = 0; i < 150; ++i) inputs.push_back(fuzz::mutate(wire, rng));
                    // Mutations of the *plain* packet, re-encoded with a valid CRC, reach the passes behind the CRC check.
                    for (int i = 0; i < 150; ++i) {
                        Bytes plain = fuzz::mutate(seed, rng);
                        if (!plain.empty()) inputs.push_back(soe::encode_datagram(params, plain));
                    }
                    for (const auto& in : inputs)
                        fuzz::run_case("decode_datagram", in, Throws::Never, [&](std::span<const std::byte> d) {
                            (void)soe::decode_datagram(params, d);
                        });
                }
            }
}

TEST_CASE("fuzz: decompress_body honours its size limit for bogus zlib data") {
    fuzz::QuietLog quiet;
    Rng rng(0x21b);
    Bytes big(100000, std::byte{0}); // compresses to a few hundred bytes: a decompression bomb
    Bytes bomb = soe::compress_body(big);
    auto limited = soe::decompress_body(bomb, 8192);
    CHECK_FALSE(limited.has_value());
    for (int i = 0; i < 3000; ++i) {
        Bytes in = fuzz::random_bytes(rng, rng.below(120));
        if (!in.empty() && rng.chance(80)) in[0] = std::byte{0x5a};
        fuzz::run_case("decompress_body", in, Throws::Never, [&](std::span<const std::byte> d) { (void)soe::decompress_body(d); });
    }
    for (std::size_t n = 0; n <= bomb.size(); ++n)
        fuzz::run_case("decompress_body", Bytes(bomb.begin(), bomb.begin() + static_cast<std::ptrdiff_t>(n)), Throws::Never,
                       [&](std::span<const std::byte> d) { (void)soe::decompress_body(d); });
}

TEST_CASE("fuzz: a live Session receiving corrupt datagrams from its server keeps running") {
    fuzz::QuietLog quiet;
    platform::UdpSocket sock;
    std::string err;
    auto local = platform::Endpoint::v4(127, 0, 0, 1, 0);
    REQUIRE(sock.open(platform::Endpoint::Family::V4, err, &local));
    soe::SessionParams params = params_with(true, false, 2);

    soe::Options options;
    options.service_interval = std::chrono::milliseconds(0);
    auto session = soe::create_session(options);
    session->connect("127.0.0.1", sock.local_endpoint()->port);

    platform::Endpoint client;
    bool have_client = false;
    auto pump = [&] {
        session->update();
        std::array<std::byte, 4096> buf;
        platform::Endpoint from;
        while (auto n = sock.recv_from(buf, from)) {
            ByteSpan d(buf.data(), *n);
            if (*n >= 10 && std::to_integer<int>(d[0]) == 0 && std::to_integer<int>(d[1]) == soe::op::SessionRequest && !have_client) {
                client = from;
                have_client = true;
                ByteWriter w;
                w.le<std::uint8_t>(0);
                w.le<std::uint8_t>(soe::op::SessionResponse);
                w.bytes(d.subspan(6, 4));
                w.be<std::uint32_t>(params.encode_key);
                w.le<std::uint8_t>(params.crc_bytes);
                w.le<std::uint8_t>(1);
                w.le<std::uint8_t>(0);
                w.be<std::uint32_t>(params.max_packet_size);
                sock.send_to(client, w.data());
            }
        }
    };
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (session->state() == SessionState::Connecting && std::chrono::steady_clock::now() < deadline) {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(session->state() == SessionState::Connected);

    const auto seeds = soe_seeds();
    Rng rng(0x5e55);
    for (int i = 0; i < 3000; ++i) {
        const Bytes& seed = seeds[rng.below(seeds.size())];
        Bytes plain = fuzz::mutate(seed, rng);
        Bytes wire;
        if (rng.chance(80) && !plain.empty() && std::to_integer<int>(plain[0]) == 0) wire = soe::encode_datagram(params, plain);
        else wire = fuzz::mutate(soe::encode_datagram(params, seed), rng);
        // Never disconnect packets: keep the session alive to exercise its handlers.
        if (wire.size() > 1 && std::to_integer<int>(wire[0]) == 0 && std::to_integer<int>(wire[1]) == soe::op::SessionDisconnect) continue;
        sock.send_to(client, wire);
        if (i % 16 == 0) {
            pump();
            while (session->receive()) {}
        }
    }
    for (int i = 0; i < 20; ++i) {
        pump();
        while (session->receive()) {}
    }
    session->disconnect();
    for (int i = 0; i < 10; ++i) pump();
    CHECK(true);
}
