// Hotbutton sets (game/hot_button_sets.h), the icon selection window (game/icon_selection.h) and the icon grid cells
// (ui/ui_textures.h grid_index_cell).
#include <doctest/doctest.h>

#include "core/ini_file.h"
#include "game/hot_button_sets.h"
#include "game/icon_selection.h"
#include "ui/ui_textures.h"

using namespace hailstone;

TEST_CASE("hotbutton sets: saved by name or the first unused entry, then sorted; a set keeps its file number") {
    game::HotButtonSets s;
    CHECK(s.begin_save("Raid") == 0);
    CHECK(s.at(0).file == 0);
    s.sort();
    CHECK(s.begin_save("buffs") == 1);
    CHECK(s.at(1).file == 1);
    s.sort(); // in use first, by name (__stricmp)
    CHECK(s.at(0).name == "buffs");
    CHECK(s.at(0).file == 1); // the file number stays with the set
    CHECK(s.at(1).name == "Raid");
    CHECK(s.begin_save("RAID") == 1); // the same set (case ignored) takes the name
    CHECK(s.at(1).name == "RAID");
    CHECK(s.at(1).file == 0); // its own file (the original: its index, 1, which is buffs' file)
    CHECK(s.at(0).file == 1);
    CHECK(s.in_use_count() == 2);
    CHECK(std::string(40, 'x').substr(0, game::kHotButtonSetNameMax).size() == 24);
    CHECK(s.at(s.begin_save(std::string(40, 'x'))).name.size() == game::kHotButtonSetNameMax);
}

namespace {

// The files of the sets in use (no two the same).
bool files_distinct(const game::HotButtonSets& s) {
    for (int a = 0; a < game::kHotButtonSets; ++a)
        for (int b = a + 1; b < game::kHotButtonSets; ++b)
            if (s.at(a).in_use && s.at(b).in_use && s.at(a).file == s.at(b).file) return false;
    return true;
}

int find_set(const game::HotButtonSets& s, std::string_view name) {
    for (int n = 0; n < game::kHotButtonSets; ++n)
        if (s.at(n).in_use && s.at(n).name == name) return n;
    return -1;
}

} // namespace

TEST_CASE("hotbutton sets: a new set never takes the file of a set in use (the original's loss, fixed)") {
    // save A, save B, delete A, save C: in the original C is saved at index 1 with file 1, B's file
    game::HotButtonSets s;
    CHECK(s.begin_save("A") == 0);
    s.sort();
    CHECK(s.begin_save("B") == 1);
    s.sort();
    CHECK(s.remove(0)); // sorted: B (file 1) at index 0
    CHECK(s.at(0).name == "B");
    CHECK(s.begin_save("C") == 1);
    CHECK(s.at(1).file == 0); // A's old file, not B's
    s.sort();
    CHECK(s.at(find_set(s, "B")).file == 1);
    CHECK(files_distinct(s));

    // the same with three sets, the sort moving them past each other
    game::HotButtonSets t;
    t.begin_save("z");
    t.sort();
    t.begin_save("a"); // index 1, file 1; sorted: a (1), z (0)
    t.sort();
    t.begin_save("m"); // index 2, file 2; sorted: a (1), m (2), z (0)
    t.sort();
    CHECK(t.remove(0)); // a; sorted: m (2), z (0)
    CHECK(t.begin_save("b") == 2);
    CHECK(t.at(2).file == 1); // the lowest free number (the original: 2, m's file)
    t.sort();
    CHECK(t.at(find_set(t, "m")).file == 2);
    CHECK(t.at(find_set(t, "z")).file == 0);
    CHECK(files_distinct(t));

    // churn: saves, re-saves and deletes in any order leave every set in use with its own file
    game::HotButtonSets u;
    const char* names[] = {"q", "B", "x", "a", "M", "c", "k", "Z", "e", "h", "p"};
    for (int round = 0; round < 40; ++round) {
        const char* name = names[(round * 7) % 11];
        int at = find_set(u, name);
        if (round % 3 == 2 && u.in_use_count() > 0) u.remove((round / 3) % u.in_use_count());
        else if (at >= 0 || u.in_use_count() < game::kHotButtonSets) {
            std::int32_t before = at >= 0 ? u.at(at).file : -1;
            int n = u.begin_save(name);
            REQUIRE(n >= 0);
            if (before >= 0) CHECK(u.at(n).file == before); // a set saved again keeps its file
            u.sort();
        }
        CHECK(files_distinct(u));
    }
    for (int n = 0; n < game::kHotButtonSets; ++n)
        if (u.at(n).in_use) CHECK((u.at(n).file >= 0 && u.at(n).file < game::kHotButtonSets));
}

TEST_CASE("hotbutton sets: the original's [HBLoadouts] loads as written; a shared file is split on the next save") {
    // what the original writes: file numbers that are not the index, and after its loss two sets on one file
    IniFile ini;
    ini.set_int("HBLoadouts", "HBSet1.inuse", 1);
    ini.set("HBLoadouts", "HBSet1.name", "Buffs");
    ini.set_int("HBLoadouts", "HBSet1.fileNum", 4);
    ini.set_int("HBLoadouts", "HBSet2.inuse", 1);
    ini.set("HBLoadouts", "HBSet2.name", "Pull");
    ini.set_int("HBLoadouts", "HBSet2.fileNum", 1);
    ini.set_int("HBLoadouts", "HBSet3.inuse", 1);
    ini.set("HBLoadouts", "HBSet3.name", "Raid");
    ini.set_int("HBLoadouts", "HBSet3.fileNum", 1);
    ini.set_int("HBLoadouts", "HBSet4.inuse", 0);
    game::HotButtonSets s;
    s.load(ini);
    CHECK(s.in_use_count() == 3);
    CHECK(s.at(0).file == 4); // read as written: HB_4.ini
    CHECK(s.at(1).file == 1);
    CHECK(s.at(2).file == 1);
    CHECK(s.begin_save("Buffs") == 0);
    CHECK(s.at(0).file == 4); // its own, kept
    CHECK(s.begin_save("raid") == 2);
    CHECK(s.at(2).file == 0); // shared with Pull: the lowest free number
    CHECK(s.at(1).file == 1); // Pull keeps HB_1.ini
    CHECK(s.begin_save("New") == 3);
    CHECK(s.at(3).file == 2);
    CHECK(files_distinct(s));
    s.sort();
    IniFile out;
    s.save(out); // the same keys as the original's
    CHECK(out.get_string("HBLoadouts", "HBSet1.name") == "Buffs");
    CHECK(out.get_int("HBLoadouts", "HBSet1.fileNum", -1) == 4);
    CHECK(out.get_string("HBLoadouts", "HBSet3.name") == "Pull");
    CHECK(out.get_int("HBLoadouts", "HBSet3.fileNum", -1) == 1);
    CHECK(out.get_string("HBLoadouts", "HBSet4.name") == "raid");
    CHECK(out.get_int("HBLoadouts", "HBSet4.fileNum", -1) == 0);
    game::HotButtonSets r;
    r.load(out);
    CHECK(r.at(1).name == "New");
    CHECK(r.at(1).file == 2);
}

TEST_CASE("hotbutton sets: ten at most; the delete sorts; [HBLoadouts] round trip writes changed entries") {
    game::HotButtonSets s;
    for (int i = 0; i < game::kHotButtonSets; ++i) CHECK(s.begin_save(std::string(1, static_cast<char>('a' + i))) == i);
    CHECK(s.begin_save("k") == -1); // eqstr 8006
    CHECK_FALSE(s.remove(-1));
    CHECK(s.remove(0));
    CHECK(s.at(0).name == "b");
    CHECK_FALSE(s.at(9).in_use);
    IniFile ini;
    s.save(ini);
    CHECK(ini.get_int("HBLoadouts", "HBSet1.inuse", -1) == 1);
    CHECK(ini.get_string("HBLoadouts", "HBSet1.name") == "b");
    CHECK(ini.get_int("HBLoadouts", "HBSet1.fileNum", -1) == 1);
    CHECK(ini.get_int("HBLoadouts", "HBSet10.inuse", -1) == 0);
    CHECK_FALSE(ini.get("HBLoadouts", "HBSet10.name"));
    game::HotButtonSets r;
    r.load(ini);
    CHECK(r.in_use_count() == 9);
    CHECK(r.at(8).name == "j");
    CHECK(r.at(8).file == 9);
    IniFile again;
    r.save(again); // nothing changed since the load: nothing written
    CHECK(again.sections().empty());
}

TEST_CASE("hotbutton sets: a set file holds the bar's 10 pages, an empty button's key removed; load clears first") {
    game::HotButtonStore store;
    game::HotButton b;
    b.type = game::HotButtonType::Social;
    b.slot = 3;
    b.label = "Hi";
    b.icon_type = 2;
    b.icon_slot = 7;
    store.at(4, 9, 11) = b;
    IniFile file;
    file.set("Hotbuttons", "Page1.Slot1", "stale");
    game::write_hot_button_set(file, store, 4);
    CHECK_FALSE(file.get("Hotbuttons", "Page1.Slot1"));
    CHECK(file.get_string("Hotbuttons", "Page10.Slot12") == "E3,C7,0000000000000000,0,Hi");
    game::HotButtonStore other;
    other.at(4, 0, 0).type = game::HotButtonType::SpellGem;
    other.at(3, 0, 0).type = game::HotButtonType::SpellGem;
    game::read_hot_button_set(file, other, 4);
    CHECK(other.at(4, 0, 0).empty());
    CHECK(other.at(3, 0, 0).type == game::HotButtonType::SpellGem); // another bar is left alone
    const game::HotButton& got = other.at(4, 9, 11);
    CHECK(got.type == game::HotButtonType::Social);
    CHECK(got.slot == 3);
    CHECK(got.icon_type == 2);
    CHECK(got.icon_slot == 7);
    CHECK(got.label == "Hi");
    game::clear_hot_bar(other, 4);
    CHECK(other.at(4, 9, 11).empty());
    CHECK(game::hot_button_set_file(3) == "HB_3.ini");
    CHECK(game::hot_button_scaled(37, 40) == 14);
    CHECK(game::hot_button_scaled(37, 175) == 64);
}

TEST_CASE("icon selection: caches, the page, the clamp and the range label") {
    CHECK(game::grid_cells(256, 256, 40, 40, 222) == 7992);
    CHECK(game::grid_cells(256, 256, 24, 24, 3) == 300);
    auto items = game::icon_cache_range(0, 7992);
    CHECK(items.base == 500);
    CHECK(items.min == 500);
    CHECK(items.max == 8492);
    CHECK(game::icon_cache_range(1, std::nullopt).max == 299);
    CHECK(game::icon_cache_range(2, std::nullopt).max == 0x23);
    game::IconSelection s;
    s.ranges[0] = items;
    s.ranges[1] = game::icon_cache_range(1, 300);
    s.ranges[2] = game::icon_cache_range(2, 100);
    // an icon-less button (-1, -1): type 1 (Spell), the icon clamped to the range's minimum
    CHECK(s.open(-1, -1));
    CHECK(s.type() == 1);
    CHECK(s.selected() == 0);
    CHECK(s.first() == 0);
    CHECK(s.last() == 49);
    CHECK(s.page_number() == 1);
    CHECK(s.range_text() == "Icons: 0 - 49");
    s.page_left(); // before the first page: the last one
    CHECK(s.first() == 250);
    CHECK(s.last() == 299);
    CHECK(s.page_number() == 6);
    s.page_right(); // past the end: the first
    CHECK(s.first() == 0);
    s.set_selected(1000);
    CHECK(s.selected() == 300); // SetSelectedIcon clamps to max itself
    s.choose_type(0);
    CHECK(s.selected() == 500);
    CHECK(s.range_text() == "Icons: 500 - 549");
    s.choose_per_page(4); // All
    CHECK(s.per_page() == -1);
    CHECK(s.last() == 8491);
    CHECK(s.page_number() == 1);
    s.page_right(); // nothing with All
    CHECK(s.first() == 500);
    CHECK_FALSE(s.open(3, 0)); // no cache for type 3 here
    CHECK(game::icon_type_animation(2) == std::string("A_MenuIcons"));
}

TEST_CASE("icon grid cells: down each column first when vertical, across rows otherwise, frame by frame") {
    // A server's UI with more spell icon sheets than stock RoF2's 7: Project Lazarus's A_SpellIcons lists 63, and
    // Group Spirit of the Wolf Pack's icon 1176 is on the 33rd (UiTextures::spell_icon goes by the animation).
    {
        auto wolf = ui::grid_index_cell(1176, 63, 256, 256, 40, 40, false);
        REQUIRE(wolf);
        CHECK(wolf->sheet == 33); // 1176 / 36 = 32
        CHECK(wolf->x == 0);      // cell 24: column 0
        CHECK(wolf->y == 160);    // row 4
        CHECK_FALSE(ui::grid_index_cell(1176, 7, 256, 256, 40, 40, false)); // past stock's 7 sheets
        auto gem = ui::grid_index_cell(2205, 23, 256, 256, 24, 24, false); // and its 23 gem sheets
        REQUIRE(gem);
        CHECK(gem->sheet == 23);
    }
    auto c = ui::grid_index_cell(7, 2, 256, 256, 40, 40, true); // 6 x 6 per frame
    REQUIRE(c);
    CHECK(c->sheet == 1);
    CHECK(c->x == 40);
    CHECK(c->y == 40);
    c = ui::grid_index_cell(37, 2, 256, 256, 40, 40, true);
    REQUIRE(c);
    CHECK(c->sheet == 2);
    CHECK(c->x == 0);
    CHECK(c->y == 40);
    c = ui::grid_index_cell(13, 1, 256, 256, 24, 24, false); // 10 per row
    REQUIRE(c);
    CHECK(c->x == 72);
    CHECK(c->y == 24);
    CHECK_FALSE(ui::grid_index_cell(100, 1, 256, 256, 24, 24, false));
    CHECK_FALSE(ui::grid_index_cell(-1, 1, 256, 256, 24, 24, false));
    // XML sizes whose cell count passes int are refused, not overflowed.
    CHECK_FALSE(ui::grid_index_cell(5, 1, 100000, 100000, 1, 1, false));
    // Within int per sheet, but not times the frames: 46340^2 cells, 2 frames.
    c = ui::grid_index_cell(2147395600, 2, 46340, 46340, 1, 1, false);
    REQUIRE(c);
    CHECK(c->sheet == 2);
}
