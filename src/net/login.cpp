#include "net/login.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>

#include <zlib.h>

#include "core/byte_io.h"
#include "core/log.h"
#include "core/process_serial.h"
#include "core/wipe.h"
#include "net/des.h"

// Login protocol as the original's eqmain.dll speaks it: LoginServerAPI's request
// counter and pending list, the BaseMessage header, CSPlayerLogin's DES block and the replies' layouts. EQEmu's
// loginserver (client.cpp, login_types.h) was the earlier source and still the server it is checked against.

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

void put_cstring(ByteWriter& w, const std::string& s) {
    w.bytes(std::as_bytes(std::span(s.data(), s.size())));
    w.le<std::uint8_t>(0);
}

AppPacket player_login(std::uint32_t sequence, const std::string& user, const std::string& password,
                       const std::string& token, bool token_login) {
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

AppPacket login_request(std::uint32_t sequence, const std::string& user, const std::string& password) {
    return player_login(sequence, user, password, "", false);
}

AppPacket token_login_request(std::uint32_t sequence, const std::string& token) {
    need(valid_login_token(token), "invalid login token");
    return player_login(sequence, "", "", token, true);
}

bool valid_login_token(std::string_view token) {
    return !token.empty() && token.size() <= kMaxLoginTokenBytes && token.find('\0') == std::string_view::npos;
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

} // namespace login_codec

const char* to_string(LoginState state) {
    switch (state) {
    case LoginState::Idle: return "idle";
    case LoginState::Connecting: return "connecting";
    case LoginState::Handshake: return "handshake";
    case LoginState::Authenticating: return "authenticating";
    case LoginState::LoggedIn: return "logged in";
    case LoginState::RequestingList: return "requesting server list";
    case LoginState::ServerList: return "server list";
    case LoginState::RequestingPlay: return "requesting play";
    case LoginState::KickOffered: return "offering to remove the logged in player";
    case LoginState::Kicking: return "removing the logged in player";
    case LoginState::PlayAccepted: return "play accepted";
    case LoginState::Failed: return "failed";
    }
    return "?";
}

namespace {

std::mutex g_strings_mutex;
std::map<std::int32_t, std::string> g_strings; // eqlsstr_us.txt, once loaded

// The ids the client shows itself, for when eqlsstr_us.txt is not loaded. The texts are Hailstone's own wording of
// what each id means, not the stock file's (clean room); a loaded file's texts replace them.
const char* builtin_text(std::int32_t id) {
    switch (id) {
    case 101: return "No problem.";
    case 102: return "Something went wrong, and the login server did not say what.";
    case 103: return "The login server took too long to answer.";
    case 105: return "That account name or password is not valid. Check them and try again.";
    case 106: return "The login server has no account with that name.";
    case 107: return "That password is not right for this account.";
    case 111: return "A character of yours is still in play on that server. Wait about a minute for it to leave, then try again.";
    case 112: return "The server closed the connection, most likely because you were idle for too long.";
    case 114: return "A character on this account is still in the world as an offline trader. One account can have only one character in the world at a time, on any server. Remove that character so you can log in?";
    case 302: return "A character on this account is already in the world. One account can have only one character in the world at a time, on any server. Remove that character so you can log in?";
    case 324: return "Joining the server failed for an unknown reason.";
    case 326: return "That server cannot be reached right now. Try again later.";
    case 337: return "This account is suspended.";
    case 338: return "This account is banned.";
    case 339: return "That server is full and takes no more logins until some players leave. Try again later.";
    case 6011: return "Could not reach the server. Check that you are connected to the internet.";
    case 6018: return "Since your last login there were %1 failed login attempts on this account.";
    case 6028: return "That server is for another language or region. Pick a different server.";
    case 6075: return "Removing the character that is logged in. Please wait.";
    default: return nullptr;
    }
}

std::string fill(std::string t, std::span<const std::string> args) {
    for (std::size_t at = 0; (at = t.find("<BR>", at)) != std::string::npos;) t.replace(at, 4, "\n");
    for (std::size_t at = 0; (at = t.find('%', at)) != std::string::npos;) {
        if (at + 1 < t.size() && t[at + 1] >= '1' && t[at + 1] <= '9') {
            auto n = static_cast<std::size_t>(t[at + 1] - '1');
            std::string v = n < args.size() ? args[n] : std::string();
            t.replace(at, 2, v);
            at += v.size();
        } else {
            ++at;
        }
    }
    return t;
}

} // namespace

bool load_login_strings(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return false;
    std::map<std::int32_t, std::string> table;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto space = line.find(' ');
        if (space == std::string::npos || space == 0) continue;
        char* end = nullptr;
        long id = std::strtol(line.c_str(), &end, 10);
        if (end != line.c_str() + space) continue;
        table[static_cast<std::int32_t>(id)] = line.substr(space + 1);
    }
    if (table.empty()) return false;
    std::lock_guard lock(g_strings_mutex);
    g_strings = std::move(table);
    return true;
}

std::string login_error_text(std::int32_t id, std::span<const std::string> args) {
    {
        std::lock_guard lock(g_strings_mutex);
        if (auto it = g_strings.find(id); it != g_strings.end()) return fill(it->second, args);
    }
    if (const char* t = builtin_text(id)) return fill(t, args);
    // An id with no text: the number, so it can still be looked up (our wording, not the original's).
    return std::format("Login message {} (no text for it).", id);
}

bool locale_matches(const std::string& server_country, const std::string& client_locale) {
    static constexpr const char* kLocales[] = {"us", "kr", "tw", "jp", "de", "fr", "cn"};
    std::string code = server_country.substr(0, 2);
    std::string mapped = "us";
    for (const char* l : kLocales)
        if (code == l) mapped = l;
    return mapped == client_locale;
}

LoginClient::LoginClient() : LoginClient(Options{}) {}
LoginClient::LoginClient(Options options) : options_(std::move(options)) {}
LoginClient::~LoginClient() { close(); }

void LoginClient::start(const std::string& host, std::uint16_t port, std::string user, std::string password) {
    start(std::vector<Host>{{host, port}}, std::move(user), std::move(password));
    core::wipe(password); // a short one's bytes stay in the moved-from string's own storage, on the stack
}

void LoginClient::start(std::vector<Host> hosts, std::string user, std::string password) {
    close();
    user_ = std::move(user);
    password_ = std::move(password);
    core::wipe(password);
    begin(std::move(hosts));
}

void LoginClient::start_token(const std::string& host, std::uint16_t port, std::string token) {
    start_token(std::vector<Host>{{host, port}}, std::move(token));
    core::wipe(token);
}

void LoginClient::start_token(std::vector<Host> hosts, std::string token) {
    close();
    token_login_ = true;
    if (!login_codec::valid_login_token(token)) {
        core::wipe(token);
        return fail(0, "The login ticket is empty, too long or contains a NUL byte.");
    }
    token_ = std::move(token);
    core::wipe(token);
    begin(std::move(hosts));
}

void LoginClient::begin(std::vector<Host> hosts) {
    hosts_ = std::move(hosts);
    host_index_ = 0;
    error_.clear();
    error_id_ = 0;
    notice_.clear();
    login_ = {};
    servers_.clear();
    play_ = {};
    if (hosts_.empty()) return fail(login_error::NoConnection);
    connect_next();
}

void LoginClient::wipe_credentials() {
    core::wipe(password_);
    core::wipe(token_);
    user_.clear();
}

void LoginClient::wipe_result() {
    core::wipe(login_.session_key);
    core::wipe(login_.token);
    login_ = {};
}

LoginClient::Clock::time_point LoginClient::now() const {
    return options_.session.clock ? options_.session.clock() : Clock::now();
}

// LoginClient::ConnectToLoginServer: a new LoginServerAPI (sequence counter 1, nothing pending) per attempt.
void LoginClient::connect_next() {
    const Host& h = hosts_[host_index_];
    session_ = Session::create(options_.session);
    session_->connect(h.host, h.port);
    sequence_ = 1;
    pending_.clear();
    state_ = LoginState::Connecting;
    connect_deadline_ = now() + options_.connect_timeout;
}

void LoginClient::fail(std::int32_t error_id, std::string text) {
    error_id_ = error_id;
    error_ = text.empty() || error_id != 0 ? login_error_text(error_id) : std::move(text);
    state_ = LoginState::Failed;
    pending_.clear();
    wipe_credentials();
    wipe_result();
    log::debug("login: {} ({})", error_, error_id);
    if (session_) session_->disconnect();
}

void LoginClient::send_request(std::uint16_t opcode, std::chrono::milliseconds timeout,
                               const std::function<AppPacket(std::uint32_t)>& build) {
    // LoginServerAPI::SendRequest @0x100135f0: the counter goes up before every request, wrapping to 1.
    if (++sequence_ >= login_codec::kSequenceLimit) sequence_ = 1;
    pending_.push_back({opcode, sequence_, now() + timeout});
    session_->send(build(sequence_));
}

void LoginClient::send_list_request() {
    last_list_request_ = now();
    send_request(login_op::ServerListRequest, options_.list_timeout,
                 [](std::uint32_t seq) { return login_codec::server_list_request(seq); });
}

// LoginServerAPI::OnTimeout: the canned replies of BuildTimeoutReplies.
void LoginClient::on_timeout(const Pending& p) {
    switch (p.opcode) {
    case login_op::SessionReady:
        // Hailstone launcher policy: short-lived tickets must not leave an unresponsive sign-in pending forever.
        // The original password path has no canned handshake-timeout reply and keeps its existing behaviour.
        if (token_login_) return fail(login_error::Timeout);
        break;
    case login_op::Login:
    case login_op::ServerListRequest: return fail(login_error::Timeout);
    case login_op::PlayEverquestRequest: return fail(login_error::JoinFailed);
    case login_op::KickPlayer: return fail(login_error::Timeout);
    default: break;
    }
    log::debug("login: request 0x{:04x} (sequence {}) timed out", p.opcode, p.sequence);
}

void LoginClient::update() {
    if (!session_ || state_ == LoginState::Idle) return;
    session_->update();
    if (state_ == LoginState::Failed) return;

    if (state_ == LoginState::Connecting) {
        if (session_->state() == SessionState::Connected) {
            // OnConnectComplete: the handshake, pending for 10 s; its timeout shows nothing (no canned reply).
            send_request(login_op::SessionReady, options_.connect_timeout,
                         [](std::uint32_t seq) { return login_codec::session_ready(seq); });
            state_ = LoginState::Handshake;
        } else if (session_->state() == SessionState::Disconnected || now() > connect_deadline_) {
            // OnConnectionTerminated in state 2: the next eqhost.txt host, or 6011 when none is left.
            log::debug("login: no session with {}:{}", hosts_[host_index_].host, hosts_[host_index_].port);
            session_.reset();
            if (++host_index_ < hosts_.size()) return connect_next();
            return fail(login_error::NoConnection);
        }
        return;
    }
    while (auto p = session_->receive()) {
        handle(*p);
        if (state_ == LoginState::Failed) return;
    }
    if (session_->state() == SessionState::Disconnected) {
        if (state_ == LoginState::PlayAccepted) return; // the login server may close once we're done
        bool logged_in = state_ == LoginState::LoggedIn || state_ == LoginState::RequestingList ||
                         state_ == LoginState::ServerList || state_ == LoginState::RequestingPlay ||
                         state_ == LoginState::KickOffered || state_ == LoginState::Kicking;
        return fail(logged_in ? login_error::Terminated : login_error::NoConnection);
    }
    const auto current_time = now();
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        if (current_time <= pending_[i].deadline) continue;
        Pending p = pending_[i];
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        on_timeout(p);
        if (state_ == LoginState::Failed) return;
        --i;
    }
    // MainLoop @0x10010d76: the list again once more than 60 s have passed since the last request.
    bool listing = state_ == LoginState::RequestingList || state_ == LoginState::ServerList ||
                   state_ == LoginState::RequestingPlay || state_ == LoginState::KickOffered ||
                   state_ == LoginState::Kicking;
    if (listing && current_time - last_list_request_ > options_.list_refresh) send_list_request();
}

void LoginClient::handle(const AppPacket& packet) {
    // OnRoutePacket @0x100151c0: a sequence below 0xffffff must be a pending request's, unless the opcode is a
    // server push; the reply's opcode is not compared with the request's.
    std::uint32_t seq = login_codec::sequence_of(packet.data);
    if (seq < login_codec::kSequenceLimit) {
        auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.sequence == seq; });
        if (it != pending_.end()) {
            pending_.erase(it);
        } else if (!login_codec::is_push(packet.opcode)) {
            log::debug("login: dropping opcode 0x{:04x} with sequence {} (no such request)", packet.opcode, seq);
            return;
        }
    }
    try {
        switch (packet.opcode) {
        case login_op::ChatMessage: {
            if (state_ != LoginState::Handshake) break;
            auto reply = login_codec::parse_handshake_reply(packet.data);
            if (!reply.success) return fail(reply.error_id);
            // Explicit token authentication sends empty user/password fields. A token failure never falls back
            // to another credential type, and a launcher's one-use ticket is forgotten as soon as it is sent.
            send_request(login_op::Login, options_.login_timeout, [&](std::uint32_t s) {
                return token_login_ ? login_codec::token_login_request(s, token_)
                                    : login_codec::login_request(s, user_, password_);
            });
            wipe_credentials();
            state_ = LoginState::Authenticating;
            break;
        }
        case login_op::LoginAccepted:
            if (state_ != LoginState::Authenticating) break;
            login_ = login_codec::parse_login_reply(packet.data);
            if (!login_.success) return fail(login_.error_id);
            if (login_.failed_attempts > 0) {
                std::string n = std::to_string(login_.failed_attempts);
                notice_ = login_error_text(login_error::FailedAttempts, std::span(&n, 1));
            }
            state_ = LoginState::LoggedIn;
            break;
        case login_op::ServerListResponse:
            // OnGetServerList replaces the list, a refresh included.
            servers_ = login_codec::parse_server_list(packet.data);
            servers_serial_ = core::next_process_serial();
            if (state_ == LoginState::RequestingList) state_ = LoginState::ServerList;
            break;
        case login_op::PlayEverquestResponse:
            if (state_ != LoginState::RequestingPlay) break;
            play_ = login_codec::parse_play_reply(packet.data);
            if (play_.offer_kick()) {
                // ServerSelectWnd::OnJoinServer @0x10037350: the Yes / No question; the login stays.
                state_ = LoginState::KickOffered;
                break;
            }
            if (!play_.allowed) {
                std::int32_t id = play_.error_id;
                fail(id);
                return;
            }
            state_ = LoginState::PlayAccepted;
            break;
        case login_op::KickPlayerReply: {
            // ServerSelectWnd's handler @0x10037740 (LoginClient callback +0x78): success asks to play on the same
            // server again (timeout 10 s); a failure shows the reply's error.
            if (state_ != LoginState::Kicking) break;
            auto reply = login_codec::parse_kick_reply(packet.data);
            if (!reply.success) return fail(reply.error_id);
            notice_.clear();
            send_play_request(play_server_);
            break;
        }
        default:
            // OP_LoginExpansionPacketData and the other pushes: not needed here.
            log::debug("login: ignoring opcode 0x{:04x} ({} bytes)", packet.opcode, packet.data.size());
            break;
        }
    } catch (const std::exception& e) {
        fail(0, std::format("malformed login packet 0x{:04x}: {}", packet.opcode, e.what()));
    }
}

void LoginClient::request_server_list() {
    if (state_ != LoginState::LoggedIn && state_ != LoginState::ServerList) return;
    send_list_request();
    state_ = LoginState::RequestingList;
}

void LoginClient::request_play(std::uint32_t server_id) {
    if (state_ != LoginState::ServerList) return;
    // ServerSelectWnd::WndNotification (@0x10038310): a server for another locale is refused here (unless
    // show-player-count). That is its only check: a down or locked world is asked like any other, and the server
    // decides (EQEmu's world admits an account at GM:MinStatusToBypassLockedServer, else answers 326).
    for (const auto& s : servers_) {
        if (s.id != server_id) continue;
        if (!login_.show_player_count && !locale_matches(s.country, options_.locale)) {
            error_id_ = login_error::WrongLocale;
            error_ = login_error_text(login_error::WrongLocale);
            notice_ = error_;
            return;
        }
    }
    send_play_request(server_id);
}

void LoginClient::send_play_request(std::uint32_t server_id) {
    play_server_ = server_id;
    send_request(login_op::PlayEverquestRequest, options_.play_timeout,
                 [server_id](std::uint32_t seq) { return login_codec::play_request(seq, server_id); });
    state_ = LoginState::RequestingPlay;
}

void LoginClient::answer_kick(bool yes) {
    if (state_ != LoginState::KickOffered) return;
    if (!yes) {
        state_ = LoginState::ServerList;
        return;
    }
    // ServerSelectWnd @0x100388dc: the request (LoginServerAPI @0x10013fb0 forgets an earlier one's pending entry
    // first), pending 60 s with the server's id, then 6075 while it waits.
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [](const Pending& p) { return p.opcode == login_op::KickPlayer; }),
                   pending_.end());
    send_request(login_op::KickPlayer, options_.kick_timeout,
                 [](std::uint32_t seq) { return login_codec::kick_request(seq); });
    notice_ = login_error_text(login_error::RemovingPlayer);
    state_ = LoginState::Kicking;
}

void LoginClient::close() {
    if (session_) session_->disconnect();
    session_.reset();
    pending_.clear();
    wipe_credentials();
    wipe_result();
    token_login_ = false;
    state_ = LoginState::Idle;
}

std::optional<WorldTicket> LoginClient::ticket() const {
    if (state_ != LoginState::PlayAccepted) return std::nullopt;
    // OnJoinServer: the reply's server id is looked up in the current list; that entry's address is the host.
    WorldTicket t;
    t.account_id = login_.account_id;
    t.session_key = login_.session_key;
    t.server_id = play_server_;
    for (const auto& s : servers_)
        if (s.id == play_.server_id) t.server_id = s.id;
    for (const auto& s : servers_) {
        if (s.id == t.server_id) {
            t.world_host = s.address;
            t.server_name = s.name;
        }
    }
    return t;
}

} // namespace hailstone::net
