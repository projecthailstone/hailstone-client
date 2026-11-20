#include "core/server_profile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <system_error>

#include "core/ini_file.h"

namespace hailstone {

namespace {
char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower(c);
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return lower(x) == lower(y);
           });
}

// server.ini's section of a saved account: "Account:" and its username; in it, a character's key.
constexpr std::string_view kAccountPrefix = "Account:";
constexpr std::string_view kCharPrefix = "char:";

// CON, PRN, AUX, NUL, COM1-9, LPT1-9, with or without an extension.
bool windows_device_name(std::string_view name) {
    std::string stem = to_lower(name.substr(0, name.find('.')));
    static constexpr std::array<std::string_view, 4> kPlain{"con", "prn", "aux", "nul"};
    if (std::find(kPlain.begin(), kPlain.end(), stem) != kPlain.end()) return true;
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' &&
           stem[3] <= '9';
}
} // namespace

std::string sanitize_file_name(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name)
        out.push_back(alnum(c) || c == ' ' || c == '-' || c == '_' || c == '.' ? c : '_');
    auto trim = [](char c) { return c == ' ' || c == '.'; };
    while (!out.empty() && trim(out.back())) out.pop_back();
    while (!out.empty() && trim(out.front())) out.erase(out.begin());
    if (out.empty()) return "_";
    if (windows_device_name(out)) out.insert(out.begin(), '_');
    return out;
}

std::string make_profile_id(std::string_view name, const std::filesystem::path& servers) {
    std::string base;
    for (char c : name) {
        if (alnum(c)) base.push_back(lower(c));
        else if (!base.empty() && base.back() != '-') base.push_back('-');
    }
    while (!base.empty() && base.back() == '-') base.pop_back();
    if (base.empty()) base = "server";
    if (windows_device_name(base)) base.insert(base.begin(), '_');

    std::vector<std::string> taken;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(servers, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        taken.push_back(to_lower(it->path().filename().string()));
    auto free = [&](const std::string& id) { return std::find(taken.begin(), taken.end(), id) == taken.end(); };
    if (free(base)) return base;
    for (int n = 2;; ++n)
        if (std::string id = base + "-" + std::to_string(n); free(id)) return id;
}

std::filesystem::path profile_dir(std::string_view id, const std::filesystem::path& servers) {
    return servers / std::string(id);
}
std::filesystem::path profile_ini(std::string_view id, const std::filesystem::path& servers) {
    return profile_dir(id, servers) / "server.ini";
}
std::filesystem::path profile_assets_dir(std::string_view id, const std::filesystem::path& servers) {
    return profile_dir(id, servers) / "assets";
}
std::filesystem::path profile_maps_dir(std::string_view id, const std::filesystem::path& servers) {
    return profile_dir(id, servers) / "maps";
}
std::filesystem::path profile_characters_dir(std::string_view id, const std::filesystem::path& servers) {
    return profile_dir(id, servers) / "characters";
}
std::string world_folder_name(std::string_view world) { return sanitize_file_name(world); }
std::filesystem::path character_dir(std::string_view id, std::string_view world, std::string_view character,
                                    const std::filesystem::path& servers) {
    return profile_characters_dir(id, servers) / world_folder_name(world) / sanitize_file_name(character);
}

std::filesystem::path profile_worlds_ini(std::string_view id, const std::filesystem::path& servers) {
    return profile_dir(id, servers) / "worlds.ini";
}

std::vector<std::string> listed_worlds(std::string_view id, const std::filesystem::path& servers) {
    std::vector<std::string> out;
    IniFile ini;
    if (id.empty() || !ini.load(profile_worlds_ini(id, servers))) return out;
    for (const std::string& section : ini.sections())
        if (ini.get_int(section, "listed", 0) != 0) out.push_back(ini.get_string(section, "name", section));
    return out;
}

bool set_listed_worlds(std::string_view id, const std::vector<std::string>& worlds,
                       const std::filesystem::path& servers) {
    if (id.empty()) return false;
    if (listed_worlds(id, servers) == worlds) return true;
    const auto path = profile_worlds_ini(id, servers);
    IniFile ini;
    ini.load(path);
    for (const std::string& section : ini.sections()) ini.erase(section, "listed");
    // The listed ones in the login server's order: their sections are made again at the end.
    for (const std::string& w : worlds) {
        const std::string section = world_folder_name(w);
        std::vector<std::pair<std::string, std::string>> kept;
        for (const std::string& key : ini.keys(section))
            if (key != "name") kept.emplace_back(key, ini.get_string(section, key));
        ini.erase_section(section);
        ini.set(section, "name", w);
        for (const auto& [k, v] : kept) ini.set(section, k, v);
        ini.set_int(section, "listed", 1);
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return ini.save(path);
}

std::vector<ServerProfile> list_profiles(const std::filesystem::path& servers) {
    std::vector<ServerProfile> out;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(servers, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_directory(type_ec)) continue;
        if (auto p = load_profile(it->path().filename().string(), servers)) out.push_back(std::move(*p));
    }
    std::sort(out.begin(), out.end(), [](const ServerProfile& a, const ServerProfile& b) {
        auto la = to_lower(a.name), lb = to_lower(b.name);
        return la != lb ? la < lb : a.id < b.id;
    });
    return out;
}

std::optional<ServerProfile> load_profile(std::string_view id, const std::filesystem::path& servers) {
    if (id.empty()) return std::nullopt;
    IniFile ini;
    if (!ini.load(profile_ini(id, servers))) return std::nullopt;
    ServerProfile p;
    p.id = std::string(id);
    p.name = ini.get_string("Server", "name", id);
    p.host = ini.get_string("Server", "host");
    p.port = ini.get_int("Server", "port", p.port);
    // Host/Host1/... are an ordered set for a single login service. A hole terminates the list, as in eqmain.
    for (int i = 1; i <= 32; ++i) {
        const std::string suffix = std::to_string(i);
        const std::string host = ini.get_string("LoginHosts", "host" + suffix);
        if (host.empty()) break;
        const int port = ini.get_int("LoginHosts", "port" + suffix, ServerProfile{}.port);
        if (port < 1 || port > 65535) break;
        p.login_fallbacks.push_back({host, port});
    }
    p.username = ini.get_string("Server", "username");
    p.remember = ini.get_int("Server", "remember", 1) != 0;
    p.ui = ini.get_string("Server", "ui");
    p.game_codec = ini.get_string("Protocol", "game_codec", "rof2");
    p.session_protocol = ini.get_string("Protocol", "session_protocol", "soe");
    p.hailstone_extensions = ini.get_int("Protocol", "hailstone_extensions", 0) != 0;
    for (const std::string& section : ini.sections()) {
        if (!section.starts_with(kAccountPrefix) || section.size() == kAccountPrefix.size()) continue;
        SavedAccount a;
        a.username = section.substr(kAccountPrefix.size());
        a.character = ini.get_string(section, "character");
        a.world = ini.get_string(section, "world");
        for (const std::string& key : ini.keys(section)) {
            if (!key.starts_with(kCharPrefix)) continue;
            std::string_view rest = std::string_view(key).substr(kCharPrefix.size());
            std::size_t at = rest.find('@'); // a character's name has none
            if (at == 0 || at == std::string_view::npos) continue;
            SavedCharacter c;
            c.name = std::string(rest.substr(0, at));
            c.world = std::string(rest.substr(at + 1));
            std::string value = ini.get_string(section, key);
            c.level = std::atoi(value.c_str());
            if (std::size_t sp = value.find(' '); sp != std::string::npos) c.classes = value.substr(sp + 1);
            a.characters.push_back(std::move(c));
        }
        a.box = ini.get_int(section, "box", 0) != 0;
        a.save_password = ini.get_int(section, "save_password", 0) != 0;
        p.accounts.push_back(std::move(a));
    }
    return p;
}

bool save_profile(const ServerProfile& profile, const std::filesystem::path& servers) {
    if (profile.id.empty()) return false;
    const auto path = profile_ini(profile.id, servers);
    IniFile ini;
    ini.load(path); // keep what is there
    ini.set("Server", "name", profile.name);
    ini.set("Server", "host", profile.host);
    ini.set_int("Server", "port", profile.port);
    // Replace the full ordered fallback list, including obsolete entries from an earlier profile revision.
    ini.erase_section("LoginHosts");
    for (std::size_t i = 0; i < profile.login_fallbacks.size() && i < 32; ++i) {
        const auto& host = profile.login_fallbacks[i];
        if (host.host.empty() || host.port < 1 || host.port > 65535) break;
        const std::string suffix = std::to_string(i + 1);
        ini.set("LoginHosts", "host" + suffix, host.host);
        ini.set_int("LoginHosts", "port" + suffix, host.port);
    }
    if (profile.remember) ini.set("Server", "username", profile.username);
    else ini.erase("Server", "username");
    ini.set_int("Server", "remember", profile.remember ? 1 : 0);
    if (!profile.ui.empty()) ini.set("Server", "ui", profile.ui);
    else ini.erase("Server", "ui");
    ini.set("Protocol", "game_codec", profile.game_codec);
    ini.set("Protocol", "session_protocol", profile.session_protocol);
    ini.set_int("Protocol", "hailstone_extensions", profile.hailstone_extensions ? 1 : 0);
    for (const std::string& section : ini.sections())
        if (section.starts_with(kAccountPrefix)) ini.erase_section(section);
    for (const SavedAccount& a : profile.accounts) {
        if (a.username.empty()) continue;
        const std::string section = std::string(kAccountPrefix) + a.username;
        if (!a.character.empty()) ini.set(section, "character", a.character);
        if (!a.world.empty()) ini.set(section, "world", a.world);
        ini.set_int(section, "box", a.box ? 1 : 0);
        ini.set_int(section, "save_password", a.save_password ? 1 : 0);
        for (const SavedCharacter& c : a.characters)
            ini.set(section, std::string(kCharPrefix) + c.name + "@" + c.world,
                    std::to_string(c.level) + (c.classes.empty() ? "" : " " + c.classes));
    }
    std::error_code ec;
    std::filesystem::create_directories(profile_assets_dir(profile.id, servers), ec);
    return ini.save(path);
}

bool remove_profile(std::string_view id, const std::filesystem::path& servers) {
    if (id.empty() || sanitize_file_name(id) != id) return false; // never a path outside servers/
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(profile_dir(id, servers), ec)) return false;
    // One rename: all of it goes or nothing does (a file held open on Windows fails it whole), and it can be undone.
    const fs::path trash = deleted_profiles_dir(servers);
    fs::create_directories(trash, ec);
    auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    std::string base = std::string(id) + "-" + std::format("{:%Y%m%d-%H%M%S}", now);
    fs::path to = trash / base;
    for (int n = 2; fs::exists(to, ec); ++n) to = trash / (base + "-" + std::to_string(n));
    fs::rename(profile_dir(id, servers), to, ec);
    return !ec;
}

std::filesystem::path deleted_profiles_dir(const std::filesystem::path& servers) { return servers / ".deleted"; }

bool note_account_character(std::string_view id, std::string_view username, std::string_view world,
                            std::string_view character, const std::filesystem::path& servers) {
    auto p = load_profile(id, servers);
    if (!p) return false;
    SavedAccount* a = find_account(*p, username);
    if (!a || (a->character == character && a->world == world)) return false;
    a->character = std::string(character);
    a->world = std::string(world);
    return save_profile(*p, servers);
}

std::string saved_character_key_part(std::string_view name) {
    // The prefix keeps the key's first character fixed, so only the per-character rule applies to the name.
    return IniFile::clean_key(std::string(kCharPrefix) + std::string(name)).substr(kCharPrefix.size());
}

bool note_account_characters(std::string_view id, std::string_view username, std::string_view world,
                             const std::vector<SavedCharacter>& characters, const std::filesystem::path& servers) {
    auto p = load_profile(id, servers);
    if (!p) return false;
    SavedAccount* a = find_account(*p, username);
    if (!a) return false;
    // Compared and kept as the file stores them, so a world name with an '=' still replaces its old entries.
    const std::string saved_world = saved_character_key_part(world);
    std::vector<SavedCharacter> list;
    for (const auto& c : a->characters)
        if (c.world != saved_world) list.push_back(c);
    for (const auto& c : characters) {
        list.push_back(c);
        list.back().name = saved_character_key_part(c.name);
        list.back().world = saved_world;
    }
    auto same = [](const SavedCharacter& x, const SavedCharacter& y) {
        return x.name == y.name && x.world == y.world && x.level == y.level && x.classes == y.classes;
    };
    if (std::equal(list.begin(), list.end(), a->characters.begin(), a->characters.end(), same)) return false;
    a->characters = std::move(list);
    return save_profile(*p, servers);
}

SavedAccount* find_account(ServerProfile& profile, std::string_view username) {
    for (auto& a : profile.accounts)
        if (iequals(a.username, username)) return &a;
    return nullptr;
}

const SavedAccount* find_account(const ServerProfile& profile, std::string_view username) {
    return find_account(const_cast<ServerProfile&>(profile), username);
}

const ServerProfile* find_profile(const std::vector<ServerProfile>& profiles, std::string_view host, int port) {
    for (const auto& p : profiles)
        if (p.port == port && iequals(p.host, host)) return &p;
    return nullptr;
}

const ServerProfile* find_profile(const std::vector<ServerProfile>& profiles, std::string_view id) {
    for (const auto& p : profiles)
        if (iequals(p.id, id)) return &p;
    return nullptr;
}

std::string client_window_title(std::string_view server, std::string_view character) {
    std::string title = server.empty() ? std::string("Hailstone") : std::string(server);
    if (!character.empty()) title += " - " + std::string(character);
    return title;
}

std::optional<ServerProfile> profile_from_eqhost(std::string_view text) {
    auto trim = [](std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    };
    // eqmain's ReadHosts visits Host, Host1, Host2, ... until the first absent value.
    // Keep their ordering and the existing 64 KiB file-input limit. The 32-entry cap bounds profile state.
    std::array<std::optional<LoginHost>, 33> hosts{};
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    std::string_view section;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (line.starts_with('[')) {
            const std::size_t close = line.find(']');
            section = trim(line.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1));
            continue;
        }
        if (!section.empty() && !iequals(section, "LoginServer")) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view key = trim(line.substr(0, eq));
        if (key.size() < 4 || !iequals(key.substr(0, 4), "host")) continue;
        std::size_t index = 0;
        if (key.size() > 4) {
            std::string_view digits = key.substr(4);
            if (digits.empty() || digits.front() == '0') continue;
            const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
            if (ec != std::errc() || ptr != digits.data() + digits.size() || index > 32 || index == 0) continue;
        }
        // The first usable definition of a key wins, matching the earlier single-host importer.
        if (hosts[index]) continue;
        std::string_view value = trim(line.substr(eq + 1));
        std::string_view port_text;
        bool has_port = false;
        if (value.starts_with('[')) {
            const std::size_t close = value.find(']');
            if (close == std::string_view::npos) continue;
            const std::string_view rest = value.substr(close + 1);
            if (!rest.empty()) {
                if (rest.front() != ':') continue;
                port_text = rest.substr(1);
                has_port = true;
            }
            value = value.substr(1, close - 1);
        } else if (const std::size_t colon = value.find(':');
                   colon != std::string_view::npos && value.find(':', colon + 1) == std::string_view::npos) {
            port_text = value.substr(colon + 1);
            value = value.substr(0, colon);
            has_port = true;
        }
        LoginHost host;
        if (has_port) {
            port_text = trim(port_text);
            int port = 0;
            const auto [ptr, ec] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
            if (ec != std::errc() || ptr != port_text.data() + port_text.size() || port < 1 || port > 65535)
                continue;
            host.port = port;
        }
        value = trim(value);
        if (value.empty()) continue;
        host.host = std::string(value);
        hosts[index] = std::move(host);
    }
    if (!hosts[0]) return std::nullopt;
    ServerProfile profile;
    profile.host = std::move(hosts[0]->host);
    profile.port = hosts[0]->port;
    profile.name = profile.host;
    for (std::size_t i = 1; i < hosts.size() && hosts[i]; ++i)
        profile.login_fallbacks.push_back(std::move(*hosts[i]));
    return profile;
}

std::optional<ServerProfile> profile_from_eqhost_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string text(kMaxEqhostBytes, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(in.gcount()));
    return profile_from_eqhost(text);
}

} // namespace hailstone
