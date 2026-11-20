// The Advanced Inventory's Keep marks (game/kept_items.h): the file's format, and a bag holding a kept item.

#include <doctest/doctest.h>

#include "game/kept_items.h"
#include "net/protocol/rof2/packets/item_packets.h"

using namespace hailstone;

TEST_CASE("kept items: set, unset and the file's lines") {
    game::KeptItems k;
    CHECK(k.set(1001, "Fine Steel Sword", true));
    CHECK_FALSE(k.set(1001, "Fine Steel Sword", true)); // no change
    CHECK(k.set(17005, "Backpack", true));
    CHECK_FALSE(k.set(0, "Nothing", true));               // id 0 is never kept
    CHECK(k.kept(1001));
    CHECK_FALSE(k.kept(0));
    CHECK(k.format() == "# Items marked Keep in the Advanced Inventory window: <item id> <name>\n"
                        "1001 Fine Steel Sword\n17005 Backpack\n");
    CHECK(k.set(1001, "", false));
    CHECK_FALSE(k.set(1001, "", false));
    CHECK_FALSE(k.kept(1001));
}

TEST_CASE("kept items: parse skips comments, blank lines and lines without an id") {
    game::KeptItems k;
    k.parse("# a comment\r\n\n  42 Cloth Cap  \r\nnot a number\n0 Zero\n7\n13005\tIron Ration\n");
    REQUIRE(k.items().size() == 3);
    CHECK(k.items().at(42) == "Cloth Cap  ");
    CHECK(k.items().at(7).empty());
    CHECK(k.items().at(13005) == "Iron Ration");
    // what format writes, parse reads back
    game::KeptItems again;
    again.parse(k.format());
    CHECK(again.items() == k.items());
}

TEST_CASE("kept items: a bag holding a kept item counts as kept") {
    game::KeptItems k;
    k.set(1001, "Fine Steel Sword", true);
    net::Item bag;
    bag.id = 17005;
    bag.bag_slots = 8;
    bag.item_class = 1;
    net::Item ration;
    ration.id = 13005;
    bag.contents.push_back({0, ration});
    CHECK_FALSE(k.holds_kept(bag));
    net::Item sword;
    sword.id = 1001;
    bag.contents.push_back({3, sword});
    CHECK(k.holds_kept(bag));
    CHECK_FALSE(k.kept(bag.id));
}
