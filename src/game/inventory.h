#pragma once

// The player's items as the client keeps them: possessions (equipment,
// general inventory, cursor), bank, shared bank, with bag contents inside
// their bags; plus the item rules the original checks before it moves an
// item. The server has the
// final word; the client moves items locally first and the server corrects
// it with item packets, OP_MoveItem or OP_DeleteItem.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "net/messages/item.h"

namespace hailstone::game {

class Inventory {
public:
    using Key = std::pair<std::int16_t, std::int16_t>; // (type, slot) of a top-level item

    void clear();
    // OP_CharInventory: replaces everything with the items at their own header slots.
    void load(std::vector<net::Item> items);

    // The item at a slot (top level or inside a bag), or null.
    const net::Item* at(const net::ItemSlot& slot) const;
    net::Item* at(const net::ItemSlot& slot);
    const net::Item* cursor() const { return at(net::ItemSlot::possessions(net::kSlotCursor)); }

    // Puts an item at a slot (replacing what is there). A bag slot needs a
    // bag in the parent slot; returns false otherwise. The item's header slot
    // is updated.
    bool put(const net::ItemSlot& slot, net::Item item);
    // Removes and returns the item at a slot.
    std::optional<net::Item> take(const net::ItemSlot& slot);
    // Exchanges two slots' contents (either may be empty). False if either
    // slot cannot hold items.
    bool swap(const net::ItemSlot& a, const net::ItemSlot& b);

    const std::map<Key, net::Item>& items() const { return items_; }
    // Counts every change, for UI caches.
    std::uint64_t revision() const { return revision_; }
    // Counts every put, take, swap, load or clear that touches the cursor slot (the item there or a bag's contents),
    // so a note about the item on the cursor (GameConnection's cursor origin) can tell it still describes that item.
    // A stack count changed through at() is the same item and does not count.
    std::uint64_t cursor_placement() const { return cursor_placement_; }

private:
    void touched(const net::ItemSlot& slot);

    std::map<Key, net::Item> items_;
    std::uint64_t revision_ = 0;
    std::uint64_t cursor_placement_ = 0;
};

// ---- item rules (client side; the server checks again) ----

// The client's race bit for an item's race mask (CharacterZoneClient_CanUseItemRace
// @00446200; game::item_race_bit_of): races below 13 themselves, Iksar 128 → 13, Vah Shir 130 → 14,
// Froglok 330 → 15, Drakkin 522 → 16, anything else 17.
int item_race_bit(std::uint32_t race);
// CharacterZoneClient::CanUseItem(item, 0, 0) @0044c430, the check the click
// path uses: bags and books always; else the item's class mask must contain
// one of the player's classes (NMS multiclass: any of the up to three).
bool can_use_class(const net::Item& item, std::uint32_t class_mask);
// PcZoneClient_CanItemGoInEquipSlot @005831b0 for a possessions slot
// (game::can_item_go_in_equip_slot, checked against the original's vectors).
// `primary` / `secondary` are the items in slots 13 / 14 (or null);
// `dual_wield` whether the player has skill 22; `slot_available`
// CharacterBase_IsInvSlotAvailable's answer.
bool can_go_in_equip_slot(const net::Item& item, int slot, const net::Item* primary, const net::Item* secondary,
                          bool dual_wield, bool slot_available = true);
// ItemBase_GetBagFitResult @007b39e0 (this = bag): 1 fits; 0 not a bag,
// 2 placeable bag, 3 non-empty bag into a bag, 5 too big, 6 quiver needs
// arrows, 7 bandolier needs throwing items, 8 tradeskill bag, 10 type 60.
int bag_fit(const net::Item& bag, const net::Item& item);
// EQEmu's InventoryProfile::CanItemFitInContainer, the test Client::MoveItemToInventory uses when the bandolier swap
// returns an item to a bag: not too big, a quiver (bag type 2) takes arrows (27) only and a bandolier bag (8) small
// throwing items (19) only. Looser than bag_fit (no placeable, tradeskill or type 60 refusal). The server never says
// where a swap put things, so the client's prediction has to use the server's test, not the original's, or the two
// inventories part (docs/unknowns.md).
bool server_bag_accepts(const net::Item& bag, const net::Item& item);
// ItemBase_CanStackWith @007b1160.
bool can_stack_with(const net::Item& a, const net::Item& b);
// The eqstr id CanGoInBag prints for a bag_fit() result (0 = none).
std::uint32_t bag_fit_message(int fit);

// The equipment texture slot an item in a possessions slot shows on the
// body (PlayerClient material slots, EQ::InventoryProfile::CalcMaterialFromSlot):
// 0 head, 1 chest, 2 arms, 3 wrist, 4 hands, 5 legs, 6 feet, 7 primary,
// 8 secondary; -1 for slots that show nothing.
int material_slot(int possessions_slot);

// Coin text the client builds ("3 platinum, 5 gold, 1 silver, 9 copper");
// empty for no money.
std::string coin_text(std::uint32_t platinum, std::uint32_t gold, std::uint32_t silver, std::uint32_t copper);

// FormatCoinAmountsText @0x4f3f50 (English): the positive amounts as "<n> platinum", "<n> gold", "<n> silver",
// "<n> copper", each after the first preceded by ", " when a later amount is non-zero, else " and "
// ("1 platinum, 2 gold, 3 silver and 4 copper"); copper always takes " and ". The coin looted from a corpse and the
// split shares are printed with it.
std::string coin_amounts_text(std::uint32_t platinum, std::uint32_t gold, std::uint32_t silver, std::uint32_t copper);

} // namespace hailstone::game
