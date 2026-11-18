// The per-frame work the client's glue keeps from frame to frame instead of rebuilding (app/group_ui.h and others).

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "app/actions_ui.h"
#include "app/group_ui.h"
#include "core/config.h"
#include "game/spells.h"
#include "game/world_state.h"
#include "scratch_dir.h"

using namespace hailstone;

namespace {

game::Spawn spawn(std::uint32_t id, std::string name, bool corpse = false) {
    game::Spawn s;
    s.id = id;
    s.name = std::move(name);
    s.is_corpse = corpse;
    return s;
}

} // namespace

TEST_CASE("Group window: a member's spawn by its name, EQEmu's digits and case aside") {
    CHECK(app::group_member_is("Soandso", "Soandso"));
    CHECK(app::group_member_is("Soandso", "soandso000"));
    CHECK(app::group_member_is("soandso", "SOANDSO12"));
    CHECK_FALSE(app::group_member_is("Soandso", "Soandsoa"));
    CHECK_FALSE(app::group_member_is("Soand", "Soandso"));
    CHECK_FALSE(app::group_member_is("Soandso", "123"));
}

TEST_CASE("Pet window: a buff's time left, held for a suspendable buff in a zone that suspends buffs") {
    game::SpellData suspendable;
    suspendable.suspendable = true;
    game::SpellData plain;
    game::BuffSlot b;
    b.tics = 11; // 66 s, sent 7.5 s ago
    b.received_ms = 100000;
    const std::int64_t now = 107500;
    CHECK(app::pet_buff_time_left(b, &plain, false, now) == "58s");
    CHECK(app::pet_buff_time_left(b, &suspendable, false, now) == "58s");
    CHECK(app::pet_buff_time_left(b, &plain, true, now) == "58s");     // EQEmu ticks it on
    CHECK(app::pet_buff_time_left(b, &suspendable, true, now) == "1m06s"); // the count as sent
    CHECK(app::pet_buff_time_left(b, nullptr, true, now) == "58s");     // a spell not known is not held
    CHECK(app::pet_buff_time_left(b, &plain, false, 200000).empty());  // run out
    b.tics = -1;
    CHECK(app::pet_buff_time_left(b, &suspendable, true, now).empty()); // permanent
}

TEST_CASE("Group window: each member's spawn is kept between frames and looked for again only when it may have come") {
    game::WorldState world;
    world.spawns[5] = spawn(5, "a_rat01");
    world.spawns[7] = spawn(7, "Soandso000");
    world.spawns[9] = spawn(9, "Soandso's_corpse", true);
    app::GroupMemberSpawns cache;
    std::int64_t now = 1000;
    auto frame = [&](const std::string& name) {
        cache.begin();
        const game::Spawn* s = cache.find(world, name, now);
        cache.end();
        return s ? s->id : 0u;
    };
    CHECK(frame("Soandso") == 7);
    CHECK(cache.scans() == 1);
    // Found: checked by its id, with no search.
    for (int i = 0; i < 10; ++i) CHECK(frame("Soandso") == 7);
    CHECK(cache.scans() == 1);
    // A member in another zone: searched for once, then again when the spawns change or after a second.
    CHECK(frame("Farfaraway") == 0);
    CHECK(cache.scans() == 2);
    now += 500;
    CHECK(frame("Farfaraway") == 0);
    CHECK(cache.scans() == 2);
    world.spawns[11] = spawn(11, "Farfaraway");
    CHECK(frame("Farfaraway") == 11);
    CHECK(cache.scans() == 3);
    // Gone (zoned, camped): searched for again at once; a corpse is not the member.
    world.spawns.erase(7);
    CHECK(frame("Soandso") == 0);
    // Another spawn taking the id is not the member either.
    world.spawns[11] = spawn(11, "Someoneelse");
    CHECK(frame("Farfaraway") == 0);
    // Back after a second although the count is the same.
    world.spawns[11] = spawn(11, "Farfaraway01");
    CHECK(frame("Farfaraway") == 0);
    now += 1000;
    CHECK(frame("Farfaraway") == 11);
}

namespace {
std::string read_text(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("Socials: a running social stops when another character enters the world, not on zoning") {
    test::ScratchDir scratch("actions_social_character");
    Config config;
    app::ActionsUi actions(config, scratch.path() / "hailstone.ini");
    actions.enter_zone(scratch.path() / "first.ini");
    actions.press_social(4);
    REQUIRE(actions.current_social() == 4);
    actions.enter_zone(scratch.path() / "first.ini"); // the same character in another zone
    CHECK(actions.current_social() == 4);
    actions.enter_zone(scratch.path() / "second.ini");
    CHECK(actions.current_social() == -1);
}

TEST_CASE("Hot buttons: changes are saved once they have waited, at once on zoning and at the end; not a page flip") {
    test::ScratchDir scratch("actions_save_delay");
    const std::filesystem::path file = scratch.path() / "hotbuttons.ini";
    Config config;
    {
        app::ActionsUi actions(config, scratch.path() / "hailstone.ini");
        actions.enter_zone(file);
        CHECK_FALSE(actions.unsaved());
        // A page flip changes nothing that is saved.
        actions.set_hot_bar_page(0, 3);
        CHECK_FALSE(actions.unsaved());
        // A button: kept unsaved for kSaveDelayMs from when it is first seen, however often it is asked.
        game::HotButton b;
        b.type = game::HotButtonType::Social;
        b.slot = 2;
        actions.set_hot_button(0, 1, b);
        CHECK(actions.unsaved());
        CHECK_FALSE(actions.save_if_due(10'000));
        CHECK_FALSE(actions.save_if_due(10'000 + app::ActionsUi::kSaveDelayMs - 1));
        CHECK_FALSE(std::filesystem::exists(file));
        CHECK(actions.save_if_due(10'000 + app::ActionsUi::kSaveDelayMs));
        CHECK_FALSE(actions.unsaved());
        CHECK(std::filesystem::exists(file));
        const std::string first = read_text(file);
        CHECK_FALSE(first.empty());
        // Nothing waiting: nothing written.
        CHECK_FALSE(actions.save_if_due(50'000));
        // Zoning (the same character) saves what waits at once.
        actions.set_hot_button(0, 2, b);
        CHECK_FALSE(actions.save_if_due(60'000));
        actions.enter_zone(file);
        CHECK_FALSE(actions.unsaved());
        CHECK(read_text(file) != first);
        // The end (camp, exit: the destructor and ClientApp::leave_character) saves too.
        actions.set_hot_button(0, 3, b);
        CHECK(actions.unsaved());
    }
    app::ActionsUi again(config, scratch.path() / "hailstone.ini");
    again.enter_zone(file);
    CHECK(again.hot_button_spec(0, 3, 3).value_or("") != "");
}
