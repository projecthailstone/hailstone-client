#include "net/protocol/login/codec.h"
#include "net/messages/login.h"
#include "net/protocol/login/opcodes.h"
#include "core/byte_io.h"
#include "net/crypto/des.h"
#include <algorithm>
#include <stdexcept>
#include <zlib.h>

namespace hailstone::net {

namespace {

// Plaintext login bodies also contain passwords and returned session keys. Clear their allocation before release.
struct WipeBytes {
    std::vector<std::byte>& bytes;
    ~WipeBytes() {
        bytes.resize(bytes.capacity());
        volatile std::byte* p = bytes.data();
        for (std::size_t i = 0; i < bytes.size(); ++i) p[i] = std::byte{0};
    }
};

} // namespace

namespace login_codec {

namespace {

// BaseMessage after the opcode: sequence, compressed flag, encrypt type (2 = DES), unused.
void write_header(ByteWriter& w, std::uint32_t sequence, std::uint8_t encrypt_type) {
    w.le<std::uint32_t>(sequence);
    w.le<std::uint8_t>(0);
    w.le<std::uint8_t>(encrypt_type);
    w.le<std::uint32_t>(0);
}

constexpr std::size_t kHeader = 10; // after the opcode

void need(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

void put_cstring(ByteWriter& w, std::string_view s) {
    w.bytes(std::as_bytes(std::span(s.data(), s.size())));
    w.le<std::uint8_t>(0);
}

AppPacket player_login(std::uint32_t sequence, std::string_view user, std::string_view password,
                       std::string_view token, bool token_login) {
    // CSPlayerLogin::WriteBody @0x10020510; EncryptBody zero-pads and DES-CBC encrypts (key and IV zero).
    ByteWriter plain;
    plain.reserve(user.size() + password.size() + token.size() + 4);
    put_cstring(plain, user);
    put_cstring(plain, password);
    put_cstring(plain, token);
    plain.le<std::uint8_t>(token_login ? 1 : 0);
    auto body = plain.take();
    const WipeBytes wipe_body{body};
    ByteWriter w;
    write_header(w, sequence, 2);
    w.bytes(des::zero_cbc_encrypt(body));
    return {login_op::Login, w.take()};
}

// A login reply inflates to at most this (the largest, a server list, is a few KB); larger is refused.
constexpr std::size_t kMaxInflated = std::size_t{1} << 20;

std::vector<std::byte> inflate_all(std::span<const std::byte> in) {
    // Capped from the start: a body over 256 KB would otherwise start past the cap, which is checked only when the
    // buffer fills.
    std::vector<std::byte> out(std::min(std::max<std::size_t>(in.size() * 4, 256), kMaxInflated));
    z_stream z{};
    need(inflateInit(&z) == Z_OK, "zlib init");
    z.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(in.data()));
    z.avail_in = static_cast<uInt>(in.size());
    int rc = Z_OK;
    while (rc == Z_OK) {
        if (z.total_out == out.size()) {
            if (out.size() >= kMaxInflated) break;
            out.resize(std::min(out.size() * 2, kMaxInflated));
        }
        z.next_out = reinterpret_cast<Bytef*>(out.data() + z.total_out);
        z.avail_out = static_cast<uInt>(out.size() - z.total_out);
        rc = inflate(&z, Z_NO_FLUSH);
    }
    std::size_t produced = z.total_out;
    inflateEnd(&z);
    need(rc == Z_STREAM_END, "bad zlib data in a login reply");
    out.resize(produced);
    return out;
}

LoginReplyBase read_base(ByteReader& r) {
    LoginReplyBase b;
    b.success = r.le<std::uint8_t>() != 0;
    b.error_id = r.le<std::int32_t>();
    b.text = r.cstring();
    return b;
}

} // namespace

AppPacket session_ready(std::uint32_t sequence) {
    ByteWriter w;
    write_header(w, sequence, 0);
    w.le<std::uint16_t>(kHandshakeVersion);
    return {login_op::SessionReady, w.take()};
}

AppPacket login_request(std::uint32_t sequence, std::string_view user, std::string_view password) {
    return player_login(sequence, user, password, "", false);
}

AppPacket token_login_request(std::uint32_t sequence, std::string_view token) {
    need(messages::valid_login_token(token), "invalid login token");
    return player_login(sequence, "", "", token, true);
}

AppPacket server_list_request(std::uint32_t sequence) {
    ByteWriter w;
    write_header(w, sequence, 0);
    return {login_op::ServerListRequest, w.take()};
}

AppPacket kick_request(std::uint32_t sequence) {
    // The request class at 0x100209b0 writes no body (its vf3 / vf4 are 0x100205a0).
    ByteWriter w;
    write_header(w, sequence, 0);
    return {login_op::KickPlayer, w.take()};
}

AppPacket play_request(std::uint32_t sequence, std::uint32_t server_id) {
    ByteWriter w;
    write_header(w, sequence, 0);
    w.le<std::uint32_t>(server_id);
    return {login_op::PlayEverquestRequest, w.take()};
}

std::uint32_t sequence_of(std::span<const std::byte> data) {
    if (data.size() < 4) return 0;
    ByteReader r(data);
    return r.le<std::uint32_t>();
}

bool is_push(std::uint16_t opcode) { return (opcode >= 0x25 && opcode <= 0x2d) || opcode == 0x30 || opcode == 0x31; }

std::vector<std::byte> reply_body(std::uint16_t opcode, std::span<const std::byte> data) {
    need(data.size() >= kHeader, "login reply shorter than its header");
    auto body = data.subspan(kHeader);
    std::vector<std::byte> out(body.begin(), body.end());
    // DecompressBody @0x1001b540: byte 6 of the packet is the compressed flag (data[4] after the opcode).
    if (std::to_integer<int>(data[4]) == 1) out = inflate_all(out);
    // DecryptBody @0x1001b010: the encrypt type the reply class sets, not the header's byte.
    if (opcode == login_op::LoginAccepted || opcode == login_op::OrderExpansionReply) {
        out.resize((out.size() + 7) / 8 * 8, std::byte{0});
        auto plain = des::zero_cbc_decrypt(out);
        need(plain.has_value(), "login reply is not whole DES blocks");
        out = std::move(*plain);
    }
    return out;
}

LoginReplyBase parse_handshake_reply(std::span<const std::byte> data) {
    auto body = reply_body(login_op::ChatMessage, data);
    ByteReader r(body);
    return read_base(r);
}

LoginResult parse_login_reply(std::span<const std::byte> data) {
    // SCPlayerLoginReply::ReadBody @0x10020bb0.
    auto body = reply_body(login_op::LoginAccepted, data);
    const WipeBytes wipe_body{body};
    ByteReader r(body);
    auto base = read_base(r);
    LoginResult res;
    res.success = base.success;
    res.error_id = base.error_id;
    res.text = base.text;
    // A refusal is shown by its error id alone. login.eqemulator.net's refusal (error 107) is a 40-byte body that
    // ends inside the offer fields, and the stock client shows its error, so the rest is not read when it failed.
    if (!res.success) return res;
    r.skip(2);
    res.account_id = r.le<std::uint32_t>();
    res.session_key = r.cstring();
    res.failed_attempts = r.le<std::int32_t>();
    res.show_player_count = r.le<std::uint8_t>() != 0;
    r.skip(7 * 4); // offer fields
    res.token = r.cstring();
    res.commerce = r.cstring();
    return res;
}

std::vector<ServerInfo> parse_server_list(std::span<const std::byte> data) {
    auto body = reply_body(login_op::ServerListResponse, data);
    ByteReader r(body);
    auto base = read_base(r);
    if (!base.success) throw std::runtime_error("server list refused: " + login_error_text(base.error_id));
    std::uint32_t count = r.le<std::uint32_t>();
    // Every entry takes at least 20 bytes, so a bad count fails before allocating.
    need(count <= r.remaining() / 20, "server list count is larger than the packet");
    std::vector<ServerInfo> out;
    for (std::uint32_t i = 0; i < count; ++i) {
        // EQLS::ReadServerData @0x1001a600.
        ServerInfo s;
        s.address = r.cstring();
        s.type = r.le<std::int32_t>();
        s.id = r.le<std::uint32_t>();
        s.name = r.cstring();
        s.country = r.cstring();
        s.language = r.cstring();
        s.status = r.le<std::int32_t>();
        s.players = r.le<std::int32_t>();
        out.push_back(std::move(s));
    }
    return out;
}

PlayResult parse_play_reply(std::span<const std::byte> data) {
    auto body = reply_body(login_op::PlayEverquestResponse, data);
    ByteReader r(body);
    auto base = read_base(r);
    PlayResult p;
    p.allowed = base.success;
    p.error_id = base.error_id;
    p.text = base.text;
    p.server_id = r.le<std::uint32_t>();
    return p;
}

LoginReplyBase parse_kick_reply(std::span<const std::byte> data) {
    auto body = reply_body(login_op::KickPlayerReply, data);
    ByteReader r(body);
    return read_base(r);
}


AppPacket Codec::encode(const messages::LoginCommand& command) {
    if (++sequence_ >= kSequenceLimit) sequence_ = 1;
    AppPacket packet;
    switch (command.kind) {
    case messages::LoginCommandKind::Handshake:
        packet = session_ready(sequence_);
        break;
    case messages::LoginCommandKind::Authenticate:
        packet = command.token_auth
            ? token_login_request(sequence_, command.token)
            : login_request(sequence_, command.username, command.password);
        break;
    case messages::LoginCommandKind::ServerList:
        packet = server_list_request(sequence_);
        break;
    case messages::LoginCommandKind::Play:
        packet = play_request(sequence_, command.server_id);
        break;
    case messages::LoginCommandKind::Kick:
        packet = kick_request(sequence_);
        break;
    default:
        throw std::invalid_argument("unsupported login command kind");
    }
    if (command.id) pending_[sequence_] = command.id;
    return packet;
}

std::variant<messages::LoginEvent, messages::LoginCodecIssue> Codec::decode(const AppPacket& packet) {
    using Issue = messages::LoginCodecIssue;
    const bool known = packet.opcode == login_op::ChatMessage ||
        packet.opcode == login_op::LoginAccepted ||
        packet.opcode == login_op::ServerListResponse ||
        packet.opcode == login_op::PlayEverquestResponse ||
        packet.opcode == login_op::KickPlayerReply;
    // Unknown pushes never consume a pending authentication or play request,
    // even when they reuse the sequence number of one of those requests.
    if (!known) return Issue{Issue::Kind::Unsupported, 0, "unhandled login-server push"};
    if (packet.data.size() < kHeader)
        return Issue{Issue::Kind::Malformed, 0, "login reply shorter than wire header"};
    const auto seq = sequence_of(packet.data);
    auto pending = pending_.find(seq);
    if (seq >= kSequenceLimit || pending == pending_.end())
        return Issue{Issue::Kind::Stale, 0, "unmatched login-server reply"};
    const std::uint64_t request_id = pending->second;
    try {
        messages::LoginEvent event;
        event.request_id = request_id;
        switch (packet.opcode) {
        case login_op::ChatMessage:
            event.kind = messages::LoginEventKind::Handshake;
            event.payload = parse_handshake_reply(packet.data);
            break;
        case login_op::LoginAccepted:
            event.kind = messages::LoginEventKind::Authenticated;
            event.payload = parse_login_reply(packet.data);
            break;
        case login_op::ServerListResponse:
            event.kind = messages::LoginEventKind::Servers;
            event.payload = parse_server_list(packet.data);
            break;
        case login_op::PlayEverquestResponse:
            event.kind = messages::LoginEventKind::Played;
            event.payload = parse_play_reply(packet.data);
            break;
        case login_op::KickPlayerReply:
            event.kind = messages::LoginEventKind::Kicked;
            event.payload = parse_kick_reply(packet.data);
            break;
        default: break;
        }
        // Retire only a reply successfully parsed and correlated. For a
        // malformed required reply, the connection owns the failure policy.
        pending_.erase(pending);
        return event;
    } catch (const std::exception& e) {
        pending_.erase(pending);
        return Issue{Issue::Kind::Malformed, request_id, e.what()};
    }
}

void Codec::cancel(std::uint64_t request_id) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->second == request_id) it = pending_.erase(it);
        else ++it;
    }
}

} // namespace login_codec


} // namespace hailstone::net
