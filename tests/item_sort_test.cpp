#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

#include "game/inventory.h"
#include "game/item_sort.h"
#include "net/protocol/rof2/packets/item_packets.h"

using namespace hailstone;
using net::Item;
using net::ItemSlot;

namespace {

Item bag(std::uint8_t slots, std::uint8_t size = 4, std::uint8_t type = 0) {
    Item b;
    b.item_class = 1;
    b.bag_slots = slots;
    b.bag_size = size;
    b.bag_type = type;
    b.name = "Bag";
    b.id = 17000 + slots;
    return b;
}

Item thing(std::uint32_t id, std::string name, std::uint8_t type = 11, std::uint32_t count = 1, std::uint32_t max = 0,
           std::uint8_t size = 1) {
    Item it;
    it.id = id;
    it.name = std::move(name);
    it.item_type = type;
    it.stack_count = count;
    it.max_stack = max;
    it.size = size;
    return it;
}

ItemSlot inv(std::int16_t slot, std::int16_t sub = -1) { return ItemSlot::possessions(slot, sub); }
const ItemSlot kCursor = inv(net::kSlotCursor);

// The clicks as GameConnection::click_slot (Shift) makes them: an empty cursor picks up the whole item; a held item
// merges into the same stackable item (as much as fits) or swaps. Fails the test when a click's expectation is wrong.
void play(game::Inventory& v, const std::vector<game::SortClick>& clicks) {
    for (const auto& c : clicks) {
        const Item* held = v.at(kCursor);
        REQUIRE((held ? held->id : 0u) == c.cursor_id);
        Item* there = v.at(c.slot);
        if (!held) {
            REQUIRE(there);
            v.swap(c.slot, kCursor);
        } else if (there && game::can_stack_with(*held, *there)) {
            Item* h = v.at(kCursor);
            std::uint32_t moved = std::min(there->max_stack - there->stack_count, h->stack_count);
            REQUIRE(moved > 0);
            there->stack_count += moved;
            h->stack_count -= moved;
            if (h->stack_count == 0) v.take(kCursor);
        } else {
            if (c.slot.sub >= 0) REQUIRE(game::bag_fit(*v.at(c.slot.parent()), *held) == 1);
            v.swap(kCursor, c.slot);
        }
    }
    CHECK(v.at(kCursor) == nullptr);
}

std::map<std::uint32_t, std::uint32_t> totals(const game::Inventory& v, const std::vector<game::SortPosition>& ps) {
    std::map<std::uint32_t, std::uint32_t> out;
    for (const auto& p : ps)
        if (const Item* it = v.at(p.slot)) out[it->id] += it->stack_count;
    return out;
}

std::vector<std::int16_t> general() {
    std::vector<std::int16_t> s;
    for (std::int16_t i = net::kSlotGeneral1; i <= net::kSlotGeneral10; ++i) s.push_back(i);
    return s;
}

} // namespace

TEST_CASE("item sort: positions are the bags' slots") {
    game::Inventory v;
    v.put(inv(23), bag(4));
    v.put(inv(25), thing(1, "Rock"));
    v.put(inv(26), bag(2));
    auto ps = game::sort_positions(v, net::kInvPossessions, general());
    REQUIRE(ps.size() == 6);
    CHECK(ps[0].slot.same(inv(23, 0)));
    CHECK(ps[4].slot.same(inv(26, 0)));
}

TEST_CASE("item sort: by type then name, through the cursor") {
    game::Inventory v;
    v.put(inv(23), bag(6));
    v.put(inv(24), bag(4));
    v.put(inv(23, 0), thing(5, "Zebra Hide", 11));
    v.put(inv(23, 1), thing(3, "Apple", 14));
    v.put(inv(23, 3), thing(4, "Bread", 14));
    v.put(inv(24, 2), thing(2, "Axe", 0));
    v.put(inv(24, 3), thing(6, "Arrow", 27, 20, 100));
    v.put(inv(25), thing(7, "Mug", 11));
    auto ps = game::sort_positions(v, net::kInvPossessions, general());
    auto before = totals(v, ps);
    auto plan = game::plan_sort(v, ps);
    REQUIRE(plan);
    play(v, *plan);
    CHECK(totals(v, ps) == before);
    std::vector<std::string> names;
    for (int i = 0; i < 6; ++i) names.push_back(v.at(inv(23, static_cast<std::int16_t>(i))) ? v.at(inv(23, static_cast<std::int16_t>(i)))->name : "");
    CHECK(names == std::vector<std::string>{"Axe", "Zebra Hide", "Apple", "Bread", "Arrow", ""});
    CHECK(v.at(inv(25))->name == "Mug"); // what is loose stays
    // sorted again: nothing to do
    auto again = game::plan_sort(v, ps);
    REQUIRE(again);
    CHECK(again->empty());
}

TEST_CASE("item sort: stacks of one item are never dropped on each other") {
    game::Inventory v;
    v.put(inv(23), bag(4));
    v.put(inv(23, 0), thing(9, "Bone Chips", 11, 5, 20));
    v.put(inv(23, 1), thing(1, "Ale", 11));
    v.put(inv(23, 2), thing(9, "Bone Chips", 11, 7, 20));
    auto ps = game::bag_positions(v, inv(23));
    auto plan = game::plan_sort(v, ps);
    REQUIRE(plan);
    play(v, *plan);
    CHECK(v.at(inv(23, 0))->name == "Ale");
    CHECK(v.at(inv(23, 1))->stack_count + v.at(inv(23, 2))->stack_count == 12);
}

TEST_CASE("item sort: an item goes only where its bag takes it") {
    game::Inventory v;
    v.put(inv(23), bag(2, 1)); // small items only
    v.put(inv(24), bag(2, 4));
    v.put(inv(24, 0), thing(2, "A Big Thing", 11, 1, 0, 3));
    v.put(inv(24, 1), thing(1, "Z Small Thing", 11, 1, 0, 1));
    auto ps = game::sort_positions(v, net::kInvPossessions, {23, 24});
    auto plan = game::plan_sort(v, ps);
    REQUIRE(plan);
    play(v, *plan);
    CHECK(v.at(inv(23, 0))->name == "Z Small Thing");
    CHECK(v.at(inv(24, 0))->name == "A Big Thing");
}

TEST_CASE("item stack: later stacks fill the earlier ones") {
    game::Inventory v;
    v.put(inv(23), bag(5));
    v.put(inv(23, 0), thing(9, "Bone Chips", 11, 15, 20));
    v.put(inv(23, 1), thing(1, "Ale", 11));
    v.put(inv(23, 2), thing(9, "Bone Chips", 11, 8, 20));
    v.put(inv(23, 4), thing(9, "Bone Chips", 11, 4, 20));
    auto ps = game::bag_positions(v, inv(23));
    auto plan = game::plan_stack(v, ps);
    play(v, plan);
    CHECK(v.at(inv(23, 0))->stack_count == 20);
    CHECK(v.at(inv(23, 2))->stack_count == 7);
    CHECK(v.at(inv(23, 4)) == nullptr);
    CHECK(game::plan_stack(v, ps).empty());
}

TEST_CASE("item sort: a filtered bag takes its kind first") {
    game::Inventory v;
    v.put(inv(23), bag(3));
    v.put(inv(24), bag(3));
    v.put(inv(23, 0), thing(1, "Bread", 14));
    v.put(inv(23, 1), thing(2, "Axe", 0));
    v.put(inv(23, 2), thing(3, "Milk", 15));
    v.put(inv(24, 0), thing(4, "Dagger", 2));
    auto ps = game::sort_positions(v, net::kInvPossessions, {23, 24});
    for (auto& p : ps)
        if (p.slot.slot == 24) p.filter = game::ItemCategory::Food;
    auto plan = game::plan_sort(v, ps);
    REQUIRE(plan);
    play(v, *plan);
    CHECK(v.at(inv(24, 0))->name == "Bread");
    CHECK(v.at(inv(24, 1))->name == "Milk");
    CHECK(v.at(inv(23, 0))->name == "Axe");
    CHECK(v.at(inv(23, 1))->name == "Dagger");
}

TEST_CASE("item tidy: items into the free slots of their kind's bag") {
    game::Inventory v;
    v.put(inv(23), bag(3));
    v.put(inv(24), bag(2));
    v.put(inv(23, 0), thing(1, "Bread", 14));
    v.put(inv(23, 1), thing(2, "Axe", 0));
    v.put(inv(23, 2), thing(3, "Milk", 15));
    v.put(inv(24, 1), thing(5, "Water", 15));
    auto ps = game::sort_positions(v, net::kInvPossessions, {23, 24});
    for (auto& p : ps)
        if (p.slot.slot == 24) p.filter = game::ItemCategory::Food;
    auto plan = game::plan_tidy(v, ps);
    play(v, plan);
    CHECK(v.at(inv(24, 0))->name == "Bread");
    CHECK(v.at(inv(23, 2))->name == "Milk"); // no room left
    CHECK(v.at(inv(23, 1))->name == "Axe");
    CHECK(game::category_by_name("food") == game::ItemCategory::Food);
    CHECK(game::category_name(game::ItemCategory::Augments) == "augments");
}
