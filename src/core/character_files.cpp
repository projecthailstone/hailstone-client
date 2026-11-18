#include "core/character_files.h"

#include <algorithm>
#include <cctype>

#include "core/ascii_case.h"
#include "core/game_dir.h"
#include "core/log.h"
#include "core/server_profile.h"

namespace hailstone {

std::string profile_id_for_world(std::string_view server_name) {
    std::string id;
    for (char c : server_name) {
        if (std::isalnum(static_cast<unsigned char>(c))) id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else if (!id.empty() && id.back() != '-') id.push_back('-');
    }
    while (!id.empty() && id.back() == '-') id.pop_back();
    if (id.empty()) return "server";
    // A Windows device name (con, nul, com1 ...) is kept from being the folder the same way make_profile_id() does.
    return sanitize_file_name(id) == id ? id : "_" + id;
}

std::string transient_profile_id(std::string_view server_name, std::string_view host, int port,
                                 const std::filesystem::path& servers) {
    std::string id = profile_id_for_world(server_name);
    auto saved = load_profile(id, servers);
    if (!saved || (saved->port == port && saved->host.size() == host.size() &&
                   std::equal(host.begin(), host.end(), saved->host.begin(), [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                   })))
        return id;
    return "_transient-" + id;
}

bool is_kept_character_file(std::string_view name) {
    static constexpr std::string_view kKept[] = {
        "UI.ini", "hotbuttons.ini", "BZR.ini", "BART.ini", "btrlog.txt", "AddressBook.txt", "IgnoreList.txt",
        "GuildNotes.txt", "chat.ini", "options.ini", "chat_history.txt", "scripts.ini", "kept_items.txt",
        "bank.dat", "blocked_buffs.ini", "hailstone.ini", "plugin-config",
    };
    for (std::string_view kept : kKept)
        if (iequals(name, kept)) return true;
    return istarts_with(name, "ui-") && iends_with(name, ".txt");
}

CharacterFiles character_files(std::string_view profile, std::string_view world, std::string_view character,
                               const std::filesystem::path& servers) {
    CharacterFiles f;
    if (!profile.empty() && !world.empty() && !character.empty()) f.dir = character_dir(profile, world, character, servers);
    return f;
}

int adopt_unsorted_character_files(std::string_view profile, std::string_view world, std::string_view character,
                                   const std::filesystem::path& servers) {
    const CharacterFiles to = character_files(profile, world, character, servers);
    std::error_code ec;
    if (to.empty() || std::filesystem::exists(to.dir, ec) || ec) return 0;
    const std::filesystem::path characters = profile_characters_dir(profile, servers);
    const std::string name = sanitize_file_name(character);
    const CharacterFiles from{characters / name};
    // Only the first world the character enters takes them: a character of the same name on another world is
    // someone else, whose prices, hot buttons and address book these are not.
    for (std::filesystem::directory_iterator it(characters, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dir_ec;
        if (it->path() != from.dir && std::filesystem::is_directory(it->path() / name, dir_ec)) return 0;
    }
    ec.clear();
    // Copied into a sibling folder first and renamed into place when every copy worked: a copy that fails leaves
    // no world folder behind, and the next entry tries again.
    const CharacterFiles part{to.dir.parent_path() / ("." + name + ".adopting")};
    const std::filesystem::path pairs[][2] = {
        {from.ui_ini(), part.ui_ini()},
        {from.hot_buttons(), part.hot_buttons()},
        {from.trader_prices(), part.trader_prices()},
        {from.address_book(), part.address_book()},
        {from.chat_ini(), part.chat_ini()},
    };
    std::filesystem::remove_all(part.dir, ec); // a run that died halfway
    int copied = 0;
    bool failed = false;
    for (const auto& [old_file, new_file] : pairs) {
        if (!std::filesystem::is_regular_file(old_file, ec)) continue;
        std::filesystem::create_directories(new_file.parent_path(), ec);
        if (!ec) std::filesystem::copy_file(old_file, new_file, ec);
        if (ec) {
            log::warn("layout: could not copy {} to {}: {}", old_file.string(), new_file.string(), ec.message());
            failed = true;
            break;
        }
        ++copied;
    }
    if (!failed && copied > 0) {
        std::filesystem::rename(part.dir, to.dir, ec);
        if (ec) {
            log::warn("layout: could not move {} to {}: {}", part.dir.string(), to.dir.string(), ec.message());
            failed = true;
        }
    }
    if (failed || copied == 0) {
        std::filesystem::remove_all(part.dir, ec);
        return 0;
    }
    log::info("layout: {} of {}'s files copied from {} to {}", copied, std::string(character), from.dir.string(),
              to.dir.string());
    return copied;
}

IniFile load_chat_settings(const GameDir& game, const std::filesystem::path& chat_ini) {
    IniFile ini;
    if (auto path = game.find("eqclient.ini")) ini.load(*path);
    if (chat_ini.empty()) return ini;
    IniFile own;
    if (!own.load(chat_ini)) return ini;
    for (const std::string& section : own.sections())
        for (const std::string& key : own.keys(section))
            if (auto v = own.get(section, key)) ini.set(section, key, *v);
    return ini;
}

IniFile load_option_settings(const GameDir& game, const std::filesystem::path& options_ini) {
    return load_chat_settings(game, options_ini);
}

bool save_chat_setting(const std::filesystem::path& chat_ini, std::string_view section, std::string_view key,
                       std::string_view value) {
    return save_chat_settings(chat_ini, section, {{key, std::string(value)}});
}

bool save_chat_settings(const std::filesystem::path& chat_ini, std::string_view section,
                        std::initializer_list<std::pair<std::string_view, std::string>> values) {
    if (chat_ini.empty()) return false;
    IniFile ini;
    ini.load(chat_ini);
    for (const auto& [key, value] : values) ini.set(section, key, value);
    return ini.save(chat_ini); // creates the folder

}

} // namespace hailstone
