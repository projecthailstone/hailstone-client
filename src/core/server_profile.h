#pragma once

// The saved servers. Each is a folder servers/<id>/ under the data folder (platform::servers_dir()):
//   server.ini              [Server] name, host, port, username, remember, ui; [Account:<username>] character,
//                           world, box, save_password, char:<name>@<world>: the saved accounts and the characters
//                           each was seen to have. Other sections are kept (an older build's [NMS] the NMS plugin
//                           reads)
//   assets/                 the server's own client files, layered over the asset folder (GameDir::set_overlay)
//   maps/                   map edits made on this server (for all its worlds)
//   characters/<world>/<character>/
//                           each character's own files (UI.ini, hotbuttons.ini, BZR.ini, AddressBook.txt, chat.ini)
//   worlds.ini              [<world>] name, listed: the worlds the login server listed last
// A deleted profile's folder is kept in servers/.deleted/.
// The id is the folder name, made once from the server's name; renaming the server keeps the folder.
// No password is ever written here: a saved one is in the system's keyring (platform/credentials.h).

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platform/paths.h"

namespace hailstone {

// An account the login screen remembers for this server. Several make a box set: Log in all logs this window in to
// the first and starts another Hailstone for each of the rest (--account), each entering its character.
// A character an account has, as its world's character select last listed it (char:<name>@<world>=<level> <classes>).
struct SavedCharacter {
    std::string name;
    std::string world;   // the world's name as the login server lists it
    int level = 0;
    std::string classes; // short ("WAR", "SHD/CLR")
};

struct SavedAccount {
    std::string username;
    std::string character;      // the character entered last on this account, which a box login enters again
    std::string world;          // the world that character is on (played without the server select)
    std::vector<SavedCharacter> characters; // the login screen's Characters list: picked, it logs in and plays
    bool box = false;           // in the box set
    bool save_password = false; // its password is in the system keyring (credential_server(host, port), username)
};

struct LoginHost {
    std::string host;
    int port = 5997;
};

struct ServerProfile {
    std::string id; // the folder name under servers/
    std::string name;
    std::string host;
    int port = 5997;
    // Ordered fallback endpoints from eqhost.txt Host1, Host2, ... . The first host remains in host/port;
    // alternate endpoints belong to the same login-server profile and do not create separate saved servers.
    std::vector<LoginHost> login_fallbacks;
    std::string username;
    bool remember = true; // keep the username (the last account used) and the accounts list
    std::vector<SavedAccount> accounts; // in the order they were first used
    // The user interface used on this server (a folder of ui/, ui/ui_packs.h); empty means hailstone.ini ui.name.
    std::string ui;
    // Application and session choices are separate, even while only RoF2/SOE
    // is registered. Unknown names are preserved and rejected at endpoint
    // creation; never silently reinterpret a TOB/custom profile as RoF2.
    std::string game_codec = "rof2";
    std::string session_protocol = "soe";
    bool hailstone_extensions = false;
};

// A name made safe as one folder name on every file system: characters other than letters, digits, space, '-',
// '_' and '.' become '_', leading and trailing spaces and dots go, Windows device names (CON, NUL, COM1 ...) get a
// leading '_', and nothing left is "_". The case is kept.
std::string sanitize_file_name(std::string_view name);

// A new folder id for a server called `name`: lower case, runs of anything but letters and digits become one '-'
// ("My Server!" -> "my-server"), "server" when nothing is left, then "-2", "-3" ... until no folder in `servers`
// has that name in any case.
std::string make_profile_id(std::string_view name,
                            const std::filesystem::path& servers = platform::servers_dir());

// The folders and files of a profile.
std::filesystem::path profile_dir(std::string_view id,
                                  const std::filesystem::path& servers = platform::servers_dir());
std::filesystem::path profile_ini(std::string_view id, const std::filesystem::path& servers = platform::servers_dir());
std::filesystem::path profile_assets_dir(std::string_view id,
                                         const std::filesystem::path& servers = platform::servers_dir());
std::filesystem::path profile_maps_dir(std::string_view id,
                                       const std::filesystem::path& servers = platform::servers_dir());
std::filesystem::path profile_characters_dir(std::string_view id,
                                             const std::filesystem::path& servers = platform::servers_dir());
// The folder under characters/ of one world: its name as the login server lists it (WorldTicket::server_name),
// through sanitize_file_name(). One login server can list several worlds, and a character's name is unique only on
// its world, so the characters' files are kept per world.
std::string world_folder_name(std::string_view world);
// characters/<world>/<character>/: world_folder_name(world), then the character's name through sanitize_file_name().
std::filesystem::path character_dir(std::string_view id, std::string_view world, std::string_view character,
                                    const std::filesystem::path& servers = platform::servers_dir());

// servers/<id>/worlds.ini: one section per world (world_folder_name), with what the client keeps about that world.
// Kept apart from server.ini so a test login's transient profile stays out of list_profiles().
std::filesystem::path profile_worlds_ini(std::string_view id,
                                         const std::filesystem::path& servers = platform::servers_dir());

// The worlds the login server listed at the last login (listed=1, by their names), in that order.
std::vector<std::string> listed_worlds(std::string_view id,
                                       const std::filesystem::path& servers = platform::servers_dir());
// Records the worlds the login server lists now; written only when they changed. False when the write fails.
bool set_listed_worlds(std::string_view id, const std::vector<std::string>& worlds,
                       const std::filesystem::path& servers = platform::servers_dir());

// Every folder in `servers` with a server.ini, sorted by name (case-insensitively), then id.
std::vector<ServerProfile> list_profiles(const std::filesystem::path& servers = platform::servers_dir());
// servers/<id>/server.ini; nullopt when it cannot be read. A missing name is the id.
std::optional<ServerProfile> load_profile(std::string_view id,
                                          const std::filesystem::path& servers = platform::servers_dir());
// Writes servers/<id>/server.ini (keeping keys it does not know) and creates the assets/ folder. An empty id is an
// error (false); use make_profile_id() for a new profile first.
bool save_profile(const ServerProfile& profile, const std::filesystem::path& servers = platform::servers_dir());
// The login server an EverQuest install's eqhost.txt names, as a new profile named after the host, with no id yet:
// [LoginServer]'s first Host and consecutive Host1, Host2, ... entries; Host is the primary and later hosts are
// login_fallbacks. Each accepts "name:port", "name", "[v6]:port" or bare IPv6. A missing port keeps the profile
// default (what the original does then is not confirmed:
// docs/unknowns.md). A UTF-8 byte order mark is skipped. Nullopt when the text names none. A server's patcher ships
// this file, so it is the first server's default (ui::load_login_settings).
std::optional<ServerProfile> profile_from_eqhost(std::string_view text);
// The same from a file, of which at most kMaxEqhostBytes are read; nullopt when it cannot be read.
inline constexpr std::size_t kMaxEqhostBytes = 64 * 1024;
std::optional<ServerProfile> profile_from_eqhost_file(const std::filesystem::path& path);
// Takes the whole servers/<id>/ folder, with its assets, maps and characters' files, out of the list: it is renamed
// to servers/.deleted/<id>-<date>-<time>/ in one step, so it can be brought back by hand and a file held open never
// leaves half a profile. False when it fails (nothing changed then).
bool remove_profile(std::string_view id, const std::filesystem::path& servers = platform::servers_dir());
// servers/.deleted/, where remove_profile() puts the profiles it takes out (never a profile itself: an id has no
// leading dot).
std::filesystem::path deleted_profiles_dir(const std::filesystem::path& servers = platform::servers_dir());

// The OS window's title: the server's name, "Hailstone" before one is picked; in the world "<server> - <character>"
// (`character` with its classes when it has several, "Name [War/Clr]").
std::string client_window_title(std::string_view server, std::string_view character = {});

// The saved account with this username (case-insensitively), or nullptr.
SavedAccount* find_account(ServerProfile& profile, std::string_view username);
const SavedAccount* find_account(const ServerProfile& profile, std::string_view username);

// Records the character an account entered on a world (SavedAccount::character and world) in
// servers/<id>/server.ini, read fresh so another Hailstone's changes stay. Only for an account the profile has; false
// when nothing was written.
bool note_account_character(std::string_view id, std::string_view username, std::string_view world,
                            std::string_view character, const std::filesystem::path& servers = platform::servers_dir());
// Replaces the account's characters on this world with the ones its character select lists (those of other worlds
// stay); false when nothing changed or the account is not saved.
bool note_account_characters(std::string_view id, std::string_view username, std::string_view world,
                             const std::vector<SavedCharacter>& characters,
                             const std::filesystem::path& servers = platform::servers_dir());

// A character's or world's name as server.ini keeps it inside a char:<name>@<world> key (IniFile::clean_key): a
// line break or '=' in it is a '_'. A name read back from the file compares with a live one in this form.
std::string saved_character_key_part(std::string_view name);

// The first profile with this host (case-insensitively) and port, or nullptr.
const ServerProfile* find_profile(const std::vector<ServerProfile>& profiles, std::string_view host, int port);
const ServerProfile* find_profile(const std::vector<ServerProfile>& profiles, std::string_view id);

} // namespace hailstone
