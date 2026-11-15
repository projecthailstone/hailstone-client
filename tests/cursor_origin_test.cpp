// Hailstone's cursor origin (GameConnection::cursor_origin, the plugin host's cursor_origin; docs/plugins.md "The
// cursor item's origin") and the plugin commands /destroycursor <id> and /stowcursor <id>, against the mock server.
#include <doctest/doctest.h>

#include <optional>

#include "mock_server/harness.h"
#include "mq_test_world.h"
#include "net/item_packets.h"
#include "net/packet_io.h"

using namespace hailstone;
using namespace hailstone::test;

namespace {

constexpr std::uint32_t kUnknown = 0, kInventory = 1, kServer = 2; // HAILSTONE_CURSOR_ORIGIN_*
constexpr auto kSecondGeneral = static_cast<std::int16_t>(net::kSlotGeneral1 + 1);

// The mock's character zones in holding coin (profile_builder.cpp): put it away, one coin a click.
void put_coin_away(net::GameConnection& c) {
    for (int i = 0; i < 4; ++i) c.drop_on_doll();
    for (std::uint32_t n : c.cursor_coins()) REQUIRE(n == 0u);
}

bool holds(const net::GameConnection& c, std::uint32_t id) {
    const net::Item* it = c.inventory().cursor();
    return it && it->id == id;
}

std::optional<net::MoveItem> last_move(const MockEqServer& server) {
    std::optional<net::MoveItem> m;
    for (const auto& p : server.zone_log)
        if (net::is_op(p, net::Op::MoveItem)) m = net::decode_move_item(p);
    return m;
}

net::Item bag_item(std::uint32_t id, std::int16_t slot) {
    net::Item b = make_item(id, "Backpack", slot);
    b.item_class = 1;
    b.bag_slots = 4;
    b.bag_size = 4;
    return b;
}

} // namespace

TEST_CASE("cursor origin: the server's items, the player's pickups, and nothing stale") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());
    put_coin_away(conn);
    CHECK(conn.cursor_origin() == kUnknown); // nothing held

    // An item packet to the empty cursor (EQEmu's ground pickup and loot onto the cursor send type 0x67).
    server.item_packet(net::kItemPacketTrade, make_item(2001, "Ground Find", net::kSlotCursor));
    REQUIRE(rig.until([&] { return holds(conn, 2001); }));
    CHECK(conn.cursor_origin() == kServer);
    // Another one onto the occupied cursor: not known.
    server.item_packet(net::kItemPacketTrade, make_item(2002, "Second Find", net::kSlotCursor));
    REQUIRE(rig.until([&] { return holds(conn, 2002); }));
    CHECK(conn.cursor_origin() == kUnknown);
    conn.destroy_cursor();
    CHECK(conn.cursor_origin() == kUnknown);

    // The cursor queue's item packet (EQEmu's PushItemOnCursor: summoned items, tradeskill results).
    server.item_packet(net::kItemPacketLimbo, make_item(2003, "Summoned Bread", net::kSlotCursor));
    REQUIRE(rig.until([&] { return holds(conn, 2003); }));
    CHECK(conn.cursor_origin() == kServer);

    // The cursor emptied by the server, then a new item by a path that marks nothing: unknown, not the old origin.
    using A = net::GameConnectionTestAccess;
    server.zone.send_reliable(net::encode_delete_item({net::ItemSlot::possessions(net::kSlotCursor), net::ItemSlot::invalid(), 0xffffffffu}));
    REQUIRE(rig.until([&] { return conn.inventory().cursor() == nullptr; }));
    CHECK(conn.cursor_origin() == kUnknown);
    A::inventory(conn).put(net::ItemSlot::possessions(net::kSlotCursor), make_item(2004, "Unmarked", net::kSlotCursor));
    REQUIRE(holds(conn, 2004));
    CHECK(conn.cursor_origin() == kUnknown);
    // Even replacing a server item in place, without emptying the cursor first.
    A::inventory(conn).take(net::ItemSlot::possessions(net::kSlotCursor));
    server.item_packet(net::kItemPacketLimbo, make_item(2005, "Summoned Water", net::kSlotCursor));
    REQUIRE(rig.until([&] { return holds(conn, 2005); }));
    REQUIRE(conn.cursor_origin() == kServer);
    A::inventory(conn).put(net::ItemSlot::possessions(net::kSlotCursor), make_item(2006, "Swapped In", net::kSlotCursor));
    CHECK(conn.cursor_origin() == kUnknown);
    A::inventory(conn).take(net::ItemSlot::possessions(net::kSlotCursor));

    // The player's own pickup: the Short Sword (slot 13), and part of the Rations stack (slot 23, 5).
    conn.click_slot(net::ItemSlot::possessions(13));
    REQUIRE(holds(conn, 1001));
    CHECK(conn.cursor_origin() == kInventory);
    conn.click_slot(net::ItemSlot::possessions(13)); // put back
    REQUIRE_FALSE(conn.inventory().cursor());
    CHECK(conn.cursor_origin() == kUnknown);
    conn.click_slot(net::ItemSlot::possessions(net::kSlotGeneral1)); // the quantity dialog
    conn.finish_quantity(2);
    REQUIRE(holds(conn, 1002));
    CHECK(conn.inventory().cursor()->stack_count == 2u);
    CHECK(conn.cursor_origin() == kInventory);
}

TEST_CASE("cursor origin: an item held at the zone-in is not known") {
    MockConfig config = MockConfig::standard();
    config.characters[0].inventory.push_back(make_item(13005, "Iron Ration", net::kSlotCursor));
    Rig rig(std::move(config));
    REQUIRE(rig.to_zone());
    REQUIRE(holds(rig.conn, 13005));
    CHECK(rig.conn.cursor_origin() == kUnknown);
}

TEST_CASE("/destroycursor and /stowcursor act only on the cursor item of the id given") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());

    // Nothing held but coin: neither touches the coin.
    const auto coins = conn.cursor_coins();
    REQUIRE((coins[0] | coins[1] | coins[2] | coins[3]) != 0u);
    conn.send_command("/destroycursor 1");
    conn.send_command("/stowcursor 1");
    CHECK(conn.cursor_coins() == coins);
    put_coin_away(conn);
    const int moves = MockEqServer::count(server.zone_log, net::Op::MoveItem);

    server.item_packet(net::kItemPacketLimbo, make_item(2001, "Summoned Bread", net::kSlotCursor));
    REQUIRE(rig.until([&] { return holds(conn, 2001); }));
    // Another id, no id, a malformed one: nothing.
    for (const char* line : {"/destroycursor 2002", "/destroycursor", "/destroycursor 2001x", "/destroycursor -2001",
                             "/stowcursor 2002", "/stowcursor"})
        conn.send_command(line);
    CHECK(holds(conn, 2001));
    // Its own id: destroyed, OP_MoveItem {cursor, nowhere}. It is the only move the server sees, so the commands
    // above sent nothing (reliable packets arrive in order).
    conn.send_command("/destroycursor 2001");
    CHECK_FALSE(conn.inventory().cursor());
    REQUIRE(rig.until([&] { return MockEqServer::count(server.zone_log, net::Op::MoveItem) == moves + 1; }));
    auto m = last_move(server);
    REQUIRE(m);
    CHECK(m->from.same(net::ItemSlot::possessions(net::kSlotCursor)));
    CHECK(m->to.type == -1);

    // A bag is never destroyed, even of the right id.
    server.item_packet(net::kItemPacketLimbo, bag_item(3000, net::kSlotCursor));
    REQUIRE(rig.until([&] { return holds(conn, 3000); }));
    conn.send_command("/destroycursor 3000");
    CHECK(holds(conn, 3000));
    // Stowed by its id: into the first free general slot (the Rations hold the first).
    conn.send_command("/stowcursor 3001");
    CHECK(holds(conn, 3000));
    conn.send_command("/stowcursor 3000");
    CHECK_FALSE(conn.inventory().cursor());
    REQUIRE(rig.until([&] { return MockEqServer::count(server.zone_log, net::Op::MoveItem) == moves + 2; }));
    m = last_move(server);
    REQUIRE(m);
    CHECK(m->from.same(net::ItemSlot::possessions(net::kSlotCursor)));
    CHECK(m->to.same(net::ItemSlot::possessions(kSecondGeneral)));
    const net::Item* stowed = conn.inventory().at(net::ItemSlot::possessions(kSecondGeneral));
    REQUIRE(stowed);
    CHECK(stowed->id == 3000u);
}
