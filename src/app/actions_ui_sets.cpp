// Hotbutton sets: CHotButtonWnd::WndNotification's text entry answer (Save hotbutton set) and
// HotButtons_LoadFromIniFile @0x68af20 (the Load / Delete / Clear confirmations' Yes); game/hot_button_sets.h.
#include "app/actions_ui.h"

#include "core/log.h"
#include "net/connection/game_connection.h"

namespace hailstone::app {

namespace {

// The set's file beside the character's hotbuttons.ini (the original's userdata\HB_<n>_<name>_<server>.ini).
std::filesystem::path set_path(const std::filesystem::path& char_ini, std::int32_t file) {
    return char_ini.parent_path() / game::hot_button_set_file(file);
}

} // namespace

void ActionsUi::save_now() {
    if (char_ini_path_.empty()) return;
    store_.save(char_ini_);
    sets_.save(char_ini_);
    if (!char_ini_.save(char_ini_path_)) log::warn("cannot write {}", char_ini_path_.string());
    dirty_ = false;
}

void ActionsUi::save_hot_button_set(net::GameConnection& conn, int bar, const std::string& name) {
    if (bar < 0 || bar >= game::kHotBars) return;
    int n = sets_.begin_save(name);
    if (n < 0) {
        // BuildStringFromEQSTR(8006, itoa(100)), colour 0xd
        messages_.emplace_back(conn.strings().format(game::kStrHbSetLimit, std::vector<std::string>{"100"}), 0xd);
        return;
    }
    if (!char_ini_path_.empty()) {
        const auto path = set_path(char_ini_path_, sets_.at(n).file);
        IniFile file;
        file.load(path);
        game::write_hot_button_set(file, store_, bar);
        if (!file.save(path)) log::warn("cannot write {}", path.string());
    }
    // eqstr 8007 with the name as the entry has it, then 0x5199c0 (sorted, every entry written)
    messages_.emplace_back(conn.strings().format(game::kStrHbSetSaved, std::vector<std::string>{sets_.at(n).name}), 0xf);
    sets_.sort();
    save_now();
}

void ActionsUi::load_hot_button_set(net::GameConnection& conn, int bar, int n) {
    if (bar < 0 || bar >= game::kHotBars || n < 0 || n >= game::kHotButtonSets || !sets_.at(n).in_use) return;
    IniFile file;
    if (!char_ini_path_.empty()) file.load(set_path(char_ini_path_, sets_.at(n).file));
    game::read_hot_button_set(file, store_, bar);
    save_now(); // SaveHotButtonsToIni
    messages_.emplace_back(conn.strings().format(game::kStrHbSetLoaded, std::vector<std::string>{sets_.at(n).name}), 0xf);
}

void ActionsUi::delete_hot_button_set(net::GameConnection& conn, int n) {
    if (n < 0 || n >= game::kHotButtonSets || !sets_.at(n).in_use) return;
    std::string text = conn.strings().format(game::kStrHbSetDeleted, std::vector<std::string>{sets_.at(n).name});
    sets_.remove(n); // not in use, changed, sorted (0x5199c0); the file stays
    save_now();
    messages_.emplace_back(std::move(text), 0xf);
}

void ActionsUi::clear_hot_bar(net::GameConnection& conn, int bar) {
    if (bar < 0 || bar >= game::kHotBars) return;
    game::clear_hot_bar(store_, bar);
    save_now();
    messages_.emplace_back(conn.strings().format(game::kStrHbCleared, std::vector<std::string>{}), 0xf);
}

} // namespace hailstone::app
