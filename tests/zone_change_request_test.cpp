// OP_RequestClientZoneChange goes to DoTeleportB (CEverQuest::HandleWorldMessage @0x4d13cd) when the app gives the
// connection a teleport handler: a #goto in the player's own zone moves the player instead of zoning.
#include <doctest/doctest.h>

#include "net/messages/zone.h"
#include "mq_test_world.h"

using namespace hailstone;

TEST_CASE("OP_RequestClientZoneChange hands DoTeleportB the zone dword, world x / y, z, heading, type and +4") {
    mq_test::World w("hailstone_zone_change_request_test");
    net::GameConnection& c = *w.conn;
    std::optional<game::TeleportBArgs> got;
    c.set_teleport_handler([&](const game::TeleportBArgs& a) { got = a; });

    net::RequestClientZoneChange request;
    request.zone_id = 1;
    request.instance_id = 2;
    request.unknown004 = 0xffffffffu;
    request.y = -186.0f;
    request.x = -371.0f;
    request.z = 4.0f;
    request.heading = 128.0f;
    request.type = 1;
    c.receive_zone_message_for_test({net::messages::GameOpcode::RequestClientZoneChange, request});

    REQUIRE(got);
    CHECK(got->zone == (1 | (2 << 16)));
    CHECK(got->x == doctest::Approx(-186.0f)); // world x is the server's y
    CHECK(got->y == doctest::Approx(-371.0f));
    CHECK(got->z == doctest::Approx(4.0f));
    CHECK(got->heading == doctest::Approx(128.0f));
    CHECK(got->reason == 1);
    CHECK(got->arg8 == -1);
}

TEST_CASE("The zone dword carries the player profile's instance: a summon inside an instance moves, not zones") {
    mq_test::World w("hailstone_zone_change_instance_test");
    net::GameConnection& c = *w.conn;
    // An incoming NewZone reports the zone ID and the zero instance.
    net::NewZone zone;
    zone.zone_id = 89;
    zone.zone_type = 0;
    c.receive_zone_message_for_test({net::messages::GameOpcode::NewZone, zone});
    REQUIRE(c.zone_id() == 89);
    CHECK(c.current_zone_dword() == 89u);
    // The profile says which instance (PlayerProfile zoneInstance).
    net::PlayerProfile pp;
    pp.zone_id = 89;
    pp.zone_instance = 968;
    c.set_profile_for_test(pp);
    CHECK(c.zone_instance() == 968);
    CHECK(c.current_zone_dword() == (89u | (968u << 16)));
    // A profile for another zone (left from before) does not count.
    pp.zone_id = 22;
    c.set_profile_for_test(pp);
    CHECK(c.current_zone_dword() == 89u);
}

TEST_CASE("OP_NewZone's suspend_buffs (+876) says whether the zone holds the suspendable buffs") {
    mq_test::World w("hailstone_zone_suspend_buffs_test");
    net::GameConnection& c = *w.conn;
    auto new_zone = [](std::uint32_t suspend) {
        net::NewZone zone;
        zone.zone_id = 202;
        zone.zone_type = 0;
        zone.suspend_buffs = suspend;
        return zone;
    };
    CHECK_FALSE(c.spell_state().zone_suspends_buffs);
    c.receive_zone_message_for_test({net::messages::GameOpcode::NewZone, new_zone(1)});
    CHECK(c.spell_state().zone_suspends_buffs);
    c.receive_zone_message_for_test({net::messages::GameOpcode::NewZone, new_zone(0)});
    CHECK_FALSE(c.spell_state().zone_suspends_buffs);
}

TEST_CASE("A new connection has none of the app's hooks, even where the freed one was allocated") {
    // The app gives each connection its line of sight, teleport and slash-command hooks once, when the connection
    // has none (client_app_player.cpp): a test on the connection's address skipped a new one put where the old was.
    // This checks only the condition the app tests; the installation in ClientApp::update_player is not unit-tested
    // (ClientApp needs a window).
    mq_test::World w("hailstone_zone_change_request_hooks_test");
    CHECK_FALSE(w.conn->has_line_of_sight());
    w.conn->set_line_of_sight([](const glm::vec3&, const glm::vec3&) { return true; });
    CHECK(w.conn->has_line_of_sight());
    w.conn.reset();
    w.conn = std::make_unique<net::GameConnection>(*w.game, net::ConnectionOptions{});
    CHECK_FALSE(w.conn->has_line_of_sight());
}

TEST_CASE("OP_NewZone: a short name that is a path is not used for the zone's files") {
    // It names the zone's archives, map layers and navmesh cache (core/zone_name.h).
    mq_test::World w("hailstone_new_zone_name_test");
    net::GameConnection& c = *w.conn;
    net::NewZone nz;
    nz.zone_long_name = "Nowhere";
    for (const char* bad : {"../../../EverQuest/spells_us", "/home/user/notes", "C:\\EQ\\spells_us", "nul"}) {
        nz.zone_short_name = bad;
        c.receive_zone_message_for_test({net::messages::GameOpcode::NewZone, nz});
        CHECK(c.world().zone_short_name.empty());
        CHECK(c.take_zone_to_load() == std::optional<std::string>(""));
    }
    nz.zone_short_name = "qeynos2";
    c.receive_zone_message_for_test({net::messages::GameOpcode::NewZone, nz});
    CHECK(c.world().zone_short_name == "qeynos2");
    CHECK(c.take_zone_to_load() == std::optional<std::string>("qeynos2"));
}
