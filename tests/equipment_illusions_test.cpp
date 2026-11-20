#include <doctest/doctest.h>
#include <algorithm>
#include "game/appearance.h"
#include "game/mounts.h"
#include "mock_server/harness.h"
#include "net/messages/message.h"
#include "net/messages/zone.h"
#include "net/protocol/rof2/packets/group_packets.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/packets/social_packets.h"
using namespace hailstone;
using namespace hailstone::test;

namespace {
net::messages::GameMessage illusion(std::uint32_t spawn, std::uint16_t race,
                                    std::uint32_t heritage = 0xffffffffu,
                                    std::uint32_t tattoo = 0xffffffffu,
                                    std::uint32_t details = 0xffffffffu) {
    net::Illusion value;
    value.spawn_id = spawn;
    value.race = race;
    value.gender = value.texture = value.helm = 0xff;
    value.face = 0xffffffffu;
    value.hair_style = value.hair_color = value.beard = value.beard_color = 0xff;
    value.drakkin_heritage = heritage;
    value.drakkin_tattoo = tattoo;
    value.drakkin_details = details;
    return {net::messages::GameOpcode::Illusion, value};
}
}

TEST_CASE("Appearance integration: spawn and wear records retain all five words through save") {
    Rig rig;
    REQUIRE(rig.to_zone());
    auto s = make_spawn("Gennro", rig.server.config.own_spawn_id, 0, 1, 0, 0, 3);
    s.texture_type = 0xff;
    for (std::size_t slot = 0; slot < s.equipment.size(); ++slot) {
        auto& e = s.equipment[slot];
        e.material = static_cast<std::uint32_t>(slot + 1);
        e.unknown1 = static_cast<std::uint32_t>(101 + slot);
        e.elite_material = static_cast<std::uint32_t>(201 + slot);
        e.hero_forge_model = static_cast<std::uint32_t>(301 + slot);
        e.material2 = static_cast<std::uint32_t>(401 + slot);
        s.equipment_tint[slot] = 0xff123456u;
    }
    rig.server.add_spawn(s);
    REQUIRE(rig.until([&] { return rig.conn.world().player()->appearance.material[0] == 1; }));
    const auto& a = rig.conn.world().player()->appearance;
    for (std::size_t slot = 0; slot < s.equipment.size(); ++slot) {
        CHECK(a.material[slot] == s.equipment[slot].material);
        CHECK(a.material_variation[slot] == s.equipment[slot].unknown1);
        CHECK(a.elite_material[slot] == s.equipment[slot].elite_material);
        CHECK(a.hero_forge[slot] == s.equipment[slot].hero_forge_model);
        CHECK(a.new_armor_type[slot] == s.equipment[slot].material2);
    }
    CHECK(a.new_armor_bits == (0x08 | 0x02 | 0x01)); // a player, self (group bit) and the local player
    net::WearChange wear;
    wear.spawn_id = static_cast<std::uint16_t>(s.spawn_id);
    wear.slot = game::kSlotHands;
    wear.armor = {3, 71, 72, 73, 74};
    wear.tint = 0xffabcdefu;
    rig.server.zone.send_reliable(net::encode(wear));
    REQUIRE(rig.until([&] { return a.new_armor_type[wear.slot] == 74; }));
    CHECK(a.material_variation[wear.slot] == 71);
    CHECK(a.elite_material[wear.slot] == 72);
    CHECK(a.tint[wear.slot] == 0xffabcdefu);
    rig.conn.camp();
    REQUIRE(rig.until([&] { return MockEqServer::count(rig.server.zone_log, net::rof2::Op::Save) != 0; }));
    auto saved = std::find_if(rig.server.zone_log.rbegin(), rig.server.zone_log.rend(),
        [](const net::AppPacket& p) { return net::rof2::from_wire(p.opcode) == net::rof2::Op::Save; });
    REQUIRE(saved != rig.server.zone_log.rend());
    auto decoded = net::decode_save(*saved);
    REQUIRE(decoded);
    CHECK(decoded->armor[wear.slot].material == 3);
    CHECK(decoded->armor[wear.slot].unknown1 == 71);
    CHECK(decoded->armor[wear.slot].elite_material == 72);
    CHECK(decoded->armor[wear.slot].hero_forge_model == 73);
    CHECK(decoded->armor[wear.slot].material2 == 74);
    CHECK(decoded->armor[0].unknown1 == 101); // untouched slots survive too
}

TEST_CASE("Appearance filter: ApplyNewArmorFilter's bits by kind and membership") {
    using game::new_armor_bits;
    CHECK(new_armor_bits(1, false, false, false) == 0x10);
    CHECK(new_armor_bits(1, true, true, true) == 0x10); // membership counts for players only
    CHECK(new_armor_bits(0, false, false, false) == 0x08);
    CHECK(new_armor_bits(0, true, false, false) == 0x0b);
    CHECK(new_armor_bits(0, false, true, false) == 0x0a);
    CHECK(new_armor_bits(0, false, false, true) == 0x0c);
    for (std::uint8_t corpse : {std::uint8_t{2}, std::uint8_t{3}}) CHECK(new_armor_bits(corpse, true, true, true) == 0);
    game::Appearance a;
    CHECK(a.new_armor_bits == game::kNewArmorUnknownKind); // either category bit shows an unknown kind
    CHECK_FALSE(game::appearance_new_armor_filtered(a, 8));
    CHECK_FALSE(game::appearance_new_armor_filtered(a, 16));
    CHECK(game::appearance_new_armor_filtered(a, 0));
    a.new_armor_bits = new_armor_bits(0, false, true, false);
    CHECK(game::appearance_new_armor_filtered(a, 0x10 | 0x04 | 0x01));
    CHECK_FALSE(game::appearance_new_armor_filtered(a, 2));
    a.new_armor_bits = 0; // a corpse: hidden whatever the filter
    CHECK(game::appearance_new_armor_filtered(a, 0x1f));
    // Resolved for the default filter (24) a group member and a stranger look the same; for filter 2 they do not.
    game::Appearance stranger, member;
    stranger.new_armor_bits = new_armor_bits(0, false, false, false);
    member.new_armor_bits = new_armor_bits(0, false, true, false);
    auto resolved = [](game::Appearance x, std::uint32_t filter) {
        game::resolve_new_armor_filter(x, filter);
        return x;
    };
    CHECK(resolved(stranger, 24) == resolved(member, 24));
    CHECK_FALSE(resolved(stranger, 2) == resolved(member, 2));
    CHECK(game::appearance_new_armor_filtered(resolved(stranger, 2), 2));
    CHECK_FALSE(game::appearance_new_armor_filtered(resolved(member, 2), 2));
}

TEST_CASE("Appearance filter: spawn kinds, corpses and group/raid membership") {
    Rig rig;
    REQUIRE(rig.to_zone());
    auto npc = make_spawn("Armored_NPC", 301, 1, 1, 0, 0, 3);
    rig.server.add_spawn(npc);
    REQUIRE(rig.until([&] { return rig.conn.world().spawns.contains(301); }));
    CHECK(rig.conn.world().spawns.at(301).appearance.new_armor_bits == 0x10);
    // EQEmu sends NPC 2 for every corpse, a player's too; RoF2 hides hero's forge on any Type above 1.
    auto corpse = make_spawn("Gennro's_corpse", 303, 2, 1, 0, 0, 3);
    rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::ZoneEntry, corpse});
    CHECK(rig.conn.world().spawns.at(303).appearance.new_armor_bits == 0);
    auto member = make_spawn("Armored_Visitor", 302, 0, 1, 0, 0, 3);
    rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::ZoneEntry, member});
    auto& bits = rig.conn.world_for_test().spawns.at(302).appearance.new_armor_bits;
    const auto exempt = [&] { return (bits & 0x06) != 0; };
    rig.step();
    CHECK(bits == 0x08);
    // Nothing changed: the bits are not made again every frame.
    bits = 0x80;
    rig.step();
    CHECK(bits == 0x80);
    bits = 0x08;
    net::GroupUpdateB group;
    group.group_id = 7;
    group.leader = rig.conn.world().player()->name;
    net::GroupUpdateMember entry;
    entry.slot = 0;
    entry.name = group.leader;
    group.members.push_back(entry);
    entry.slot = 1;
    entry.name = member.name;
    group.members.push_back(entry);
    rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::GroupUpdateB, group});
    rig.step();
    CHECK(exempt());
    CHECK(bits == 0x0a);
    group.members.pop_back();
    rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::GroupUpdateB, group});
    rig.step();
    CHECK_FALSE(exempt());
    net::RaidUpdate raid;
    raid.general.action = net::kRaidAddMember;
    raid.general.player_name = member.name;
    raid.general.leader_name = group.leader;
    rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::RaidUpdate, raid});
    rig.step();
    CHECK(exempt());
    raid.general.action = net::kRaidRemoveMember;
    rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::RaidUpdate, raid});
    rig.step();
    CHECK_FALSE(exempt());
}

TEST_CASE("Appearance integration: illusions change, retain and reset Drakkin features independently") {
    Rig rig;
    REQUIRE(rig.to_zone());
    auto s = make_spawn("Drakkin_Visitor", 301, 0, 522, 0, 0, 3);
    s.drakkin_heritage = 1;
    s.drakkin_tattoo = 2;
    s.drakkin_details = 3;
    rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::ZoneEntry, s});
    auto& spawn = rig.conn.world_for_test().spawns.at(s.spawn_id);
    auto& a = spawn.appearance;
    REQUIRE(a.drakkin_heritage == 1);
    REQUIRE(a.drakkin_tattoo == 2);
    REQUIRE(a.drakkin_details == 3);
    const auto original = a;
    rig.conn.receive_zone_message_for_test(illusion(s.spawn_id, 522, 4, 5, 6));
    CHECK(a.drakkin_heritage == 4);
    CHECK(a.drakkin_tattoo == 5);
    CHECK(a.drakkin_details == 6);
    CHECK_FALSE(a == original); // the renderer's appearance comparison sees the change
    CHECK_FALSE(spawn.actor_made);

    rig.conn.receive_zone_message_for_test(illusion(s.spawn_id, 522));
    CHECK(a.drakkin_heritage == 4);
    CHECK(a.drakkin_tattoo == 5);
    CHECK(a.drakkin_details == 6);
    rig.conn.receive_zone_message_for_test(illusion(s.spawn_id, 522, 0xffffffffu, 0, 0xffffffffu));
    CHECK(a.drakkin_heritage == 4);
    CHECK(a.drakkin_tattoo == 0);
    CHECK(a.drakkin_details == 6);
    rig.conn.receive_zone_message_for_test(illusion(s.spawn_id, 522, 0, 0xffffffffu, 0));
    CHECK(a.drakkin_heritage == 0);
    CHECK(a.drakkin_tattoo == 0);
    CHECK(a.drakkin_details == 0);
}

TEST_CASE("Mounted form changes: unseat before rebuild and re-seat against the new model") {
    Rig rig;
    REQUIRE(rig.to_zone());
    auto& world = rig.conn.world_for_test();
    auto& rider = world.spawns.at(world.player_id);
    auto horse = make_spawn("Gennro`s_Mount00", 9000, 1, 216, 0, 0, 3);
    rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::ZoneEntry, horse});
    const auto actor = [](const game::Spawn&) -> std::optional<game::WorldState::ActorModel> {
        return game::WorldState::ActorModel{true, false};
    };
    rig.conn.smooth(0.0f, {}, actor);
    REQUIRE(rider.mount_id == 9000);
    REQUIRE(world.controlled_id == 9000);
    rig.conn.receive_zone_message_for_test(illusion(rider.id, 60));
    CHECK(rider.mount_id == 0);
    CHECK(world.spawns.at(9000).rider_id == 0);
    CHECK(world.controlled_id == 0);
    CHECK(rider.form_mount_id == 9000);
    CHECK_FALSE(rider.actor_made);
    SUBCASE("the replacement model can ride") {
        rig.conn.smooth(0.0f, {}, actor);
        CHECK(rider.mount_id == 9000);
        CHECK(world.spawns.at(9000).rider_id == rider.id);
        CHECK(world.controlled_id == 9000);
        CHECK(rider.form_mount_id == 0);
    }
    SUBCASE("a departing mount cannot reattach through a reused id") {
        rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::DeleteSpawn, net::SpawnDespawn{9000, true}});
        horse.name = "Another_Rider`s_Mount00";
        rig.conn.receive_zone_message_for_test({net::messages::GameOpcode::ZoneEntry, horse});
        rig.conn.smooth(0.0f, {}, actor);
        CHECK(rider.mount_id == 0);
        CHECK(world.controlled_id == 0);
        CHECK(rider.form_mount_id == 0);
    }
}
