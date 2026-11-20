// Hailstone's Combine N / Combine All in the tradeskill window (game/tradeskill.h CombineBatch, net/connection/game_connection_progress.cpp):
// the components against what the character carries, the room a result needs, where a result is put away, and a
// batch against the mock EQEmu (the server's side played by the test as HandleAutoCombine does it: the components
// taken, the answer, then the result on the cursor).

#include <doctest/doctest.h>

#include <string>

#include "game/inventory.h"
#include "game/tradeskill.h"
#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/door_packets.h"
#include "net/protocol/rof2/packets/trade_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

Item stack_of(std::uint32_t id, const std::string& name, std::uint32_t count, std::int16_t slot, std::int16_t sub = -1) {
    Item it = make_item(id, name, slot, count);
    it.max_stack = 20;
    it.slot = ItemSlot::possessions(slot, sub);
    return it;
}

Item bag(std::uint32_t id, std::uint8_t type, std::uint8_t slots, std::int16_t slot) {
    Item b = make_item(id, "Bag", slot);
    b.item_class = 1;
    b.bag_type = type;
    b.bag_slots = slots;
    b.bag_size = 4;
    return b;
}

RecipeDetails details(std::uint32_t recipe, std::initializer_list<std::pair<std::uint32_t, const char*>> items) {
    RecipeDetails d;
    d.recipe_id = recipe;
    for (const auto& [id, name] : items) d.components.push_back({id, 500, name});
    return d;
}

const auto kAllSlots = [](int) { return true; };

} // namespace

TEST_CASE("tradeskill batch: carried components, what a recipe makes, and partial details") {
    game::Inventory inv;
    inv.put(ItemSlot::possessions(23), bag(9000, 10, 4, 23));
    inv.put(ItemSlot::possessions(23, 0), stack_of(13006, "Water Flask", 5, 23, 0));
    inv.put(ItemSlot::possessions(24), stack_of(13006, "Water Flask", 2, 24));
    inv.put(ItemSlot::possessions(25), make_item(13068, "Bat Wing", 25)); // not stackable: one
    inv.put(ItemSlot::possessions(1), stack_of(13006, "Water Flask", 9, 1));    // worn: HandleAutoCombine skips
    inv.put(ItemSlot::possessions(kSlotCursor), stack_of(13006, "Water Flask", 9, kSlotCursor)); // and the cursor
    CHECK(game::carried_count(inv, 13006) == 7);
    CHECK(game::carried_count(inv, 13068) == 1);
    CHECK(game::carried_count(inv, 1) == 0);

    // SendTradeskillDetails lists an item once for each one needed.
    auto d = details(500, {{13006, "Water Flask"}, {13068, "Bat Wing"}, {13006, "Water Flask"}});
    game::RecipeNeeds n = game::recipe_needs(d, 3, inv);
    REQUIRE(n.components.size() == 2);
    CHECK(n.components[0].name == "Water Flask");
    CHECK(n.components[0].need == 2);
    CHECK(n.components[0].have == 7);
    CHECK(n.components[1].need == 1);
    CHECK(n.components[1].have == 1);
    CHECK(n.complete);
    CHECK(n.makeable == 1);

    // A recipe of more than the ten listed: the count is at most what the list says.
    n = game::recipe_needs(d, 12, inv);
    CHECK_FALSE(n.complete);
    CHECK(n.makeable == 1);

    // Missing one: nothing to make.
    n = game::recipe_needs(details(501, {{13006, "Water Flask"}, {77, "Something"}}), 2, inv);
    CHECK(n.makeable == 0);
    CHECK(n.components[1].have == 0);
    CHECK(game::recipe_needs(RecipeDetails{}, 0, inv).makeable == 0);
}

TEST_CASE("tradeskill batch: room for a result, and where one is put away") {
    game::Inventory inv;
    for (std::int16_t s = 23; s <= 32; ++s) inv.put(ItemSlot::possessions(s), make_item(100u + static_cast<std::uint32_t>(s), "Junk", s));
    CHECK_FALSE(game::has_room_for_result(inv, kAllSlots, 0));
    Item result = stack_of(13100, "Result", 1, kSlotCursor);
    CHECK_FALSE(game::put_away_slot(inv, result, kAllSlots).has_value());

    // A general slot the character does not have is no room.
    inv.take(ItemSlot::possessions(32));
    CHECK_FALSE(game::has_room_for_result(inv, [](int s) { return s != 32; }, 0));
    CHECK(game::has_room_for_result(inv, kAllSlots, 0));
    auto p = game::put_away_slot(inv, result, kAllSlots);
    REQUIRE(p);
    CHECK(p->to.same(ItemSlot::possessions(32)));
    CHECK_FALSE(p->merge);

    // A stack of the result with room: room, and it goes there first; a full stack is not.
    inv.put(ItemSlot::possessions(32), stack_of(13100, "Result", 20, 32));
    CHECK_FALSE(game::has_room_for_result(inv, kAllSlots, 13100));
    inv.at(ItemSlot::possessions(32))->stack_count = 3;
    CHECK(game::has_room_for_result(inv, kAllSlots, 13100));
    CHECK_FALSE(game::has_room_for_result(inv, kAllSlots, 0)); // the result not known yet
    p = game::put_away_slot(inv, result, kAllSlots);
    REQUIRE(p);
    CHECK(p->to.same(ItemSlot::possessions(32)));
    CHECK(p->merge);

    // An empty place in a bag; never a worn slot.
    inv.take(ItemSlot::possessions(31));
    inv.put(ItemSlot::possessions(31), bag(9000, 1, 2, 31));
    Item sword = make_item(5000, "Sword", kSlotCursor);
    CHECK(game::has_room_for_result(inv, kAllSlots, 0));
    p = game::put_away_slot(inv, sword, kAllSlots);
    REQUIRE(p);
    CHECK(p->to.same(ItemSlot::possessions(31, 0)));
    inv.at(ItemSlot::possessions(31))->bag_size = 1;
    sword.size = 3; // too big for the bag (bag_fit): nowhere
    CHECK_FALSE(game::put_away_slot(inv, sword, kAllSlots).has_value());
}

TEST_CASE("mock server: Combine All puts each result away and stops when the components run out") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());
    for (int t = 0; t < 4; ++t) // the mock's profile holds coin on the cursor
        if (auto n = conn.cursor_coins()[static_cast<std::size_t>(t)]) conn.move_coin(kCoinCursor, kCoinInventory, t, t, n);

    // A two-place toolbox holding the components, and every other general slot taken.
    Item box = bag(9000, 10, 2, 23);
    box.contents.push_back({0, stack_of(13006, "Water Flask", 5, 23, 0)});
    box.contents.push_back({1, stack_of(13068, "Bat Wing", 3, 23, 1)});
    server.item_packet(kItemPacketCharInventory, box);
    for (std::int16_t s = 24; s <= 32; ++s)
        server.item_packet(kItemPacketCharInventory, make_item(100u + static_cast<std::uint32_t>(s), "Junk", s));
    REQUIRE(rig.until([&] { return conn.inventory().at(ItemSlot::possessions(32)) != nullptr; }));

    conn.open_tradeskill_bag(ItemSlot::possessions(23));
    REQUIRE(conn.tradeskill().open);
    RecipeReply r{10, 9000, 3, 500, 20, "Bat Wing Stew"};
    server.zone.send_reliable(encode(r));
    REQUIRE(rig.until([&] { return conn.tradeskill().recipes.size() == 1; }));
    conn.tradeskill_select(500);
    server.zone.send_reliable(encode(details(500, {{13006, "Water Flask"}, {13006, "Water Flask"}, {13068, "Bat Wing"}})));
    REQUIRE(rig.until([&] { return conn.tradeskill().details.has_value(); }));
    game::RecipeNeeds n = conn.tradeskill_needs();
    CHECK(n.makeable == 2);

    // No room for the result: refused, and said.
    rig.chat();
    CHECK(conn.tradeskill_start_batch(0) == "Your inventory is full.");
    CHECK(has_line(rig.chat(), "Your inventory is full."));
    CHECK_FALSE(conn.tradeskill().batch);

    server.zone.send_reliable(encode_delete_item({ItemSlot::possessions(32), ItemSlot::invalid(), 0xffffffffu}));
    REQUIRE(rig.until([&] { return conn.inventory().at(ItemSlot::possessions(32)) == nullptr; }));
    CHECK(conn.tradeskill_start_batch(0).empty());
    REQUIRE(conn.tradeskill().batch);

    // HandleAutoCombine: the components taken, the answer, the result on the cursor.
    auto serve = [&](int nth, std::uint32_t water, std::uint32_t wings) {
        REQUIRE(rig.until([&] { return MockEqServer::count(server.zone_log, rof2::Op::RecipeAutoCombine) == nth; }));
        server.item_packet(kItemPacketCharInventory, stack_of(13006, "Water Flask", water, 23, 0));
        server.item_packet(kItemPacketCharInventory, stack_of(13068, "Bat Wing", wings, 23, 1));
        RecipeAutoCombine a;
        a.object_type = 10;
        a.container_id = 9000;
        a.recipe_id = 500;
        server.zone.send_reliable(encode(a));
        server.item_packet(kItemPacketLimbo, stack_of(13100, "Bat Wing Stew", 1, kSlotCursor));
    };
    serve(1, 3, 2);
    // Put away in the free slot; then no slot is free, but the stew's stack has room.
    REQUIRE(rig.until([&] {
        const Item* it = conn.inventory().at(ItemSlot::possessions(32));
        return it && it->id == 13100 && !conn.inventory().cursor();
    }));
    serve(2, 1, 1);
    REQUIRE(rig.until([&] { return !conn.tradeskill().batch; }));
    const Item* stew = conn.inventory().at(ItemSlot::possessions(32));
    REQUIRE(stew);
    CHECK(stew->stack_count == 2);
    CHECK_FALSE(conn.inventory().cursor());
    CHECK(has_line(rig.chat(), "Combining stopped after 2 combines: you are out of components."));
    CHECK(MockEqServer::count(server.zone_log, rof2::Op::RecipeAutoCombine) == 2);
    CHECK(conn.tradeskill_start_batch(0) == "You do not have the components.");
}

TEST_CASE("mock server: a combine batch stops when the result has nowhere to go, and when the server refuses") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());
    for (int t = 0; t < 4; ++t)
        if (auto n = conn.cursor_coins()[static_cast<std::size_t>(t)]) conn.move_coin(kCoinCursor, kCoinInventory, t, t, n);
    Item box = bag(9000, 10, 2, 23);
    box.contents.push_back({0, stack_of(13006, "Water Flask", 10, 23, 0)});
    server.item_packet(kItemPacketCharInventory, box);
    for (std::int16_t s = 24; s <= 31; ++s)
        server.item_packet(kItemPacketCharInventory, make_item(100u + static_cast<std::uint32_t>(s), "Junk", s));
    REQUIRE(rig.until([&] { return conn.inventory().at(ItemSlot::possessions(31)) != nullptr; }));
    conn.open_tradeskill_bag(ItemSlot::possessions(23));
    server.zone.send_reliable(encode(RecipeReply{10, 9000, 1, 501, 20, "Water"}));
    REQUIRE(rig.until([&] { return conn.tradeskill().recipes.size() == 1; }));
    conn.tradeskill_select(501);
    server.zone.send_reliable(encode(details(501, {{13006, "Water Flask"}})));
    REQUIRE(rig.until([&] { return conn.tradeskill().details.has_value(); }));

    // Slot 32 and the bag's second place are free: room for one result. The first result is put away; it does not
    // stack, the next has no room: the batch stops before combining again.
    CHECK(conn.tradeskill_start_batch(5).empty());
    REQUIRE(rig.until([&] { return MockEqServer::count(server.zone_log, rof2::Op::RecipeAutoCombine) == 1; }));
    server.item_packet(kItemPacketCharInventory, stack_of(13006, "Water Flask", 9, 23, 0));
    RecipeAutoCombine a;
    a.object_type = 10;
    a.container_id = 9000;
    a.recipe_id = 501;
    server.zone.send_reliable(encode(a));
    server.item_packet(kItemPacketLimbo, make_item(13200, "Pot", kSlotCursor));
    REQUIRE(rig.until([&] { return !conn.inventory().cursor() && conn.inventory().at(ItemSlot::possessions(32)); }));
    {
        Item more = make_item(13201, "Junk", 23);
        more.slot = ItemSlot::possessions(23, 1);
        server.item_packet(kItemPacketCharInventory, more);
    }
    REQUIRE(rig.until([&] { return !conn.tradeskill().batch; }));
    CHECK(has_line(rig.chat(), "Combining stopped after 1 combine: your inventory is full."));
    CHECK(MockEqServer::count(server.zone_log, rof2::Op::RecipeAutoCombine) == 1);

    // The server's refusal (reply 0xfffffff5: missing components, a lore conflict) ends the batch.
    server.zone.send_reliable(encode_delete_item({ItemSlot::possessions(32), ItemSlot::invalid(), 0xffffffffu}));
    REQUIRE(rig.until([&] { return conn.inventory().at(ItemSlot::possessions(32)) == nullptr; }));
    CHECK(conn.tradeskill_start_batch(0).empty());
    REQUIRE(rig.until([&] { return MockEqServer::count(server.zone_log, rof2::Op::RecipeAutoCombine) == 2; }));
    a.reply_code = kAutoCombineFailed;
    server.zone.send_reliable(encode(a));
    REQUIRE(rig.until([&] { return !conn.tradeskill().batch; }));
    CHECK(has_line(rig.chat(), "Combining stopped after 0 combines: the combine did not start."));

    // An item the player picks up between combines is theirs: the batch stops rather than put it away.
    CHECK(conn.tradeskill_start_batch(0).empty());
    conn.click_slot(ItemSlot::possessions(24));
    REQUIRE(conn.inventory().cursor());
    REQUIRE(rig.until([&] { return !conn.tradeskill().batch; }));
    CHECK(has_line(rig.chat(), "Combining stopped after 0 combines: you are holding an item."));
    CHECK(conn.inventory().cursor());
    CHECK(MockEqServer::count(server.zone_log, rof2::Op::RecipeAutoCombine) == 2);
}
