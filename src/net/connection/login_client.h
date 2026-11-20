#pragma once

// Login-server client: session handshake, credentials, server list and the
// "play" request that makes the world server expect us. Non-blocking: call
// update() regularly and watch state(). Wire format: docs/protocol/login.md;
// it follows the original's eqmain.dll.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "net/messages/login.h"
#include "net/session/configuration.h"
#include "net/protocol/login/session_settings.h"

namespace hailstone::net {

class LoginEndpoint;

enum class LoginState : std::uint8_t {
    Idle,
    Connecting,       // session handshake
    Handshake,        // SessionReady sent, waiting for ChatMessage
    Authenticating,   // Login sent
    LoggedIn,         // credentials accepted; call request_server_list()
    RequestingList,
    ServerList,       // servers() is valid; call request_play()
    RequestingPlay,
    KickOffered,      // the play reply was 302 or 114: answer_kick() with the player's Yes or No
    Kicking,          // the kick request is out (6075 shows); success asks to play again
    PlayAccepted,     // ticket() is valid
    Failed,           // see error()
};

const char* to_string(LoginState state);
// The client's locale code and the server's: the Play button maps the first two characters of the server's
// country through the locale table (us kr tw jp de fr cn; anything else is us) and compares it with /locale:.
bool locale_matches(const std::string& server_country, const std::string& client_locale = "us");

class LoginClient {
public:
    struct Options {
        // eqmain's LoginServerAPI::CreateUdpManager: a keepalive after 14.8 s without sending (Params 0xe), no hold,
        // no stat requests, resend adjust 2000, 100 reliable slots. It sets no no-data timeout (Params 0x12); the
        // client keeps its 14.8 s link death, which EQEmu's 9 s keepalive meets.
        session::Configuration session = login_session_configuration();
        // The pending requests' timeouts (TimeoutList): the connect 10 s (LoginServerAPI::GiveTime), the login
        // 20 s, the server list 120 s, the play request 10 s. The handshake has none.
        std::chrono::milliseconds connect_timeout{10000};
        std::chrono::milliseconds login_timeout{20000};
        std::chrono::milliseconds list_timeout{120000};
        std::chrono::milliseconds play_timeout{10000};
        // The kick request's timeout (ServerSelectWnd @0x100388f1: 60 s); its canned reply fails with 103.
        std::chrono::milliseconds kick_timeout{60000};
        // The server list is requested again once this much has passed since the last request (MainLoop).
        std::chrono::milliseconds list_refresh{60000};
        // Independent of the login application's wire codec.
        std::string session_protocol = "soe";
        std::string locale = "us";
    };

    LoginClient();
    explicit LoginClient(Options options);
    ~LoginClient();

    struct Host {
        std::string host;
        std::uint16_t port = 0;
    };
    // Connects to the first host; a failed connect moves on to the next (eqhost.txt Host, Host1, ...).
    void start(const std::string& host, std::uint16_t port, std::string user, std::string password);
    void start(std::vector<Host> hosts, std::string user, std::string password);
    // A launcher-issued, opaque RoF2 ticket. Invalid tickets fail before connecting; a refusal never retries
    // with a password. The ticket is held only until OP_Login is sent, and is never persisted.
    void start_token(const std::string& host, std::uint16_t port, std::string token);
    void start_token(std::vector<Host> hosts, std::string token);
    void update();

    void request_server_list();
    // Refused locally (error 6028, nothing sent) when the server's country does not match the locale and the
    // login reply did not set show-player-count.
    void request_play(std::uint32_t server_id);
    // The answer to the question a 302 / 114 play reply asks (KickOffered; the text is login_error_text of
    // play_result().error_id): Yes sends the kick request for that server (Kicking), whose successful reply asks
    // to play there again (ServerSelectWnd's handler @0x10037740); No goes back to the server list.
    void answer_kick(bool yes);
    // Closes the login session and clears credentials and the result; copy ticket() before calling this.
    void close();

    LoginState state() const { return state_; }
    const std::string& error() const { return error_; }
    // The eqlsstr id behind error(), or 0.
    std::int32_t error_id() const { return error_id_; }
    // Text to show after a successful login (6018, failed attempts), or empty.
    const std::string& notice() const { return notice_; }
    const LoginResult& login_result() const { return login_; }
    const std::vector<ServerInfo>& servers() const { return servers_; }
    // Changes with each server list that arrives (unique in the process, so two logins' lists differ too); 0 none.
    std::uint32_t servers_serial() const { return servers_serial_; }
    const PlayResult& play_result() const { return play_; }
    // Valid in PlayAccepted.
    std::optional<WorldTicket> ticket() const;

    // The request counter (LoginServerAPI+0x1dc): the sequence the last request carried.
    std::uint32_t sequence() const;

private:
    using Clock = std::chrono::steady_clock;
    struct Pending {
        messages::LoginCommandKind kind = messages::LoginCommandKind::Handshake;
        std::uint64_t id = 0;
        Clock::time_point deadline;
    };

    void connect_next();
    void begin(std::vector<Host> hosts);
    void wipe_credentials();
    void wipe_result();
    Clock::time_point now() const;
    void fail(std::int32_t error_id, std::string text = {});
    void handle(const messages::LoginEvent& event);
    // Registers a semantic request; the login codec owns its wire sequence.
    void send_request(messages::LoginCommand command, std::chrono::milliseconds timeout);
    void on_timeout(const Pending& p);
    void send_list_request();
    void send_play_request(std::uint32_t server_id);

    Options options_;
    std::unique_ptr<LoginEndpoint> endpoint_;
    std::vector<Host> hosts_;
    std::size_t host_index_ = 0;
    LoginState state_ = LoginState::Idle;
    std::string error_;
    std::int32_t error_id_ = 0;
    std::string notice_;
    std::string user_, password_, token_;
    bool token_login_ = false;
    Clock::time_point connect_deadline_;
    std::uint64_t next_request_id_ = 0;
    std::vector<Pending> pending_;
    Clock::time_point last_list_request_{};
    LoginResult login_;
    std::vector<ServerInfo> servers_;
    std::uint32_t servers_serial_ = 0;
    PlayResult play_;
    std::uint32_t play_server_ = 0;
};

} // namespace hailstone::net
