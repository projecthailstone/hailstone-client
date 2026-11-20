#pragma once

// Semantic login service values. Neither SOE packet framing nor login wire
// opcodes are exposed to the connection or its consumers.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hailstone::net {

// The RoF2 client connects to the world server on this fixed port.
constexpr std::uint16_t kWorldPort = 9000;

// eqlsstr_us.txt ids the client shows itself.
namespace login_error {
constexpr std::int32_t NoError = 101;
constexpr std::int32_t Timeout = 103;             // login or server list unanswered
constexpr std::int32_t BadPassword = 105;         // what EQEmu sends
constexpr std::int32_t Terminated = 112;          // the server dropped us after login
constexpr std::int32_t OfflineTraderOnline = 114; // offer to remove the character
constexpr std::int32_t CharacterOnline = 302;     // offer to remove the character
constexpr std::int32_t JoinFailed = 324;          // play request unanswered
constexpr std::int32_t WorldUnavailable = 326;    // EQEmu: a locked world refused this account
constexpr std::int32_t NoConnection = 6011;       // no login host could be reached
constexpr std::int32_t FailedAttempts = 6018;     // "%1 failed login attempts"
constexpr std::int32_t WrongLocale = 6028;        // the server's country is not the client's
constexpr std::int32_t RemovingPlayer = 6075;     // "Please wait while the currently logged in player is removed ..."
} // namespace login_error

// A reply's common part (BaseReplyMessage): success u8, error id u32, text.
struct LoginReplyBase {
    bool success = false;
    std::int32_t error_id = 0; // eqlsstr id; 101 = no error
    std::string text;
};

struct LoginResult {
    bool success = false;
    std::int32_t error_id = 0; // eqlsstr id; 101 = no error
    std::string text;
    std::uint32_t account_id = 0;
    std::string session_key; // EQEmu: 10 characters
    std::int32_t failed_attempts = 0;
    bool show_player_count = false;
    std::string token;    // EQLogin+0x930: the next login's token
    std::string commerce; // EQLogin+0x9c0
};

struct ServerInfo {
    std::uint32_t id = 0;
    std::string name;
    std::string address; // world host as the login server lists it
    std::int32_t type = 0;   // flags: 1 standard, 8 preferred, 16 legends
    std::int32_t status = 0; // 0 up, 1 down, 4 locked (flags)
    std::int32_t players = 0;
    std::string country;
    std::string language;
    // eqmain's server row (@0x10036750): the locked bit wins over the down bit (6007 Locked, else 6006 Down, else
    // 6005 Up; with show-player-count a locked and down world reads "Locked (Down)" and an up one its player count,
    // which no Hailstone UI shows). The status never stops a play request: the server decides (see request_play).
    const char* status_text() const { return (status & 4) ? "Locked" : (status & 1) ? "Down" : "Up"; }
};

struct PlayResult {
    bool allowed = false;
    std::int32_t error_id = 0;
    std::string text;
    std::uint32_t server_id = 0;
    // Error 302 or 114: the original offers to remove the character that is still logged in.
    bool offer_kick() const {
        return !allowed && (error_id == login_error::CharacterOnline || error_id == login_error::OfflineTraderOnline);
    }
};

// What the world connection needs: OP_SendLoginInfo
// carries the account id as decimal text and the session key as the password.
struct WorldTicket {
    std::uint32_t account_id = 0;
    std::string session_key;
    std::string world_host;
    std::uint16_t world_port = kWorldPort;
    std::uint32_t server_id = 0;
    std::string server_name;
};

// UI/localization helpers for the login service, not wire parsers.
std::string login_error_text(std::int32_t error_id, std::span<const std::string> args = {});
bool load_login_strings(const std::filesystem::path& file);

namespace messages {

constexpr std::size_t kMaxLoginTokenBytes = 4096;
inline bool valid_login_token(std::string_view token) {
    return !token.empty() && token.size() <= kMaxLoginTokenBytes &&
           token.find('\0') == std::string_view::npos;
}


enum class LoginCommandKind : std::uint8_t { Handshake, Authenticate, ServerList, Play, Kick };

struct LoginCommand {
    LoginCommandKind kind = LoginCommandKind::Handshake;
    std::uint64_t id = 0; // semantic request identity, not the wire sequence
    std::string_view username;
    std::string_view password;
    std::string_view token;
    bool token_auth = false;
    std::uint32_t server_id = 0;
};

enum class LoginEventKind : std::uint8_t { Handshake, Authenticated, Servers, Played, Kicked, Push };

struct LoginEvent {
    LoginEventKind kind = LoginEventKind::Push;
    std::uint64_t request_id = 0;
    std::variant<std::monostate, LoginReplyBase, LoginResult,
                 std::vector<ServerInfo>, PlayResult> payload;
};

// Distinguish a malformed known reply from unknown optional pushes and
// superseded replies. None of these carry a wire opcode into LoginClient.
struct LoginCodecIssue {
    enum class Kind : std::uint8_t { Unsupported, Stale, Malformed };
    Kind kind = Kind::Unsupported;
    std::uint64_t request_id = 0;
    std::string detail;
};

} // namespace messages
} // namespace hailstone::net
