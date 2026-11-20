#pragma once

// Ctrl moves into a bag (CInvSlotMgr::MoveItem's bag path @0x698d80)
// and how MultipleItemMoveManager::ProcessMove @0x572300 resolves them.
//
// The list MoveItem builds, every entry with room mode 3 (the destination bag's own slots; the second loop's mode
// is 3 for each entry too, the mode is set once before the loop at 0x699403):
//  - the cursor item is not a bag, or an empty bag without bForceBag: {the cursor item -> the target bag};
//  - else each item of the cursor bag in slot order -> the target bag, and with bForceBag (Ctrl + right-click,
//    CInvSlot::HandleRButtonUp @0x697250) each item of the target bag in slot order -> the cursor bag.
// ProcessMove first takes every source out (DetachSourceItems), then finds each entry a place in its bag
// (CheckDestinations -> FindRoom @0x571870, entries a split appends included):
//  1. a stackable item tries each slot in order (0x5713d0): a slot holding an item it stacks with takes it when
//     that stack, plus what other entries already put there, has room for all of it; with room for part, that part
//     goes there and the rest becomes a new entry at the end of the list (same source); a full stack is skipped;
//  2. else the first slot that holds nothing and that no entry has taken (0x56f5c0);
//  3. (0x56f1c0 trades places with an earlier entry headed for the same location; for these lists it cannot
//     make room that 1 and 2 did not find, and is left out here.)
// Both need the bag's consent (ItemClient::CanGoInBag). An entry without a place fails the whole move: 0x20 when the
// bag refuses the item (CheckBag), else 7. MoveItem prints 0x20 as eqstr 12934 "%1 cannot be moved into %2 because
// it is too big to fit" (the cursor item, the target bag) when its list had one entry, else 5215; any other failure
// as 5214. The target bags of a swap thus end up packed from their first slot (as EQEmu's handler expects).

#include <cstdint>
#include <string>
#include <vector>

#include "net/messages/item.h"

namespace hailstone::game {

struct BagMove {
    enum class Where : std::uint8_t { Cursor, CursorBag, TargetBag };
    Where from = Where::Cursor;
    int from_sub = -1;        // the slot in the bag (-1: the cursor item itself)
    bool to_cursor_bag = false; // the destination: the cursor bag, else the target bag
    int to_sub = -1;          // the slot it was given
    std::uint32_t count = 0;  // the stack count moved (+0x18)
};

struct BagMovePlan {
    std::vector<BagMove> moves;    // in the order sent (OP_MoveMultipleItems)
    std::uint32_t eqstr = 0;       // the refusal (0: none)
    std::vector<std::string> args; // its arguments
};

// `cursor` the item on the cursor, `target` the bag clicked; `force_bag` MoveItem's bForceBag (Ctrl + right-click).
// No moves and no refusal: nothing to move (an empty cursor bag without force).
BagMovePlan plan_bag_move(const net::Item& cursor, const net::Item& target, bool force_bag);

} // namespace hailstone::game
