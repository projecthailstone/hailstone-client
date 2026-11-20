#include <doctest/doctest.h>

#include "game/mechanics/character.h"
#include "mq_test_world.h"
#include "net/messages/tribute.h"
#include "net/messages/message.h"
#include "net/messages/social.h"
#include "net/protocol/rof2/packets/tribute_packets.h"
#include "net/protocol/rof2/packets/item_packets.h" // Corrupt inventory wire check.

using namespace hailstone;

namespace {

net::ItemSlot tribute_slot(bool guild, std::int16_t slot) {
    return {guild ? net::kInvGuildTribute : net::kInvTribute, slot, -1, -1};
}

void receive_benefit(net::GameConnection& connection, bool guild, std::int16_t slot, std::int8_t strength) {
    net::ItemPacket packet;
    packet.type = guild ? 109u : 108u; // RoF2 ItemPacketGuildTribute / ItemPacketTributeItem
    packet.item.id = static_cast<std::uint32_t>(1000 + (guild ? 100 : 0) + slot);
    packet.item.name = "Received tribute benefit";
    packet.item.slot = tribute_slot(guild, slot);
    packet.item.astr = strength;
    packet.item.races = packet.item.classes = 0xffff;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::ItemPacket, packet});
}

std::int32_t strength(const net::GameConnection& connection) {
    std::int32_t value = 0;
    net::GameConnectionTestAccess::with_character(connection, [&](game::mechanics::Character& character) {
        character.process_all_stats();
        value = character.stats().stats.str;
    });
    return value;
}

net::TributeSettings personal_settings() {
    net::TributeSettings settings;
    settings.active = true;
    settings.tributes[0] = 11;
    settings.tributes[1] = 12;
    return settings;
}

net::GuildTributeActive guild_settings() {
    net::GuildTributeActive settings;
    settings.enabled = true;
    settings.tributes = {21, 22};
    return settings;
}

void join_guild(net::GameConnection& connection, std::uint32_t guild_id) {
    connection.receive_zone_message_for_test({net::messages::GameOpcode::SpawnAppearance, net::SpawnAppearance{1, 22, guild_id}});
}

} // namespace

TEST_CASE("tribute bonuses: personal state retires only changed items and disabled benefits without delete packets") {
    mq_test::World world("hailstone_personal_tribute_bonuses");
    auto& connection = *world.conn;
    const auto baseline = strength(connection);
    auto settings = personal_settings();

    // A first settings snapshot may follow the corresponding received items.
    receive_benefit(connection, false, 0, 3);
    receive_benefit(connection, false, 1, 5);
    connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, settings});
    CHECK(strength(connection) == baseline + 8);
    connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, settings}); // repeated state is not a removal
    CHECK(strength(connection) == baseline + 8);
    connection.tribute_toggle(false); // a request alone cannot deactivate the server's benefits
    CHECK(strength(connection) == baseline + 8);

    auto disabled = settings;
    disabled.active = false;
    auto malformed = net::encode(disabled);
    malformed.data.pop_back();
    CHECK_FALSE(net::decode_tribute_settings(malformed));
    CHECK(connection.tribute().settings.active);
    CHECK(strength(connection) == baseline + 8);

    settings.tiers[0] = 1;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, settings});
    CHECK_FALSE(connection.inventory().at(tribute_slot(false, 0)));
    REQUIRE(connection.inventory().at(tribute_slot(false, 1)));
    CHECK(strength(connection) == baseline + 5);
    receive_benefit(connection, false, 0, 7);
    CHECK(strength(connection) == baseline + 12);

    settings.tributes[1] = 13;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, settings});
    REQUIRE(connection.inventory().at(tribute_slot(false, 0)));
    CHECK_FALSE(connection.inventory().at(tribute_slot(false, 1)));
    receive_benefit(connection, false, 1, 9);
    settings.tributes[1] = net::kTributeNone;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, settings});
    CHECK(strength(connection) == baseline + 7);

    settings.active = false;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, settings});
    CHECK(strength(connection) == baseline);
    connection.tribute_toggle(true); // activating locally grants no item or bonus
    CHECK(strength(connection) == baseline);
}

TEST_CASE("tribute bonuses: guild opt-out and changed benefits retire the correct received items") {
    mq_test::World world("hailstone_guild_tribute_bonuses");
    auto& connection = *world.conn;
    join_guild(connection, 7);
    net::GameConnectionTestAccess::world(connection).spawns.at(1).gm = true;
    REQUIRE(connection.can_change_guild_tribute());
    const auto baseline = strength(connection);
    auto settings = guild_settings();
    receive_benefit(connection, false, 0, 2);
    receive_benefit(connection, true, 0, 3);
    receive_benefit(connection, true, 1, 5);
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, settings});
    CHECK(strength(connection) == baseline + 10);
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildOptInOut, net::GuildTributeOpt{"Tester", true}});
    // Donation status refreshes repeat the enabled selections without resending their items.
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildTributeToggleReply, settings});
    CHECK(strength(connection) == baseline + 10);
    connection.guild_tribute_opt(false);
    connection.guild_tribute_toggle(false);
    CHECK(strength(connection) == baseline + 10);

    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildOptInOut, net::GuildTributeOpt{"SomebodyElse", false}});
    CHECK(strength(connection) == baseline + 10);
    auto malformed_opt = net::encode(net::GuildTributeOpt{"Tester", false});
    malformed_opt.data.pop_back();
    CHECK_FALSE(net::decode_guild_tribute_opt(malformed_opt));
    CHECK(strength(connection) == baseline + 10);
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildOptInOut, net::GuildTributeOpt{"tEsTeR", false}});
    CHECK(connection.guild_tribute().opted_in == false);
    CHECK(strength(connection) == baseline + 2); // personal benefit is independent
    connection.guild_tribute_opt(true);
    CHECK(strength(connection) == baseline + 2);

    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildOptInOut, net::GuildTributeOpt{"Tester", true}});
    receive_benefit(connection, true, 0, 3);
    receive_benefit(connection, true, 1, 5);
    settings.tiers[1] = 1;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, settings});
    REQUIRE(connection.inventory().at(tribute_slot(true, 0)));
    CHECK_FALSE(connection.inventory().at(tribute_slot(true, 1)));
    CHECK(strength(connection) == baseline + 5);
    receive_benefit(connection, true, 1, 7);

    auto disabled = settings;
    disabled.enabled = false;
    auto malformed_state = net::encode(disabled);
    malformed_state.data.pop_back();
    CHECK_FALSE(net::decode_guild_tribute_active(malformed_state));
    CHECK(connection.guild_tribute().active.enabled);
    CHECK(strength(connection) == baseline + 12);
    settings.tributes[0] = net::kTributeNone;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, settings});
    CHECK_FALSE(connection.inventory().at(tribute_slot(true, 0)));
    REQUIRE(connection.inventory().at(tribute_slot(true, 1)));
    CHECK(strength(connection) == baseline + 9);
    settings.enabled = false;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, settings});
    CHECK(strength(connection) == baseline + 2);
}

TEST_CASE("tribute bonuses: membership and zone or session changes retire old benefits while retaining catalogs") {
    for (bool disconnect : {false, true}) {
        CAPTURE(disconnect);
        mq_test::World world("hailstone_tribute_bonus_reset");
        auto& connection = *world.conn;
        join_guild(connection, 7);
        auto personal = personal_settings();
        auto guild = guild_settings();
        net::TributeAbility ability;
        ability.id = 11;
        ability.name = "Catalog ability";
        connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeInfo, ability});
        ability.id = 21;
        connection.receive_zone_message_for_test({net::messages::GameOpcode::SendGuildTributes, ability});
        connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, personal});
        connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, guild});
        connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildOptInOut, net::GuildTributeOpt{"Tester", true}});
        receive_benefit(connection, false, 0, 3);
        receive_benefit(connection, true, 0, 5);

        // Another player's guild change is unrelated. Our own membership loss clears only guild benefits.
        connection.receive_zone_message_for_test({net::messages::GameOpcode::SpawnAppearance, net::SpawnAppearance{12, 22, 9}});
        REQUIRE(connection.inventory().at(tribute_slot(true, 0)));
        join_guild(connection, 0xffffffff);
        CHECK_FALSE(connection.inventory().at(tribute_slot(true, 0)));
        REQUIRE(connection.inventory().at(tribute_slot(false, 0)));
        CHECK_FALSE(connection.guild_tribute().active.enabled);
        CHECK_FALSE(connection.guild_tribute().active_known);
        CHECK_FALSE(connection.guild_tribute().opted_in.has_value());
        CHECK(connection.guild_tribute().abilities.contains(21));
        join_guild(connection, 8);
        connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, guild});
        receive_benefit(connection, true, 0, 5);

        const auto profile = *connection.profile();
        if (disconnect) connection.disconnect();
        else net::GameConnectionTestAccess::retire_zone_observations(connection);
        CHECK_FALSE(connection.inventory().at(tribute_slot(false, 0)));
        CHECK_FALSE(connection.inventory().at(tribute_slot(true, 0)));
        CHECK_FALSE(connection.tribute().settings_known);
        CHECK_FALSE(connection.guild_tribute().active_known);
        CHECK(connection.tribute().abilities.contains(11));
        CHECK(connection.guild_tribute().abilities.contains(21));

        // The next zone's first enabled metadata may follow its newly received item packets.
        net::GameConnectionTestAccess::profile(connection) = profile;
        net::GameConnectionTestAccess::state(connection) = net::ConnectionState::InZone;
        receive_benefit(connection, false, 0, 7);
        receive_benefit(connection, true, 0, 9);
        personal.tributes[0] = 14;
        guild.tributes[0] = 24;
        connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, personal});
        connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, guild});
        REQUIRE(connection.inventory().at(tribute_slot(false, 0)));
        REQUIRE(connection.inventory().at(tribute_slot(true, 0)));
    }
}

TEST_CASE("tribute bonuses: guild metadata received before our join appearance survives only for the new guild") {
    for (std::uint32_t metadata_guild : {0u, 8u, 7u, 9u}) {
        CAPTURE(metadata_guild);
        mq_test::World world("hailstone_tribute_guild_join_order");
        auto& connection = *world.conn;
        join_guild(connection, 7);
        connection.receive_zone_message_for_test({net::messages::GameOpcode::SpawnAppearance, net::SpawnAppearance{1, 23, 1}});
        auto previous = guild_settings();
        previous.guild_id = 7;
        connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, previous});
        connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildOptInOut, net::GuildTributeOpt{"Tester", true}});
        receive_benefit(connection, true, 0, 3);
        receive_benefit(connection, true, 1, 5);
        REQUIRE(connection.inventory().at(tribute_slot(true, 0)));
        REQUIRE(connection.inventory().at(tribute_slot(true, 1)));

        // Source sends tagged permission rows, then an active tribute packet with its first DWORD left zero,
        // before our GuildID/Rank appearances (zone/guild.cpp SendGuildMemberAdd). Future permissions cannot
        // authorize changes to our current guild while this sequence is still in flight.
        if (metadata_guild == 0) {
            CHECK_FALSE(connection.can_change_guild_tribute());
            connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildUpdate, net::GuildUpdate{5, 8, 1, 23, true, {}}});
            CHECK_FALSE(connection.can_change_guild_tribute());
        }
        // Even if the two guilds choose identical benefit ids/tiers, the old guild's received items must retire.
        auto incoming = previous;
        incoming.guild_id = metadata_guild;
        incoming.favor = 9876;
        incoming.timer_ms = 123456;
        connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, incoming});
        if (metadata_guild == 0) CHECK_FALSE(connection.can_change_guild_tribute());
        join_guild(connection, 8);
        connection.receive_zone_message_for_test({net::messages::GameOpcode::SpawnAppearance, net::SpawnAppearance{1, 23, 1}});
        CHECK(connection.guild().guild_id == 8);
        CHECK_FALSE(connection.inventory().at(tribute_slot(true, 0)));
        CHECK_FALSE(connection.inventory().at(tribute_slot(true, 1)));
        CHECK_FALSE(connection.guild_tribute().opted_in.has_value());
        const auto& active = connection.guild_tribute().active;
        if (metadata_guild == 0 || metadata_guild == 8) {
            REQUIRE(connection.guild_tribute().active_known);
            if (metadata_guild != 0) CHECK(active.guild_id == metadata_guild);
            CHECK(active.enabled);
            CHECK(active.favor == 9876);
            CHECK(active.timer_ms == 123456);
            CHECK(active.tributes == incoming.tributes);
            CHECK(active.tiers == incoming.tiers);
            if (metadata_guild == 0) CHECK(connection.can_change_guild_tribute());
        } else {
            CHECK_FALSE(connection.guild_tribute().active_known);
            CHECK_FALSE(active.enabled);
            CHECK(active.favor == 0);
            CHECK(active.timer_ms == 0);
            CHECK(active.tributes[0] == net::kTributeNone);
            CHECK(active.tributes[1] == net::kTributeNone);
        }
    }
}

TEST_CASE("tribute bonuses: the later bulk inventory snapshot preserves received benefits and honors replacements") {
    mq_test::World world("hailstone_tribute_bulk_inventory_order");
    auto& connection = *world.conn;
    join_guild(connection, 7);
    const auto baseline = strength(connection);
    auto personal = personal_settings();
    personal.tributes = {11, 12, 13, 14, 15};
    auto guild = guild_settings();
    connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, personal});
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, guild});
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildOptInOut, net::GuildTributeOpt{"Tester", true}});
    for (std::int16_t slot = 0; slot < 5; ++slot)
        receive_benefit(connection, false, slot, static_cast<std::int8_t>(slot + 1));
    receive_benefit(connection, true, 0, 5);
    receive_benefit(connection, true, 1, 7);
    CHECK(strength(connection) == baseline + 27);

    // Source sends DoTributeUpdate's items before BulkSendInventoryItems, whose list excludes tribute slots.
    // Only the seven supported bonus slots carry across; the incoming carried inventory still replaces the old.
    receive_benefit(connection, false, 5, 100);
    receive_benefit(connection, true, 2, 100);
    net::Item carried;
    carried.id = 2001;
    carried.name = "New carried inventory";
    carried.slot = net::ItemSlot::possessions(25);
    connection.receive_zone_message_for_test({net::messages::GameOpcode::CharInventory, std::vector<net::Item>{carried}});
    CHECK_FALSE(connection.inventory().at(net::ItemSlot::possessions(23)));
    CHECK_FALSE(connection.inventory().at(net::ItemSlot::possessions(24)));
    REQUIRE(connection.inventory().at(carried.slot));
    CHECK(connection.inventory().at(carried.slot)->id == carried.id);
    CHECK_FALSE(connection.inventory().at(tribute_slot(false, 5)));
    CHECK_FALSE(connection.inventory().at(tribute_slot(true, 2)));
    for (std::int16_t slot = 0; slot < 5; ++slot)
        REQUIRE(connection.inventory().at(tribute_slot(false, slot)));
    REQUIRE(connection.inventory().at(tribute_slot(true, 0)));
    REQUIRE(connection.inventory().at(tribute_slot(true, 1)));
    CHECK(strength(connection) == baseline + 27);

    net::Item personal_replacement = *connection.inventory().at(tribute_slot(false, 0));
    personal_replacement.id = 3001;
    personal_replacement.astr = 11;
    net::Item guild_replacement = *connection.inventory().at(tribute_slot(true, 1));
    guild_replacement.id = 3002;
    guild_replacement.astr = 13;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::CharInventory, std::vector<net::Item>{carried, personal_replacement, guild_replacement}});
    REQUIRE(connection.inventory().at(personal_replacement.slot));
    REQUIRE(connection.inventory().at(guild_replacement.slot));
    CHECK(connection.inventory().at(personal_replacement.slot)->id == 3001);
    CHECK(connection.inventory().at(guild_replacement.slot)->id == 3002);
    CHECK(strength(connection) == baseline + 43); // personal 11+2+3+4+5 and guild 5+13

    auto malformed = net::encode_char_inventory({carried});
    malformed.data.pop_back();
    CHECK_FALSE(net::decode_char_inventory(malformed));
    CHECK(strength(connection) == baseline + 43);
    connection.receive_zone_message_for_test({net::messages::GameOpcode::CharInventory, std::vector<net::Item>{}});
    CHECK(strength(connection) == baseline + 43);
    net::Item banked;
    banked.id = 2002;
    banked.name = "Received bank item";
    banked.slot = {net::kInvBank, 2, -1, -1};
    connection.receive_zone_message_for_test({net::messages::GameOpcode::CharInventory, std::vector<net::Item>{banked}});
    REQUIRE(connection.inventory().at(banked.slot));
    CHECK(strength(connection) == baseline + 43);

    personal.active = false;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::TributeUpdate, personal});
    CHECK(strength(connection) == baseline + 18);
    connection.receive_zone_message_for_test({net::messages::GameOpcode::CharInventory, std::vector<net::Item>{carried}});
    for (std::int16_t slot = 0; slot < 5; ++slot)
        CHECK_FALSE(connection.inventory().at(tribute_slot(false, slot)));
    CHECK(strength(connection) == baseline + 18);
    guild.enabled = false;
    connection.receive_zone_message_for_test({net::messages::GameOpcode::GuildSendActiveTributes, guild});
    CHECK(strength(connection) == baseline);
    connection.receive_zone_message_for_test({net::messages::GameOpcode::CharInventory, std::vector<net::Item>{}});
    CHECK_FALSE(connection.inventory().at(tribute_slot(true, 0)));
    CHECK_FALSE(connection.inventory().at(tribute_slot(true, 1)));
    CHECK(strength(connection) == baseline);
}
