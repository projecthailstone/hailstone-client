#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

#include <array>
#include <zlib.h>

#include "core/byte_io.h"
#include "core/env.h"
#include "net/crypto/des.h"
#include "net/connection/login_client.h"
#include "net/messages/login.h"
#include "net/protocol/login/codec.h"
#include "net/protocol/login/opcodes.h"
#include "net/session/soe/framing.h"
#include "platform/udp_socket.h"

using namespace hailstone;
using namespace hailstone::net;
using soe::Bytes;
using soe::ByteSpan;

namespace {

std::vector<std::byte> hex(const char* s) {
    std::vector<std::byte> out;
    for (; s[0] && s[1]; s += 2) out.push_back(static_cast<std::byte>(std::stoi(std::string(s, 2), nullptr, 16)));
    return out;
}

std::vector<std::byte> text(std::string_view s, bool nul = true) {
    auto sp = std::as_bytes(std::span(s.data(), s.size()));
    std::vector<std::byte> out(sp.begin(), sp.end());
    if (nul) out.push_back(std::byte{0});
    return out;
}

void header(ByteWriter& w, std::uint32_t seq, std::uint8_t enc) {
    w.le<std::uint32_t>(seq);
    w.le<std::uint8_t>(0);
    w.le<std::uint8_t>(enc);
    w.le<std::uint32_t>(0);
}

} // namespace

TEST_CASE("DES block cipher matches the standard test vectors") {
    // Classic worked example (key 133457799BBCDFF1) and the all-zero vector.
    CHECK(des::Cipher(0x133457799BBCDFF1ull).encrypt(0x0123456789ABCDEFull) == 0x85E813540F0AB405ull);
    CHECK(des::Cipher(0).encrypt(0) == 0x8CA64DE9C1B123A7ull);
    CHECK(des::Cipher(0x133457799BBCDFF1ull).decrypt(0x85E813540F0AB405ull) == 0x0123456789ABCDEFull);
}

TEST_CASE("zero-key DES-CBC matches openssl and round-trips") {
    // printf 'nmsctest1\0nmsctest1\0' | openssl enc -des-cbc -K 0..0 -iv 0..0 (zero padded)
    auto plain = text("nmsctest1");
    auto pw = text("nmsctest1");
    plain.insert(plain.end(), pw.begin(), pw.end());
    auto enc = des::zero_cbc_encrypt(plain);
    CHECK(enc == hex("ec88270c6fad44b84645466e2c3e3c2483d7836b812baa55"));
    auto dec = des::zero_cbc_decrypt(enc);
    REQUIRE(dec);
    plain.resize(24, std::byte{0});
    CHECK(*dec == plain);
    CHECK_FALSE(des::zero_cbc_decrypt(hex("0102")));
}

TEST_CASE("login requests, byte for byte (eqmain.dll's CSHandshake, CSPlayerLogin, CSGetServerList, CSJoinServer)") {
    // Header after the opcode: sequence u32, compressed 0, encrypt type, unused u32.
    auto hs = login_codec::session_ready(2);
    CHECK(hs.opcode == login_op::SessionReady);
    CHECK(hs.data == hex("02000000" "00" "00" "00000000" "0b00")); // + opcode: 14 bytes

    // user\0 password\0 token\0 flag: "nmscgrp1\0nmscgrp1\0\0\0" (20 bytes) -> 24, DES-CBC key 0 IV 0 (openssl).
    auto login = login_codec::login_request(3, "nmscgrp1", "nmscgrp1");
    CHECK(login.opcode == login_op::Login);
    CHECK(login.data == hex("03000000" "00" "02" "00000000" "fb31c2bb8762776e0c87690b5f3d5313254dbe9f747d0356"));
    auto plain = des::zero_cbc_decrypt(std::span(login.data).subspan(10));
    REQUIRE(plain);
    CHECK(*plain == hex("6e6d736367727031" "006e6d7363677270" "3100000000000000"));

    // The two extra zero bytes can add a block: "abc\0de\0" was 7 bytes (8), "abc\0de\0\0\0" is 9 (16).
    auto grows = login_codec::login_request(3, "abc", "de");
    CHECK(grows.data == hex("03000000" "00" "02" "00000000" "93b155c272af04b0" "6162630064650000"));

    // Token login: "\0\0" token "\0\x01".
    auto token = login_codec::token_login_request(3, "TOKEN123");
    CHECK(token.data == hex("03000000" "00" "02" "00000000" "f8fff90b8aca170f9b607d60a609a869"));

    CHECK(login_codec::server_list_request(4).opcode == login_op::ServerListRequest);
    CHECK(login_codec::server_list_request(4).data == hex("04000000" "00" "00" "00000000"));
    auto play = login_codec::play_request(5, 2);
    CHECK(play.opcode == login_op::PlayEverquestRequest);
    CHECK(play.data == hex("05000000" "00" "00" "00000000" "02000000")); // + opcode: 16 bytes
}

TEST_CASE("token login rejects invalid tickets and preserves the whole bounded opaque ticket") {
    CHECK_FALSE(messages::valid_login_token(""));
    CHECK_FALSE(messages::valid_login_token(std::string("one\0two", 7)));
    CHECK_FALSE(messages::valid_login_token(std::string(messages::kMaxLoginTokenBytes + 1, 'x')));
    CHECK_THROWS_AS(login_codec::token_login_request(3, ""), std::runtime_error);
    CHECK_THROWS_AS(login_codec::token_login_request(3, std::string("one\0two", 7)), std::runtime_error);
    CHECK_THROWS_AS(login_codec::token_login_request(3, std::string(messages::kMaxLoginTokenBytes + 1, 'x')),
                    std::runtime_error);

    // The ticket is opaque: the client does not impose one issuer's alphabet or prefix.
    std::string token(messages::kMaxLoginTokenBytes, 'x');
    token.front() = '+';
    token.back() = '=';
    REQUIRE(messages::valid_login_token(token));
    const auto packet = login_codec::token_login_request(3, token);
    const auto plaintext = des::zero_cbc_decrypt(std::span(packet.data).subspan(10));
    REQUIRE(plaintext);
    ByteReader fields(*plaintext);
    CHECK(fields.cstring().empty());
    CHECK(fields.cstring().empty());
    CHECK(fields.cstring() == token);
    CHECK(fields.le<std::uint8_t>() == 1);
    while (fields.remaining()) CHECK(fields.le<std::uint8_t>() == 0);
}

namespace {

// EQEmu's PlayerLoginReply (58 bytes) encrypted into an 80-byte body.
std::vector<std::byte> eqemu_login_reply(std::uint32_t seq, bool ok, std::int32_t err, std::int32_t id, const char* key,
                                         std::int32_t failed = 0) {
    ByteWriter r;
    r.le<std::uint8_t>(ok);
    r.le<std::int32_t>(err);
    r.le<std::uint8_t>(0);
    r.le<std::uint16_t>(0);
    r.le<std::int32_t>(id);
    r.fixed_string(key, 11);
    r.le<std::int32_t>(failed);
    r.le<std::uint8_t>(1);
    for (std::int32_t v : {99, -1, 0, 0, 99, -1, 0}) r.le<std::int32_t>(v);
    r.zeros(2);
    REQUIRE(r.size() == 58);
    auto enc = des::zero_cbc_encrypt(r.data());
    enc.resize(80, std::byte{0});
    ByteWriter w;
    header(w, seq, 2);
    w.bytes(enc);
    return w.take();
}

std::vector<std::byte> server_list(std::uint32_t seq, const char* country = "us") {
    ByteWriter w;
    header(w, seq, 0);
    w.le<std::uint8_t>(1);
    w.le<std::int32_t>(101);
    w.le<std::uint8_t>(0);
    w.le<std::uint32_t>(2);
    for (std::uint32_t id : {1u, 9u}) {
        w.bytes(text(id == 1 ? "192.0.2.10" : "10.0.0.1"));
        w.le<std::int32_t>(1);
        w.le<std::uint32_t>(id);
        w.bytes(text(id == 1 ? "NMS Dev" : "Other"));
        w.bytes(text(id == 1 ? country : "us"));
        w.bytes(text("en"));
        w.le<std::int32_t>(id == 1 ? 0 : 4);
        w.le<std::int32_t>(id == 1 ? 3 : 0);
    }
    return w.take();
}

std::vector<std::byte> play_reply(std::uint32_t seq, bool ok, std::int32_t err, std::uint32_t server) {
    ByteWriter p;
    header(p, seq, 0);
    p.le<std::uint8_t>(ok);
    p.le<std::int32_t>(err);
    p.le<std::uint8_t>(0);
    p.le<std::uint32_t>(server);
    return p.take();
}

std::vector<std::byte> handshake_reply(std::uint32_t seq, bool ok) {
    ByteWriter w;
    header(w, seq, 0);
    w.le<std::uint8_t>(ok);
    w.le<std::int32_t>(ok ? 101 : 102);
    w.bytes(text(""));
    w.bytes(text(""));
    return w.take();
}

} // namespace

TEST_CASE("login reply: the key is read to its NUL; failures keep their id") {
    auto ok = login_codec::parse_login_reply(eqemu_login_reply(3, true, 101, 42, "ABCDEFGHIJ", 2));
    CHECK(ok.success);
    CHECK(ok.account_id == 42);
    CHECK(ok.session_key == "ABCDEFGHIJ");
    CHECK(ok.failed_attempts == 2);
    CHECK(ok.show_player_count);
    CHECK(ok.token.empty());
    auto bad = login_codec::parse_login_reply(eqemu_login_reply(3, false, 105, -1, ""));
    CHECK_FALSE(bad.success);
    CHECK(bad.error_id == 105);
    CHECK(login_error_text(105).find("not valid") != std::string::npos);
    CHECK_THROWS(login_codec::parse_login_reply(hex("0300000000")));

    // login.eqemulator.net's refusal, as it sent it: error 107 in 39 bytes, shorter than a whole reply.
    auto short_refusal = hex("03000000000200000000"
                             "9b765973643c809f1a04a779f157eeb66489a68c643c809f1a04a779f157eeb66489a68c643c80");
    auto refused = login_codec::parse_login_reply(short_refusal);
    CHECK_FALSE(refused.success);
    CHECK(refused.error_id == 107);
    CHECK(refused.session_key.empty());

    // A body that is not whole blocks is decrypted rounded up (the original's DecryptBody).
    auto odd = eqemu_login_reply(3, true, 101, 7, "KEY", 0);
    odd.resize(10 + 60);
    CHECK(login_codec::parse_login_reply(odd).account_id == 7);

    auto hs = login_codec::parse_handshake_reply(handshake_reply(2, true));
    CHECK(hs.success);
    CHECK(hs.error_id == 101);
}

TEST_CASE("server list and play reply, plain and compressed") {
    auto data = server_list(4);
    auto list = login_codec::parse_server_list(data);
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "NMS Dev");
    CHECK(list[0].address == "192.0.2.10");
    CHECK(list[0].country == "us");
    CHECK(list[0].players == 3);
    CHECK(list[1].id == 9);
    CHECK(list[1].status == 4);
    CHECK(std::string(list[1].status_text()) == "Locked");
    auto cut = data;
    cut.pop_back();
    CHECK_THROWS(login_codec::parse_server_list(cut));

    // Byte 6 of the packet (data[4]) = 1: the body after the header is zlib data.
    std::vector<std::byte> body(data.begin() + 10, data.end());
    uLongf zsize = compressBound(static_cast<uLong>(body.size()));
    std::vector<std::byte> z(zsize);
    REQUIRE(compress2(reinterpret_cast<Bytef*>(z.data()), &zsize, reinterpret_cast<const Bytef*>(body.data()),
                      static_cast<uLong>(body.size()), 6) == Z_OK);
    z.resize(zsize);
    std::vector<std::byte> packed(data.begin(), data.begin() + 10);
    packed[4] = std::byte{1};
    packed.insert(packed.end(), z.begin(), z.end());
    CHECK(login_codec::parse_server_list(packed).size() == 2);

    auto play = login_codec::parse_play_reply(play_reply(5, true, 101, 1));
    CHECK(play.allowed);
    CHECK(play.server_id == 1);
    auto busy = login_codec::parse_play_reply(play_reply(5, false, 302, 1));
    CHECK(busy.offer_kick());

    CHECK(login_codec::is_push(0x25));
    CHECK(login_codec::is_push(0x31));
    CHECK_FALSE(login_codec::is_push(0x2f));
    CHECK(locale_matches("us"));
    CHECK(locale_matches("US")); // not in the table: taken as us
    CHECK_FALSE(locale_matches("de"));
    CHECK(login_error_text(6018, std::vector<std::string>{"3"}).find("were 3 failed") != std::string::npos);
    CHECK(login_error_text(999999).find("999999") != std::string::npos);
}

TEST_CASE("server status text: the locked bit wins over the down bit, as eqmain's server row has it") {
    net::ServerInfo server;
    for (int flags : {0, 2}) {
        server.status = flags;
        CHECK(std::string(server.status_text()) == "Up");
    }
    for (int flags : {1, 3}) {
        server.status = flags;
        CHECK(std::string(server.status_text()) == "Down");
    }
    for (int flags : {4, 5}) { // @0x10036750: 6007 Locked for 5 too ("Locked (Down)" only with a player count)
        server.status = flags;
        CHECK(std::string(server.status_text()) == "Locked");
    }
}

TEST_CASE("login reply: a compressed body inflates to at most 1 MB, however large it is") {
    // 300 KB that does not compress, then 800 KB of zeros: 1.1 MB from about 300 KB of zlib data. The buffer used to
    // start at four times the input (1.2 MB here) and was checked against the cap only once full.
    std::vector<std::byte> body(1100 * 1024, std::byte{0});
    std::uint32_t x = 12345;
    for (std::size_t i = 0; i < 300 * 1024; ++i) {
        x = x * 1664525u + 1013904223u;
        body[i] = static_cast<std::byte>(x >> 24);
    }
    auto pack = [](const std::vector<std::byte>& plain) {
        uLongf zsize = compressBound(static_cast<uLong>(plain.size()));
        std::vector<std::byte> z(zsize);
        REQUIRE(compress2(reinterpret_cast<Bytef*>(z.data()), &zsize, reinterpret_cast<const Bytef*>(plain.data()),
                          static_cast<uLong>(plain.size()), 6) == Z_OK);
        z.resize(zsize);
        std::vector<std::byte> packet(10, std::byte{0});
        packet[4] = std::byte{1};
        packet.insert(packet.end(), z.begin(), z.end());
        return packet;
    };
    auto big = pack(body);
    REQUIRE(big.size() > 256 * 1024);
    CHECK_THROWS(login_codec::reply_body(0, big));
    body.resize(1000 * 1024);
    CHECK(login_codec::reply_body(0, pack(body)).size() == body.size());
}

namespace {

// A login server on loopback that speaks just enough SOE session for LoginClient.
struct FakeLoginServer {
    platform::UdpSocket sock;
    platform::Endpoint client;
    soe::SessionParams params;
    soe::InboundStream inbound;
    std::uint16_t next_out = 0;
    std::vector<AppPacket> received;

    FakeLoginServer() {
        std::string err;
        auto local = platform::Endpoint::v4(127, 0, 0, 1, 0);
        REQUIRE(sock.open(platform::Endpoint::Family::V4, err, &local));
        params.encode_key = 0x12345678u;
        params.crc_bytes = 2;
        params.max_packet_size = 512;
    }
    std::uint16_t port() const { return sock.local_endpoint()->port; }

    void send_app(std::uint16_t opcode, std::vector<std::byte> data) {
        for (auto& p : soe::make_reliable(soe::encode_app(AppPacket(opcode, std::move(data))), next_out, params))
            sock.send_to(client, soe::encode_datagram(params, p));
    }

    void poll() {
        std::array<std::byte, 4096> buf;
        platform::Endpoint from;
        while (auto n = sock.recv_from(buf, from)) {
            ByteSpan d(buf.data(), *n);
            if (std::to_integer<int>(d[0]) == 0 && std::to_integer<int>(d[1]) == soe::op::SessionRequest) {
                client = from;
                ByteWriter w;
                w.le<std::uint8_t>(0);
                w.le<std::uint8_t>(soe::op::SessionResponse);
                w.bytes(d.subspan(6, 4));
                w.be<std::uint32_t>(params.encode_key);
                w.le<std::uint8_t>(params.crc_bytes);
                w.le<std::uint8_t>(0);
                w.le<std::uint8_t>(0);
                w.be<std::uint32_t>(params.max_packet_size);
                sock.send_to(client, w.data());
                continue;
            }
            if (auto p = soe::decode_datagram(params, d)) handle(*p);
        }
    }

    void handle(ByteSpan p) {
        if (p.size() < 2) return;
        int op = std::to_integer<int>(p[1]);
        if (std::to_integer<int>(p[0]) != 0) {
            auto app = soe::decode_app(p);
            REQUIRE(app);
            received.push_back(*app);
        } else if (op == soe::op::Combined) {
            auto parts = soe::split_combined(p);
            REQUIRE(parts);
            for (auto part : *parts) handle(part);
        } else if (op == soe::op::Packet) {
            std::vector<Bytes> out;
            inbound.push(p, out);
            auto ack = soe::make_ack(soe::op::Ack, static_cast<std::uint16_t>(inbound.next() - 1));
            sock.send_to(client, soe::encode_datagram(params, ack));
            for (auto& o : out) handle(o);
        }
    }
};

} // namespace

TEST_CASE("login client: sequences 2-6, a dropped stray reply, the 60 s list refresh, the play timeout") {
    FakeLoginServer server;
    LoginClient::Options opt;
    opt.list_refresh = std::chrono::milliseconds(400);
    opt.play_timeout = std::chrono::milliseconds(300);
    LoginClient c(opt);
    c.start("127.0.0.1", server.port(), "nmscgrp1", "nmscgrp1");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto pump = [&](auto done) {
        while (!done() && c.state() != LoginState::Failed && std::chrono::steady_clock::now() < deadline) {
            c.update();
            server.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    auto last = [&]() -> const AppPacket& { return server.received.back(); };

    pump([&] { return !server.received.empty(); });
    REQUIRE(server.received.size() == 1);
    CHECK(last().opcode == login_op::SessionReady);
    CHECK(last().data == hex("020000000000000000000b00"));
    server.send_app(login_op::ChatMessage, handshake_reply(2, true)); // EQEmu hard-codes 2

    pump([&] { return server.received.size() == 2; });
    REQUIRE(server.received.size() == 2);
    CHECK(last().opcode == login_op::Login);
    CHECK(last().data == login_codec::login_request(3, "nmscgrp1", "nmscgrp1").data);
    server.send_app(login_op::LoginAccepted, eqemu_login_reply(3, true, 101, 2, "ABCDEFGHIJ", 1));
    pump([&] { return c.state() == LoginState::LoggedIn; });
    REQUIRE(c.state() == LoginState::LoggedIn);
    CHECK(c.notice().find("1 failed login") != std::string::npos);

    c.request_server_list();
    pump([&] { return server.received.size() == 3; });
    REQUIRE(server.received.size() == 3);
    CHECK(last().data == hex("04000000000000000000"));
    server.send_app(login_op::ServerListResponse, server_list(99)); // no such request: dropped
    server.send_app(login_op::ServerListResponse, server_list(4));
    pump([&] { return c.state() == LoginState::ServerList; });
    REQUIRE(c.state() == LoginState::ServerList);

    // After the refresh interval the list is asked for again with the next number.
    pump([&] { return server.received.size() == 4; });
    REQUIRE(server.received.size() == 4);
    CHECK(last().opcode == login_op::ServerListRequest);
    CHECK(login_codec::sequence_of(last().data) == 5);
    server.send_app(login_op::ServerListResponse, server_list(5));

    c.request_play(1);
    pump([&] { return server.received.size() == 5; });
    REQUIRE(server.received.size() == 5);
    CHECK(last().data == hex("06000000000000000000" "01000000"));
    server.send_app(login_op::PlayEverquestResponse, play_reply(5, true, 101, 1)); // stale number: dropped
    pump([&] { return c.state() != LoginState::RequestingPlay; });
    CHECK(c.state() == LoginState::Failed);
    CHECK(c.error_id() == login_error::JoinFailed); // 324 after the play timeout
}

TEST_CASE("login client: a refused login keeps the server's id; a foreign locale is refused locally") {
    FakeLoginServer server;
    LoginClient c;
    c.start("127.0.0.1", server.port(), "nmscgrp1", "wrong");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto pump = [&](auto done) {
        while (!done() && c.state() != LoginState::Failed && std::chrono::steady_clock::now() < deadline) {
            c.update();
            server.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    pump([&] { return !server.received.empty(); });
    server.send_app(login_op::ChatMessage, handshake_reply(2, true));
    pump([&] { return server.received.size() == 2; });
    server.send_app(login_op::LoginAccepted, eqemu_login_reply(3, false, 105, -1, ""));
    pump([] { return false; });
    CHECK(c.state() == LoginState::Failed);
    CHECK(c.error_id() == 105);
    CHECK(c.error().find("not valid") != std::string::npos);
}

TEST_CASE("login client: no reachable host is 6011") {
    platform::UdpSocket silent;
    std::string err;
    auto local = platform::Endpoint::v4(127, 0, 0, 1, 0);
    REQUIRE(silent.open(platform::Endpoint::Family::V4, err, &local));
    LoginClient::Options opt;
    opt.connect_timeout = std::chrono::milliseconds(200);
    LoginClient c(opt);
    std::uint16_t port = silent.local_endpoint()->port;
    c.start({{"127.0.0.1", port}, {"127.0.0.1", port}}, "u", "p");
    for (int i = 0; i < 400 && c.state() != LoginState::Failed; ++i) {
        c.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(c.state() == LoginState::Failed);
    CHECK(c.error_id() == login_error::NoConnection);
}

// Live check against a local login server, only when HAILSTONE_LIVE_LOGIN=host:port is set; HAILSTONE_LIVE_USER (default
// nmscgrp1, password = the name) picks the account. HAILSTONE_LIVE_REFRESH=1 also waits out the 60 s list refresh
// (sequence 5) before the play request (sequence 6).
TEST_CASE("live login (HAILSTONE_LIVE_LOGIN)") {
    const char* target = hailstone::core::env("LIVE_LOGIN");
    if (!target) return;
    std::string t = target;
    auto colon = t.rfind(':');
    REQUIRE(colon != std::string::npos);
    const char* u = hailstone::core::env("LIVE_USER");
    std::string user = u ? u : "nmscgrp1";
    bool refresh = hailstone::core::env("LIVE_REFRESH") != nullptr;
    LoginClient c;
    c.start(t.substr(0, colon), static_cast<std::uint16_t>(std::stoi(t.substr(colon + 1))), user, user);
    auto run_until = [&](auto done, int seconds) {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (!done() && c.state() != LoginState::Failed && std::chrono::steady_clock::now() < end) {
            c.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        INFO(c.error());
        REQUIRE(c.state() != LoginState::Failed);
    };
    run_until([&] { return c.state() == LoginState::LoggedIn; }, 20);
    CHECK(c.sequence() == 3);
    c.request_server_list();
    run_until([&] { return c.state() == LoginState::ServerList; }, 20);
    REQUIRE_FALSE(c.servers().empty());
    CHECK(c.sequence() == 4);
    if (refresh) {
        run_until([&] { return c.sequence() == 5; }, 70);
        CHECK(c.sequence() == 5);
        MESSAGE("list refreshed with sequence 5");
        run_until([] { return false; }, 2); // the refreshed list arrives
        CHECK(c.state() == LoginState::ServerList);
    }
    c.request_play(c.servers().front().id);
    run_until([&] { return c.state() == LoginState::PlayAccepted; }, 20);
    REQUIRE(c.state() == LoginState::PlayAccepted);
    CHECK(c.sequence() == (refresh ? 6u : 5u));
    CHECK(c.ticket()->session_key.size() == 10);
    MESSAGE("play accepted: world " << c.ticket()->world_host << ", sequence " << c.sequence());
}
