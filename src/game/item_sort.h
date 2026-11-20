#pragma once

// Hailstone: sorting and stacking bags (the modern UI's Sort / Stack buttons; the original has neither). Each is
// planned as the slot clicks a player would make through the cursor (pick up the whole stack, put down: a swap
// or, on the same stackable item, a merge; net::GameConnection::click_slot), so the server sees nothing a player
// could not do. The plans are pure: they read the inventory and never change it.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "game/inventory.h"
#include "net/messages/item.h"

namespace hailstone::game {

// What kind of item it is, by its type (a bag's filter: Sort fills the bag with its kind first, Tidy moves its kind
// into it). Lua names: food (and drink), tradeskill, spells (scrolls and books), weapons (arrows too), armor (shields,
// jewellery, charms), potions (and poisons), augments.
enum class ItemCategory : std::uint8_t { None, Food, Tradeskill, Spells, Weapons, Armor, Potions, Augments };
ItemCategory item_category(const net::Item& item);
std::string_view category_name(ItemCategory c); // "" for None
ItemCategory category_by_name(std::string_view name);

// A place items are sorted into: a slot inside a bag (the bag must take what goes there: bag_fit), or a top-level
// slot that holds no bag (anything goes). `filter`: its bag's.
struct SortPosition {
    net::ItemSlot slot;
    ItemCategory filter = ItemCategory::None;
};

// One click (with Shift: a stack is picked up whole). `cursor_id`: the item id the cursor should hold before it,
// 0 empty; a run stops when it does not.
struct SortClick {
    net::ItemSlot slot;
    std::uint32_t cursor_id = 0;
};

// The positions of the bags at `slots` (top-level slots of one kind, in order: each bag's slots in turn). What is
// loose in a top-level slot stays there; an empty bag is a bag (its slots are positions), not something sorted.
std::vector<SortPosition> sort_positions(const Inventory& inv, std::int16_t type, const std::vector<std::int16_t>& slots);
// The slots of one bag.
std::vector<SortPosition> bag_positions(const Inventory& inv, const net::ItemSlot& bag);

// Whether `item` may be at `pos` (a bag's slot: bag_fit 1).
bool sort_fits(const Inventory& inv, const SortPosition& pos, const net::Item& item);

// Stacks of the same item filled from the later ones, in position order.
std::vector<SortClick> plan_stack(const Inventory& inv, const std::vector<SortPosition>& positions);
// The items in position order by type, then name (the same stackable item kept where it already is), a filtered
// bag's kind in it first (other items only when the rest is full); nullopt when some item has nowhere it fits.
std::optional<std::vector<SortClick>> plan_sort(const Inventory& inv, const std::vector<SortPosition>& positions);
// Each item of a kind some bag is filtered for, not in such a bag, into a free slot of one (when it has room).
std::vector<SortClick> plan_tidy(const Inventory& inv, const std::vector<SortPosition>& positions);

} // namespace hailstone::game
