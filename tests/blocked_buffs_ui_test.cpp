// The modern buff trays' "Add to Blocked Buffs" (ui/modern/lib/bufftray.lua): the Blocked Buffs window's own edit box
// and Add over the hs API (app/lua_ui_dialogs.cpp BlockedBuffWindow), with a buff's spell id, against the mock server.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <string>

#include <imgui.h>
#include <lua.hpp>

#include "app/lua_ui.h"
#include "mock_server/harness.h"
#include "mq_test_world.h"
#include "net/packet_io.h"
#include "net/spell_packets.h"
#include "scratch_dir.h"

using namespace hailstone;
using namespace hailstone::test;

TEST_CASE("Blocked buffs: accepted blocks remove the active buff and persist across sessions") {
    ScratchDir servers("hailstone_blocked_buffs_servers");
    auto options = quick_options();
    options.servers_dir = servers.path();
    ImGuiContext* ctx = ImGui::CreateContext();
    {
        // Holy Armor (11), a beneficial spell, in the game folder's spell table (read when the connection is made).
        EmptyGameDir dir;
        std::ofstream(dir.path() / "spells_us.txt")
            << mq_test::spell_line({{0, "11"}, {1, "Holy Armor"}, {13, "2000"}, {17, "270"}, {83, "1"}, {98, "5"}})
            << "\n";
        GameDir game(dir.path());
        MockEqServer server(MockConfig::standard());
        net::GameConnection conn(game, options);
        auto until = [&](const std::function<bool()>& done) {
            return pump([&] { conn.update(); server.poll(); }, done);
        };
        conn.start(server.ticket());
        REQUIRE(until([&] { return conn.state() == net::ConnectionState::CharacterSelect; }));
        conn.enter_world("Gennro");
        REQUIRE(until([&] { return conn.state() == net::ConnectionState::InZone && conn.player_status().max_hp != 0; }));
        REQUIRE(conn.spell_table().get(11));
        net::BuffPacket active;
        active.entity_id = server.config.own_spawn_id;
        active.buff.spell_id = 11;
        active.buff.duration = 10;
        active.slot = 4;
        server.zone.send_reliable(net::encode(active));
        REQUIRE(until([&] { return conn.spell_state().buffs[4].spell == 11; }));
        app::UiHost host;
        host.in_game = host.ui_ready = true;
        host.conn = &conn;
        app::UiApi api(host);
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);
        lua_newtable(L);
        api.register_functions(L);
        lua_setglobal(L, "hs");
        REQUIRE(luaL_dostring(L, "hs.SetWindowText('BlockedBuffWnd', 'BW_EditSpellToBlock', '11') "
                                 "return hs.ClickChild('BlockedBuffWnd', 'BW_AddSpellToBlock', 'left')") == LUA_OK);
        CHECK(lua_toboolean(L, -1));
        lua_close(L);
        auto sent = [&] {
            return std::find_if(server.zone_log.begin(), server.zone_log.end(),
                                [](const net::AppPacket& p) { return net::is_op(p, net::Op::BlockedBuffs); });
        };
        REQUIRE(until([&] { return sent() != server.zone_log.end(); }));
        auto b = net::decode_blocked_buffs(*sent());
        REQUIRE(b);
        CHECK_FALSE(b->pet);
        REQUIRE_FALSE(b->spells.empty());
        CHECK(b->spells[0] == 11);
        CHECK(MockEqServer::count(server.zone_log, net::Op::BuffRemoveRequest) == 0);

        server.zone.send_reliable(net::encode(*b));
        REQUIRE(until([&] { return MockEqServer::count(server.zone_log, net::Op::BuffRemoveRequest) == 1; }));
        const auto removed = std::find_if(server.zone_log.begin(), server.zone_log.end(),
            [](const net::AppPacket& p) { return net::is_op(p, net::Op::BuffRemoveRequest); });
        REQUIRE(removed != server.zone_log.end());
        CHECK(removed->data == net::encode_buff_remove_request(4, active.entity_id).data);
        active.fade = 1;
        server.zone.send_reliable(net::encode(active));
        REQUIRE(until([&] { return conn.spell_state().buffs[4].empty(); }));

        net::BlockedBuffs pet;
        pet.pet = true;
        pet.spells = {11};
        server.zone.send_reliable(net::encode(pet));
        REQUIRE(until([&] { return conn.blocked_buffs(true) == pet.spells; }));

        MockEqServer next(MockConfig::standard());
        net::GameConnection reloaded(game, options);
        auto until_reloaded = [&](const std::function<bool()>& done) {
            return pump([&] { reloaded.update(); next.poll(); }, done);
        };
        reloaded.start(next.ticket());
        REQUIRE(until_reloaded([&] { return reloaded.state() == net::ConnectionState::CharacterSelect; }));
        reloaded.enter_world("Gennro");
        REQUIRE(until_reloaded([&] { return MockEqServer::count(next.zone_log, net::Op::BlockedBuffs) == 2; }));
        CHECK(reloaded.blocked_buffs(false) == b->spells);
        CHECK(reloaded.blocked_buffs(true) == pet.spells);
        for (const auto& packet : next.zone_log) {
            if (!net::is_op(packet, net::Op::BlockedBuffs)) continue;
            const auto restored = net::decode_blocked_buffs(packet);
            REQUIRE(restored);
            CHECK(restored->initialise);
            CHECK(restored->spells == b->spells);
        }

        next.zone.send_reliable(net::encode(pet, true));
        next.zone.send_reliable(net::encode_clear_blocked_buffs(false));
        REQUIRE(until_reloaded([&] {
            return reloaded.blocked_buffs(false).empty() && reloaded.blocked_buffs(true).empty();
        }));
        net::GameConnection cleared(game, options);
        MockEqServer last(MockConfig::standard());
        auto until_cleared = [&](const std::function<bool()>& done) {
            return pump([&] { cleared.update(); last.poll(); }, done);
        };
        cleared.start(last.ticket());
        REQUIRE(until_cleared([&] { return cleared.state() == net::ConnectionState::CharacterSelect; }));
        cleared.enter_world("Gennro");
        CHECK(cleared.blocked_buffs(false).empty());
        CHECK(cleared.blocked_buffs(true).empty());
    }
    ImGui::DestroyContext(ctx);
}
