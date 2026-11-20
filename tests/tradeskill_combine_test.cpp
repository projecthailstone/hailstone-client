// CContainerWnd::HandleCombine @0x65f7b0's last step against the mock EQEmu (net/connection/game_connection_progress.cpp): a
// combine is sent at once, but a transformation mold (container types 0x37 / 0x38) asks first (ConfirmationDialog
// 0x86); ConfirmationDialog_HandleResponse's 0x86 sends the kept request on a yes and drops the pending one on a no.

#include <doctest/doctest.h>

#include <cstddef>
#include <string>

#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/door_packets.h"
#include "net/protocol/rof2/packets/trade_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

Item bag_with_one(std::uint32_t id, std::uint8_t type, std::int16_t slot) {
    Item bag = make_item(id, "Bag " + std::to_string(type), slot);
    bag.item_class = 1;
    bag.bag_type = type;
    bag.bag_slots = 10;
    BagItem in;
    in.index = 0;
    in.item = make_item(id + 1, "Component", slot);
    in.item.slot = ItemSlot::possessions(slot, 0);
    bag.contents.push_back(in);
    return bag;
}

std::size_t combines(const MockEqServer& server) { return MockEqServer::count(server.zone_log, rof2::Op::TradeSkillCombine); }

} // namespace

TEST_CASE("mock server: a transformation mold's combine asks first; yes sends, no takes the request back") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());
    for (int t = 0; t < 4; ++t) // the mock's profile holds coin on the cursor (12024 refuses a combine)
        if (auto n = conn.cursor_coins()[static_cast<std::size_t>(t)]) conn.move_coin(kCoinCursor, kCoinInventory, t, t, n);

    server.item_packet(kItemPacketCharInventory, bag_with_one(17000, 0x37, 24));
    server.item_packet(kItemPacketCharInventory, bag_with_one(17100, 0x38, 25));
    server.item_packet(kItemPacketCharInventory, bag_with_one(17200, 0x0a, 26)); // a toolbox: no question
    REQUIRE(rig.until([&] { return conn.inventory().at(ItemSlot::possessions(26)) != nullptr; }));
    REQUIRE(conn.inventory().at(ItemSlot::possessions(24))->contents.size() == 1);

    // 0x37: the question (6328), nothing sent, the request pending; a no sends nothing and frees it.
    CHECK(conn.combine_container(24));
    REQUIRE(conn.confirmation().has_value());
    CHECK(conn.confirmation()->id == kConfirmMoldCombine);
    CHECK(conn.combine_pending());
    CHECK_FALSE(conn.combine_container(25)); // pending: refused
    conn.answer_confirmation(false);
    CHECK_FALSE(conn.combine_pending());
    rig.run_for(std::chrono::milliseconds(50));
    CHECK(combines(server) == 0);

    // 0x38 (6339): a yes sends the kept OP_TradeSkillCombine for its slot.
    CHECK(conn.combine_container(25));
    REQUIRE(conn.confirmation().has_value());
    CHECK(conn.confirmation()->id == kConfirmMoldCombine);
    conn.answer_confirmation(true);
    CHECK(conn.combine_pending());
    REQUIRE(rig.until([&] { return combines(server) == 1; }));
    for (const auto& p : server.zone_log)
        if (rof2::from_wire(p.opcode) == rof2::Op::TradeSkillCombine) {
            REQUIRE(p.data.size() == kTradeSkillCombineSize);
            CHECK(std::to_integer<int>(p.data[0]) == 0);  // possessions
            CHECK(std::to_integer<int>(p.data[4]) == 25); // the mold's slot
        }

    // The server's answer ends the request; an ordinary container combines without a question.
    server.zone.send_reliable(AppPacket(rof2::to_wire(rof2::Op::TradeSkillCombine)));
    REQUIRE(rig.until([&] { return !conn.combine_pending(); }));
    CHECK(conn.combine_container(26));
    CHECK_FALSE(conn.confirmation().has_value());
    REQUIRE(rig.until([&] { return combines(server) == 2; }));
}
