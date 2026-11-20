// A right click on a trader (a player, or an NPC a server runs as one) opens its shop (CEverQuest::RightClickedOnPlayer @0x536d80, a trader:
// OP_TraderShop 28; EQEmu's answer, then its ItemPacketMerchant items), against the mock server.
#include <doctest/doctest.h>

#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/market_packets.h"

using namespace hailstone;
using namespace hailstone::test;

namespace {

constexpr std::uint32_t kTrader = 5002;

game::Spawn trader_beside(net::GameConnection& conn, bool trader) {
    auto& w = conn.world_for_test();
    REQUIRE(w.player());
    game::Spawn t = *w.player();
    t.id = kTrader;
    t.name = "Seller00";
    t.is_npc = false;
    t.is_corpse = false;
    t.trader = trader;
    return t;
}

} // namespace

TEST_CASE("a right click on a trader asks for its shop, and EQEmu's approval opens it") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    conn.world_for_test().spawns[kTrader] = trader_beside(conn, true);

    std::size_t before = rig.server.zone_log.size();
    conn.right_click_player(kTrader);
    CHECK_FALSE(conn.merchant());
    REQUIRE(rig.until([&] {
        for (std::size_t i = before; i < rig.server.zone_log.size(); ++i)
            if (rig.server.zone_log[i].opcode == net::rof2::to_wire(net::rof2::Op::TraderShop)) return true;
        return false;
    }));

    // EQEmu's RoF2 answer: {28, trader, approval} (12 bytes, Approval in the third word).
    conn.receive_zone_message_for_test({net::messages::GameOpcode::TraderShop, net::TraderShopMessage{.action = net::kTraderClick, .click = net::TraderClick{net::kTraderClick, kTrader, 1}}});
    REQUIRE(conn.merchant());
    CHECK(conn.merchant()->open);
    CHECK(conn.merchant()->trader);
    CHECK(conn.merchant()->npc_id == kTrader);
}

TEST_CASE("a player who becomes a trader after the spawn arrived is shopped at too") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    conn.world_for_test().spawns[kTrader] = trader_beside(conn, false);
    // EQEmu's OP_BecomeTrader {entity id, action 1 TraderOn}.
    conn.receive_zone_message_for_test({net::messages::GameOpcode::BecomeTrader, net::BecomeTrader{kTrader, 1}});
    REQUIRE(conn.world().spawns.at(kTrader).trader);
    conn.right_click_player(kTrader);
    conn.receive_zone_message_for_test({net::messages::GameOpcode::TraderShop, net::TraderShopMessage{.action = net::kTraderClick, .click = net::TraderClick{net::kTraderClick, kTrader, 1}}});
    REQUIRE(conn.merchant());
    CHECK(conn.merchant()->trader);
}

TEST_CASE("an NPC in trader mode (a server's simulated trader) opens its shop on a right click too") {
    Rig rig(MockConfig::standard());
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    game::Spawn t = trader_beside(conn, true);
    t.is_npc = true;
    t.class_id = 1; // not a service class: RightClickedOnPlayer reaches the trader test
    conn.world_for_test().spawns[kTrader] = t;
    conn.right_click_player(kTrader);
    conn.receive_zone_message_for_test({net::messages::GameOpcode::TraderShop, net::TraderShopMessage{.action = net::kTraderClick, .click = net::TraderClick{net::kTraderClick, kTrader, 1}}});
    REQUIRE(conn.merchant());
    CHECK(conn.merchant()->open);
    CHECK(conn.merchant()->trader);

    // An NPC that is neither a trader nor a buyer still gets nothing from this branch (no inspect request).
    conn.close_merchant();
    conn.world_for_test().spawns[kTrader].trader = false;
    rig.run_for(std::chrono::milliseconds(50)); // the click and the shop's end reach the server first
    std::size_t before = rig.server.zone_log.size();
    conn.right_click_player(kTrader);
    rig.run_for(std::chrono::milliseconds(50));
    for (std::size_t i = before; i < rig.server.zone_log.size(); ++i)
        CHECK(rig.server.zone_log[i].opcode != net::rof2::to_wire(net::rof2::Op::TraderShop));
    CHECK_FALSE(conn.merchant());
}
