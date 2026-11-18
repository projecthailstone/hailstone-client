#pragma once

// The files a character keeps, as the original keeps them, one folder per world and character under the server's
// profile (core/server_profile.h): servers/<profile>/characters/<world>/<character>/, <world> the world's name as
// the login server lists it (world_folder_name). One login server can list several worlds, and a character's name
// is unique only on its world.
//   UI.ini            window places and the chat windows (the original's UI_<name>_<server>.ini)
//   hotbuttons.ini    hot buttons, socials and abilities (<name>_<server>.ini)
//   BZR.ini           the trader's prices (BZR_<name>_<server>.ini)
//   BART.ini          the barter buy lines and welcome text (UserData\BART_<name>_<server>.ini)
//   btrlog.txt        the barter log (Logs\btrlog_<server>_<name>.txt)
//   AddressBook.txt   the mail address book (AddressBook_<name>_<server>.txt)
//   chat.ini          the chat filters and the user chat colours, which the original writes into eqclient.ini
//   chat_history.txt  Hailstone: the chat's last lines, kept over camping, quitting and link deaths
//   ui-<pack>.txt     Hailstone: the UI's window layout for this character (ui::lua::LuaUi::set_character_settings;
//                     the UI's other settings are shared, ui/settings/<pack>.txt)
//   scripts.ini       Hailstone: the Lua scripts started for this character ([Autorun]; mq/lua_mq.h set_autorun_file)
//   kept_items.txt    Hailstone: the items marked Keep in the Advanced Inventory window (game/kept_items.h)
//   options.ini       the Options window's game options (eqclient.ini [Options] / [Defaults], the UI ini's [Main]
//                     ContextMenus and UseTellWindows, the character ini's [MailOptions]; game/client_options.h)
// The asset folder is only read: its eqclient.ini gives the chat settings' and the options' defaults, and chat.ini and
// options.ini take every change.

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "core/ini_file.h"
#include "platform/paths.h"

namespace hailstone {

class GameDir;

// The profile id of a world that has no saved profile: its name made a folder id as make_profile_id() does
// ("NMS Dev Server" -> "nms-dev-server"), without the numbered suffix, so a world always finds the same folder.
std::string profile_id_for_world(std::string_view server_name);
// The folder a test login's (a transient profile's) character files go in: profile_id_for_world(), unless a saved
// profile has that id for another address (host:port), whose characters' files a test character of the same name
// must not overwrite; then "_transient-<id>", which no saved profile can have.
std::string transient_profile_id(std::string_view server_name, std::string_view host, int port,
                                 const std::filesystem::path& servers = platform::servers_dir());

struct CharacterFiles {
    std::filesystem::path dir; // servers/<profile>/characters/<world>/<character>/; empty when no character is known

    bool empty() const { return dir.empty(); }
    std::filesystem::path ui_ini() const { return file("UI.ini"); }
    std::filesystem::path hot_buttons() const { return file("hotbuttons.ini"); }
    std::filesystem::path trader_prices() const { return file("BZR.ini"); }
    std::filesystem::path barter_lines() const { return file("BART.ini"); }
    std::filesystem::path barter_log() const { return file("btrlog.txt"); }
    std::filesystem::path address_book() const { return file("AddressBook.txt"); }
    std::filesystem::path ignore_list() const { return file("IgnoreList.txt"); }
    std::filesystem::path guild_notes() const { return file("GuildNotes.txt"); } // the guild window's personal notes
    std::filesystem::path chat_ini() const { return file("chat.ini"); }
    std::filesystem::path options_ini() const { return file("options.ini"); }
    std::filesystem::path chat_history() const { return file("chat_history.txt"); } // Hailstone: the last chat lines
    std::filesystem::path scripts_ini() const { return file("scripts.ini"); } // Hailstone: the scripts' autorun list
    std::filesystem::path kept_items() const { return file("kept_items.txt"); } // Hailstone: the Keep marks
    std::filesystem::path blocked_buffs() const { return file("blocked_buffs.ini"); }
    std::filesystem::path bank_snapshot() const { return file("bank.dat"); } // Hailstone: the bank as last seen
    // Hailstone: the character's client settings, over hailstone.ini (core/config.h Config::attach_character)
    std::filesystem::path settings() const { return file("hailstone.ini"); }
    // Hailstone: the UI pack's window layout for this character
    std::filesystem::path ui_settings(std::string_view pack) const {
        return dir.empty() ? std::filesystem::path() : dir / ("ui-" + std::string(pack) + ".txt");
    }

private:
    std::filesystem::path file(const char* name) const { return dir.empty() ? std::filesystem::path() : dir / name; }
};

// The files of `character` on the world `world` of the server `profile` (an empty profile, world or character gives
// empty paths).
// Whether `name`, a file name in a character's folder, is one of the files Hailstone keeps there (the list above, the
// GuildNotes.txt, IgnoreList.txt and ui-<pack>.txt files, the plugin-config folder), ignoring case as Windows does.
// /outputfile refuses those names: its output must not replace the character's settings.
bool is_kept_character_file(std::string_view name);

CharacterFiles character_files(std::string_view profile, std::string_view world, std::string_view character,
                               const std::filesystem::path& servers = platform::servers_dir());

// Builds before the files were kept per world wrote them to servers/<profile>/characters/<character>/. When no
// world's folder of a character of that name exists yet (the first world it enters) and that older folder holds any
// of the character's files, they are copied to this world's folder (character_files()), once: the world's folder
// exists from then on. They are copied into a sibling folder and renamed into place, so a failed copy leaves nothing
// and is tried again at the next entry. The older folder is left as it was. Returns the number of files copied.
int adopt_unsorted_character_files(std::string_view profile, std::string_view world, std::string_view character,
                                   const std::filesystem::path& servers = platform::servers_dir());

// A character's chat settings: chat.ini over the asset folder's eqclient.ini, which gives what chat.ini lacks. An
// empty `chat_ini` reads the defaults alone.
IniFile load_chat_settings(const GameDir& game, const std::filesystem::path& chat_ini);
// A character's game options: options.ini over the asset folder's eqclient.ini, as load_chat_settings.
IniFile load_option_settings(const GameDir& game, const std::filesystem::path& options_ini);
// Stores one chat setting in chat.ini (created with its folder when missing); the asset folder is never written.
// False when `chat_ini` is empty or cannot be written.
bool save_chat_setting(const std::filesystem::path& chat_ini, std::string_view section, std::string_view key,
                       std::string_view value);
// Several keys of one section in one write.
bool save_chat_settings(const std::filesystem::path& chat_ini, std::string_view section,
                        std::initializer_list<std::pair<std::string_view, std::string>> values);

} // namespace hailstone
