// GameConnection::merchant_buy (CMerchantWnd::PurchasePageHandler::RequestGetItem @0x6f5be0): the cost against the
// purse, before the request goes out.
#include <doctest/doctest.h>

#include "net/messages/item.h"
#include "net/messages/merchant.h"
#include "mq_test_world.h"

using namespace hailstone;

TEST_CASE("merchant_buy: a price times quantity past 2^31 copper is not taken as affordable") {
    // 300,000 platinum each, 8 of them: a signed 32-bit product overflowed (undefined) and wrapped negative, so the
    // can-afford check passed and the purchase went out. In 64 bits it is refused with the can't-afford message.
    mq_test::World w("hailstone_merchant_buy_cost_test", {}, true);
    auto& c = *w.conn;
    auto& world = c.world_for_test();
    game::Spawn m = *world.player();
    m.id = 5001;
    m.name = "Merchant00";
    m.class_id = 41;
    m.is_npc = true;
    world.spawns[m.id] = m;
    c.open_merchant(m.id);
    REQUIRE(c.merchant());
    net::MerchantClick open;
    open.npc_id = m.id;
    open.player_id = world.player_id;
    open.rate = 1.0f;
    c.receive_zone_message_for_test({net::messages::GameOpcode::ShopRequest, open});
    REQUIRE(c.merchant()->open);
    net::ItemPacket ip;
    ip.type = net::kItemPacketMerchant;
    ip.item.id = 1001;
    ip.item.name = "Costly Thing";
    ip.item.price = 300000000u;
    ip.item.slot.slot = 1;
    c.receive_zone_message_for_test({net::messages::GameOpcode::ItemPacket, ip});
    REQUIRE(c.merchant()->items.count(1));
    while (c.poll_chat()) {}
    c.merchant_buy(1, 8);
    CHECK(c.merchant()->pending == 0);
    CHECK(c.poll_chat().has_value()); // 12061, can't afford
    // One the purse holds goes out.
    ip.item.price = 10;
    ip.item.slot.slot = 2;
    c.receive_zone_message_for_test({net::messages::GameOpcode::ItemPacket, ip});
    c.merchant_buy(2, 1);
    CHECK(c.merchant()->pending == 1);
}
