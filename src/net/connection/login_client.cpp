#include "net/connection/login_client.h"
#include "net/connection/login_endpoint.h"
#include "net/session/factory.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>


#include "core/log.h"
#include "core/process_serial.h"
#include "core/wipe.h"

// Login protocol as the original's eqmain.dll speaks it: LoginServerAPI's request
// counter and pending list, the BaseMessage header, CSPlayerLogin's DES block and the replies' layouts. EQEmu's
// loginserver (client.cpp, login_types.h) was the earlier source and still the server it is checked against.

namespace hailstone::net {

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
    if (!messages::valid_login_token(token)) {
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
    return options_.session.now();
}

// LoginClient::ConnectToLoginServer: a new LoginServerAPI (sequence counter 1, nothing pending) per attempt.
void LoginClient::connect_next() {
    const Host& h = hosts_[host_index_];
    session::SessionRegistry sessions;
    // Session protocol selection is independent of login packet encoding.
    // Mismatched concrete options are rejected by the selected factory.
    options_.session.protocol = options_.session_protocol;
    auto session = sessions.create(options_.session_protocol, options_.session);
    if (!session) return fail(0, "login session protocol is unavailable: " + options_.session_protocol);
    endpoint_ = std::make_unique<LoginEndpoint>(std::move(session));
    endpoint_->connect(h.host, h.port);
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
    if (endpoint_) endpoint_->disconnect();
}

void LoginClient::send_request(messages::LoginCommand command, std::chrono::milliseconds timeout) {
    command.id = ++next_request_id_;
    const auto kind = command.kind;
    if (!endpoint_ || !endpoint_->send(command)) {
        return fail(0, "the login codec rejected the request");
    }
    pending_.push_back({kind, command.id, now() + timeout});
}

void LoginClient::send_list_request() {
    // A refreshed list replaces the previous request, not its response
    // identity. Retire both pending deadline and codec sequence mapping so
    // a late old list cannot overwrite the current server catalog.
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->kind == messages::LoginCommandKind::ServerList) {
            endpoint_->cancel(it->id);
            it = pending_.erase(it);
        } else ++it;
    }
    send_request({.kind = messages::LoginCommandKind::ServerList}, options_.list_timeout);
    if (state_ != LoginState::Failed) last_list_request_ = now();
}

void LoginClient::on_timeout(const Pending& p) {
    using K = messages::LoginCommandKind;
    switch (p.kind) {
    case K::Handshake:
        // Preserve the original password-handshake behavior, but never keep
        // an opaque launcher ticket pending indefinitely.
        if (token_login_) return fail(login_error::Timeout);
        break;
    case K::Authenticate:
    case K::ServerList: return fail(login_error::Timeout);
    case K::Play: return fail(login_error::JoinFailed);
    case K::Kick: return fail(login_error::Timeout);
    }
    log::debug("login: semantic request {} timed out", p.id);
}

std::uint32_t LoginClient::sequence() const {
    return endpoint_ ? endpoint_->sequence() : 1;
}

void LoginClient::update() {
    if (!endpoint_ || state_ == LoginState::Idle) return;
    endpoint_->update();
    while (auto issue = endpoint_->receive_issue()) {
        if (issue->kind != messages::LoginCodecIssue::Kind::Malformed) continue;
        if (issue->request_id) {
            const auto pending = std::find_if(pending_.begin(), pending_.end(),
                [&](const Pending& p) { return p.id == issue->request_id; });
            if (pending == pending_.end()) continue; // superseded by a retry
        } else if (pending_.empty()) {
            continue; // no active request can be poisoned by this frame
        }
        // Before authentication, a bad mandatory handshake may try the next
        // configured login host. Password/token material is never replayed.
        if (state_ == LoginState::Handshake && host_index_ + 1 < hosts_.size()) {
            endpoint_.reset();
            ++host_index_;
            return connect_next();
        }
        return fail(0, "malformed required login response: " + issue->detail);
    }
    if (state_ == LoginState::Failed) return;

    if (state_ == LoginState::Connecting) {
        if (endpoint_->state() == SessionState::Connected) {
            // OnConnectComplete: the handshake, pending for 10 s; its timeout shows nothing (no canned reply).
            send_request({.kind = messages::LoginCommandKind::Handshake}, options_.connect_timeout);
            if (state_ != LoginState::Failed) state_ = LoginState::Handshake;
        } else if (endpoint_->state() == SessionState::Disconnected || now() > connect_deadline_) {
            // OnConnectionTerminated in state 2: the next eqhost.txt host, or 6011 when none is left.
            log::debug("login: no session with {}:{}", hosts_[host_index_].host, hosts_[host_index_].port);
            endpoint_.reset();
            if (++host_index_ < hosts_.size()) return connect_next();
            return fail(login_error::NoConnection);
        }
        return;
    }
    while (auto event = endpoint_->receive()) {
        handle(*event);
        if (state_ == LoginState::Failed) return;
    }
    if (endpoint_->state() == SessionState::Disconnected) {
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
        endpoint_->cancel(p.id);
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

void LoginClient::handle(const messages::LoginEvent& event) {
    // Correlation against the wire sequence is performed inside LoginCodec.
    // Pending deadlines and state-dependent replies remain connection policy.
    if (event.request_id) {
        auto it = std::find_if(pending_.begin(), pending_.end(),
                               [&](const Pending& p) { return p.id == event.request_id; });
        if (it == pending_.end()) return; // stale or cancelled request
        using Command = messages::LoginCommandKind;
        using Event = messages::LoginEventKind;
        const bool matching = (it->kind == Command::Handshake && event.kind == Event::Handshake) ||
            (it->kind == Command::Authenticate && event.kind == Event::Authenticated) ||
            (it->kind == Command::ServerList && event.kind == Event::Servers) ||
            (it->kind == Command::Play && event.kind == Event::Played) ||
            (it->kind == Command::Kick && event.kind == Event::Kicked);
        if (!matching) return; // a wrong-kind push must not consume the request
        pending_.erase(it);
    }
    using K = messages::LoginEventKind;
    switch (event.kind) {
    case K::Handshake: {
        if (state_ != LoginState::Handshake) break;
        const auto& reply = std::get<LoginReplyBase>(event.payload);
        if (!reply.success) return fail(reply.error_id);
        messages::LoginCommand command{.kind = messages::LoginCommandKind::Authenticate};
        command.token_auth = token_login_;
        command.username = user_;
        command.password = password_;
        command.token = token_;
        send_request(command, options_.login_timeout);
        // A one-use token is never retained for fallback; credentials leave
        // the connection as soon as the codec has synchronously encoded them.
        wipe_credentials();
        if (state_ != LoginState::Failed) state_ = LoginState::Authenticating;
        break;
    }
    case K::Authenticated:
        if (state_ != LoginState::Authenticating) break;
        login_ = std::get<LoginResult>(event.payload);
        if (!login_.success) return fail(login_.error_id);
        if (login_.failed_attempts > 0) {
            std::string n = std::to_string(login_.failed_attempts);
            notice_ = login_error_text(login_error::FailedAttempts, std::span(&n, 1));
        }
        state_ = LoginState::LoggedIn;
        break;
    case K::Servers:
        if (state_ != LoginState::RequestingList && state_ != LoginState::ServerList &&
            state_ != LoginState::RequestingPlay && state_ != LoginState::KickOffered &&
            state_ != LoginState::Kicking) break;
        servers_ = std::get<std::vector<ServerInfo>>(event.payload);
        servers_serial_ = core::next_process_serial();
        if (state_ == LoginState::RequestingList) state_ = LoginState::ServerList;
        break;
    case K::Played:
        if (state_ != LoginState::RequestingPlay) break;
        play_ = std::get<PlayResult>(event.payload);
        if (play_.offer_kick()) {
            state_ = LoginState::KickOffered;
            break;
        }
        if (!play_.allowed) return fail(play_.error_id);
        state_ = LoginState::PlayAccepted;
        break;
    case K::Kicked: {
        if (state_ != LoginState::Kicking) break;
        const auto& reply = std::get<LoginReplyBase>(event.payload);
        if (!reply.success) return fail(reply.error_id);
        notice_.clear();
        send_play_request(play_server_);
        break;
    }
    case K::Push:
        break; // other login-server notices are not consumed by the UI
    }
}

void LoginClient::request_server_list() {
    if (state_ != LoginState::LoggedIn && state_ != LoginState::ServerList) return;
    send_list_request();
    if (state_ != LoginState::Failed) state_ = LoginState::RequestingList;
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
    messages::LoginCommand command{.kind = messages::LoginCommandKind::Play};
    command.server_id = server_id;
    send_request(command, options_.play_timeout);
    if (state_ != LoginState::Failed) state_ = LoginState::RequestingPlay;
}

void LoginClient::answer_kick(bool yes) {
    if (state_ != LoginState::KickOffered) return;
    if (!yes) {
        state_ = LoginState::ServerList;
        return;
    }
    // ServerSelectWnd @0x100388dc: the request (LoginServerAPI @0x10013fb0 forgets an earlier one's pending entry
    // first), pending 60 s with the server's id, then 6075 while it waits.
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->kind == messages::LoginCommandKind::Kick) {
            endpoint_->cancel(it->id);
            it = pending_.erase(it);
        } else ++it;
    }
    send_request({.kind = messages::LoginCommandKind::Kick}, options_.kick_timeout);
    if (state_ != LoginState::Failed) {
        notice_ = login_error_text(login_error::RemovingPlayer);
        state_ = LoginState::Kicking;
    }
}

void LoginClient::close() {
    if (endpoint_) endpoint_->disconnect();
    endpoint_.reset();
    pending_.clear();
    next_request_id_ = 0;
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
