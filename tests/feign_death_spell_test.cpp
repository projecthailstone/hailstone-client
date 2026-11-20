// A feign death spell (SPA 74: Imitate Death, Death Peace) landing on the player, against the mock EQEmu:
// CharacterZoneClient_StopCastAndFeignDeath @0x433b80 ends the bard twist, stops the cast and lies the player down
// (ChangePosition(0x73): OP_SpawnAppearance {me, 14, 115}). EQEmu's SE_FeignDeath sends no appearance itself.

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "game/spells.h"
#include "net/messages/spell.h"
#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/spell_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

// A spells_us.txt line with every column "0" except the given ones (no effects in slots 1..12: 254).
std::string spell_line(std::initializer_list<std::pair<int, std::string>> set) {
    std::vector<std::string> cols(237, "0");
    for (int i = 1; i <= 8; ++i) cols[static_cast<std::size_t>(i)].clear();
    for (int i = 86; i <= 97; ++i) cols[static_cast<std::size_t>(i)] = "254";
    for (const auto& [c, v] : set) cols[static_cast<std::size_t>(c)] = v;
    std::string line;
    for (std::size_t i = 0; i < cols.size(); ++i) line += (i ? "^" : "") + cols[i];
    return line;
}

std::vector<SpawnAppearance> appearances(const MockEqServer& server) {
    std::vector<SpawnAppearance> out;
    for (const auto& p : server.zone_log)
        if (rof2::from_wire(p.opcode) == rof2::Op::SpawnAppearance)
            if (auto a = decode_spawn_appearance(p)) out.push_back(*a);
    return out;
}

messages::SpellAction spell_on(std::uint32_t me, std::uint32_t spell) {
    messages::SpellAction a;
    a.target = static_cast<std::uint16_t>(me);
    a.source = static_cast<std::uint16_t>(me);
    a.type = 231;
    a.spell_id = spell;
    a.spell_level = 10;
    a.effect_flag = 4;
    return a;
}

} // namespace

TEST_CASE("A feign death spell landing on the player lies them down and stops auto attack") {
    auto table = std::make_shared<game::SpellTable>();
    REQUIRE(table->parse(spell_line({{0, "400"}, {1, "Imitation"}, {20, "100"}, {86, "74"}, {83, "1"}}) + "\n" +
                         spell_line({{0, "401"}, {1, "Plain"}, {20, "5"}, {86, "0"}, {83, "1"}})) == 2);
    Rig rig;
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    conn.set_spell_table_for_test(table);
    const std::uint32_t me = conn.world().player_id;
    REQUIRE(conn.player_status().stand_state == kStandStanding);
    conn.set_auto_attack(true);
    REQUIRE(conn.auto_attack());

    // A spell without SPA 74 leaves the player standing.
    conn.receive_zone_message_for_test({messages::GameOpcode::Action, spell_on(me, 401)});
    CHECK(conn.player_status().stand_state == kStandStanding);
    CHECK(conn.auto_attack());

    rig.server.zone_log.clear();
    conn.receive_zone_message_for_test({messages::GameOpcode::Action, spell_on(me, 400)});
    CHECK(conn.player_status().stand_state == kStandLying);
    CHECK(conn.world().spawns.at(me).stand_state == kStandLying);
    CHECK_FALSE(conn.auto_attack());
    REQUIRE(rig.until([&] { return !appearances(rig.server).empty(); }));
    const SpawnAppearance a = appearances(rig.server).back();
    CHECK(a.spawn_id == me);
    CHECK(a.type == kAppearanceAnimation);
    CHECK(a.parameter == kStandLying);

    // Landing again while lying sends nothing more (the same state).
    const std::size_t sent = appearances(rig.server).size();
    conn.receive_zone_message_for_test({messages::GameOpcode::Action, spell_on(me, 400)});
    rig.run_for(std::chrono::milliseconds(30));
    CHECK(appearances(rig.server).size() == sent);
}
