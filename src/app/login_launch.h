#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/asset_folder_choice.h"
#include "core/server_profile.h"
#include "core/wipe.h"
#include "net/connection/login_client.h"

namespace hailstone::app {

// Launcher credentials live only for this attempt, never in a profile or the keyring.
struct LoginLaunch {
    bool token_requested = false;
    std::string ticket;
    std::string world;
    std::string character; // RoF2's /name:<char>; main.cpp hands it to --character
    LoginLaunch() = default;
    LoginLaunch(const LoginLaunch&) = default;
    LoginLaunch& operator=(const LoginLaunch& other) {
        if (this != &other) {
            core::wipe(ticket);
            token_requested = other.token_requested;
            ticket = other.ticket;
            world = other.world;
            character = other.character;
        }
        return *this;
    }
    LoginLaunch(LoginLaunch&& other) noexcept
        : token_requested(other.token_requested), ticket(std::move(other.ticket)), world(std::move(other.world)),
          character(std::move(other.character)) {
        core::wipe(other.ticket);
        other.token_requested = false;
    }
    LoginLaunch& operator=(LoginLaunch&& other) noexcept {
        if (this != &other) {
            core::wipe(ticket);
            token_requested = other.token_requested;
            ticket = std::move(other.ticket);
            world = std::move(other.world);
            character = std::move(other.character);
            core::wipe(other.ticket);
            other.token_requested = false;
        }
        return *this;
    }
    ~LoginLaunch() { core::wipe(ticket); }
};

// Reads an environment variable for --ticket-env and removes it from the environment, so that the processes this
// one starts (box windows, add-on tools, helpers) do not inherit the ticket (platform::take_environment_variable).
using TakeEnv = std::function<std::optional<std::string>(const std::string&)>;

// Consumes one launcher argument (and its value), leaving unrelated client arguments alone.
// Errors name the option, never its credential. argv excludes the executable name.
bool consume_login_launch_arg(LoginLaunch& launch, std::span<const std::string_view> args, std::size_t& at,
                              const TakeEnv& env);
// As in RoF2, a ticket overrides legacy account/password arguments. Clear them before constructing the client,
// so a launcher can keep its existing argument template without loading a keyring credential or sending it.
void resolve_login_launch(const LoginLaunch& launch, std::string& user, std::string& password,
                          std::string& account, bool viewer);

// Whether the asset folder's eqhost.txt may name a ticket's login server: a folder named for this run (--game-dir or
// EQ_GAME_DIR), or the EverQuest folder Hailstone is installed in (<EQ>/hailstone/, as a server's patcher puts it;
// never saved, so it is that installation's own, as RoF2 reads the eqhost.txt beside it). Not a folder remembered in
// hailstone.ini: it may be another server's.
inline bool eqhost_names_ticket_server(AssetFolderSource source) {
    return source == AssetFolderSource::Override || source == AssetFolderSource::Found;
}

// A ticket must go to an explicitly selected endpoint, never the last-used server. The asset folder's eqhost.txt
// counts only when `eqhost_trusted` (eqhost_names_ticket_server). A matching profile supplies its assets/UI without
// changing that endpoint.
ServerProfile token_login_profile(std::string_view saved_name, const ServerProfile* named,
                                  const std::optional<ServerProfile>& eqhost, bool eqhost_trusted,
                                  const std::vector<ServerProfile>& profiles, std::string_view host, int port);

// Whether the updater may act on the install at start (main.cpp: update_at_start, which can roll an update back
// and start the other version). Not for a ticket launch: a one-use ticket on the command line would be lost with
// the restart, as start_installed_client passes no arguments. The updater still runs once the client is up.
inline bool update_at_start_allowed(const LoginLaunch& launch) { return !launch.token_requested; }

// The parse_args warning for arguments nobody took. While a ticket is in use a ticket split at a space (or one in
// a mistyped option) could be among them, so only their number is logged; else each is named as before.
std::vector<std::string> unknown_argument_warnings(const LoginLaunch& launch, const std::vector<std::string>& unknown);

// A launcher world is an exact name (ignoring ASCII case), or a decimal login-server world id. Missing or
// ambiguous matches stay at server select; they never fall back to another world.
const net::ServerInfo* launcher_world(std::span<const net::ServerInfo> servers, std::string_view selector);

} // namespace hailstone::app
