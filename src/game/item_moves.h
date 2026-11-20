#pragma once

// The original's item move rules, each a
// pure function checked against the vectors recorded from the original eqgame.exe:
//   - PcZoneClient_CanItemGoInEquipSlot @0x5831b0 (PcZoneClient_CanItemGoInEquipSlot.jsonl);
//   - CharacterZoneClient::CanUseItem @0x44c430 and its race / deity / level checks @0x446200 / @0x4462c0 /
//     @0x446190 (CharacterZoneClient_CanUseItem*.jsonl);
//   - Handle_OP_MoveItem_Apply @0x4c1ba0, what a server OP_MoveItem does (Handle_OP_MoveItem_Apply.jsonl);
//   - CEverQuest::MoveMoney @0x51ad70 (CEverQuest_MoveMoney.jsonl) and CInventoryWnd::ClickedMoneyButton @0x692890
//     (CInventoryWnd_ClickedMoneyButton.jsonl).
// OP_MoveMultipleItems' serialisation is net/protocol/rof2/packets/item_packets.h's encode(const MoveMultipleItems&). Nothing here does
// networking or touches the inventory; net/connection/game_connection_items.cpp and net/connection/game_connection_bank.cpp apply them.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace hailstone::game {

// ---------------------------------------------------------------- equip slots

// What PcZoneClient_CanItemGoInEquipSlot asks of an item: ItemBase_GetType (0 common, 1 bag, 2 book), the
// definition's item type (+0x1d4), ItemBase_GetRange(1), ItemBase_GetEquipSlots(1) and ItemBase_IsWeapon.
struct EquipItem {
    std::int32_t item_class = 0;
    std::int32_t item_type = 0;
    std::int32_t range = 0;
    std::uint32_t equip_slots = 0;
    bool weapon = false;
};

// ItemDefinition_IsWeapon @0x7f68a0: item type not 27, 28, 43 or 44, and Damage (+0x188) > 0.
bool item_is_weapon(std::int32_t item_type, std::int32_t damage);

// CharacterBase_IsInvSlotAvailable @0x7e6fc0: slots 0..20 and 22 need their bit in the profile's slot mask
// (+0x341c), 21 (power source) also expansion flag 0x1000; 23..32 need slot <= last_usable (GetLastUsableInvSlot,
// 32 with the full inventory); below 0 or 33 and above never.
bool inv_slot_available(std::int32_t slot, std::uint32_t slot_mask, std::uint32_t expansions, std::int32_t last_usable);

// PcZoneClient_CanItemGoInEquipSlot @0x5831b0, in the original's order:
//   1. no item, or the slot unavailable: false;
//   2. general slots 23..32: anything (bags included);
//   3. bags, books and augments (type 54): false;
//   4. range (11): types 27 / 28 refused, range > 0 accepted; ammo (22): types 19 / 27 / 28 accepted;
//   5. otherwise the equip-slot bit must be set;
//   6. primary (13) with the secondary filled: a two-hander (types 1 / 4 / 35) refused, two instruments (23..26)
//      refused;
//   7. secondary (14): a two-hander refused; a weapon needs Dual Wield (skill 22); with the primary filled,
//      refused when both are instruments or the primary is a two-hander.
bool can_item_go_in_equip_slot(std::int32_t slot, const EquipItem* item, bool slot_available, const EquipItem* primary,
                               const EquipItem* secondary, bool dual_wield);

// ---------------------------------------------------------------- use checks

// The refusal messages (token_string_to_client colour 0xd), printed when `tell` and the character's spawn is a
// player (+0x125 0).
inline constexpr std::uint32_t kEqstrCannotUseRace = 6856, kEqstrCannotUseDeity = 6857, kEqstrCannotUseLevel = 136,
                               kEqstrCannotEquipClass = 6611;

// The race bit (CharacterZoneClient_CanUseItemRace @0x446200): races below 13 are their own bit (as a signed
// number), Iksar 128 → 13, Vah Shir 130 → 14, Froglok 330 → 15, Drakkin 522 → 16, anything else 17.
std::int32_t item_race_bit_of(std::int32_t race);
// Whether a mask has bit `bit` (1 << ((bit - 1) & 31)).
bool item_mask_has_bit(std::uint32_t mask, std::int32_t bit);

// Each returns whether the item may be used; `message` gets the eqstr to print (0 for none).
bool can_use_item_race(std::int32_t race, std::uint32_t item_races, bool tell, bool spawn_is_player,
                       std::uint32_t* message);
// `has_deity` is the CharacterBase's +0x4c virtual object (the deity holder); without it, or with an item deity
// mask of 0, anyone. Deity 396 (agnostic) is bit 1, others deity - 199.
bool can_use_item_deity(bool has_deity, std::int32_t deity, std::uint32_t item_deities, bool tell, bool spawn_is_player,
                        std::uint32_t* message);
bool can_use_item_level(std::int32_t level, std::int32_t required_level, bool tell, bool spawn_is_player,
                        std::uint32_t* message);

// CharacterZoneClient::CanUseItem @0x44c430: true for a bag or book (item class != 0); else the usable classes must
// have one of `class_bits` (the original: 1 << ((class - 1) & 31) of the profile's class byte; NMS multiclass
// passes every class of the character); with `full` then deity, level, race (skipped for item type 20) and
// prestige, in that order, stopping at the first refusal. The checks are called only when reached.
bool can_use_item(std::int32_t item_class, std::uint32_t usable_classes, std::uint32_t class_bits, bool full,
                  std::int32_t item_type, const std::function<bool()>& deity_ok, const std::function<bool()>& level_ok,
                  const std::function<bool()>& race_ok, const std::function<bool()>& prestige_ok);

// ---------------------------------------------------------------- server OP_MoveItem

// An ItemGlobalIndex: i32 location (0 possessions, 1 bank, 2 shared bank, 3 trade, 4 world, 5 limbo), slot,
// bag slot (-1 none), augment slot.
struct GlobalIndex {
    std::int32_t location = 0;
    std::int16_t slot = -1, sub = -1, aug = -1;
};

// What Handle_OP_MoveItem_Apply @0x4c1ba0 does.
enum class MoveApply : std::uint8_t {
    NoItem,        // no item at `from`: false
    Delete,        // `to` outside the inventory, `from` in possessions / bank / limbo: the item is removed; true
    Refused,       // `to` outside the inventory, `from` elsewhere: false
    WrongLocation, // a valid `to`, but `from` or `to` not possessions: false (the dispatcher prints eqstr 1389)
    MergeCount,    // an item at `to` that `from` stacks with: ItemBaseContainer::MoveItemCount(from, to, count)
    AdjustCount,   // an empty `to` and a stackable source: ItemContainer_AdjustItemStackCount(from, count); its
                   // true result is the apply's failure
    Swap,          // otherwise the two slots are swapped; true
};
// The dispatcher's message for a false result: "MOVE ITEM CHARGE FAILED IN CLIENT APPLICATION." (colour 0xf).
inline constexpr std::uint32_t kEqstrMoveItemFailed = 1389;

// ItemGlobalIndex_IsValid @0x441a00 as the apply uses it: location 0..0x17 and slot != -1.
bool global_index_valid(const GlobalIndex& index);
// The plan for a server move; `stacks_with` is ItemBase_CanStackWith(from item, to item), `from_stackable`
// EQ_Item::IsStackable(from item); each is asked only where the original asks it.
MoveApply plan_move_item_apply(const GlobalIndex& from, const GlobalIndex& to, bool has_from_item, bool has_to_item,
                               const std::function<bool()>& stacks_with, const std::function<bool()>& from_stackable);
// After a Delete: whether CDisplay's cursor-item flag is cleared (`from` is the cursor itself, Possessions 33) and
// whether the local player's worn visual of the slot is reset (any possessions `from`).
bool move_apply_clears_cursor(const GlobalIndex& from);

// ItemBaseContainer::MoveItemCount @0x7e6230 with an item at `to` (the MergeCount case): a negative count, a
// source that is not stackable or holds fewer than `count`: false. Then ItemContainer_MergeStacks @0x7e4ea0 (the
// count itself is not used): the whole source stack when it fits in `to`'s room (max - count), else as much as
// fits; no room: false. Returns how many move (nullopt for false).
std::optional<std::int32_t> move_item_count(std::int32_t count, bool from_stackable, std::int32_t from_count,
                                            std::int32_t to_count, std::int32_t to_max);
// ItemContainer_AdjustItemStackCount @0x7e60a0 (the AdjustCount case): a stackable item (or a lore one with a
// count) gets count + delta, removed below 1; returns true (applied). Otherwise false.
struct StackAdjust {
    bool applied = false;
    bool remove = false;
    std::int32_t new_count = 0;
};
StackAdjust adjust_item_stack_count(bool stackable, bool lore, std::int32_t count, std::int32_t delta);

// ---------------------------------------------------------------- coin

// Money locations as MoveMoney numbers them, and coins 0 platinum .. 3 copper (the wire's coin types are 3 - coin).
inline constexpr std::int32_t kMoneyCursor = 0, kMoneyInventory = 1, kMoneyBank = 2, kMoneyTrade = 3,
                              kMoneySharedBank = 4;
inline constexpr std::int32_t kCoinCap = 1000000000;

// Everything CEverQuest::MoveMoney reads and writes. `coins[location][coin]`; `present[location]` false for a
// location the client has no counters for (the original reads a bad address there; the client refuses).
struct MoneyState {
    std::array<std::array<std::int32_t, 4>, 5> coins{};
    std::array<bool, 5> present{true, true, true, true, true};
    std::uint8_t cursor_coin_type = 0;  // CDisplay +0x11: 0 none, 1 platinum .. 4 copper
    std::int32_t attachment_type = 0;   // CCursorAttachment +0x224 (3 money)
    std::int32_t attachment_count = 0;  // +0x244
    std::uint8_t inventory_changed = 0; // PcClient +0x2dd4
};
struct MoneyEnv {
    bool has_local_pc = true;
    bool bank_window_active = false;     // a conversion needs the bank window
    bool cursor_attachment_free = true;  // CCursorAttachment::RemoveAttachment(3), asked for a move onto the cursor
    bool has_inventory_window = true;
};
// What one MoveMoney did beyond the counters: the calls the vectors list.
struct MoneyMoveResult {
    std::uint32_t value = 0;           // 1 done, 0 refused
    bool asked_bank_window = false;
    bool asked_cursor_free = false;
    bool played_sound = false;         // wave 0x8a, a move onto the cursor
    bool cleared_coin_buttons = false; // into the inventory with the window up
    bool send = false;                 // OP_MoveCoin {from, to, 3 - fromCoin, 3 - toCoin, amount}
    std::int32_t wire_from_coin = 0, wire_to_coin = 0;
};
// (int)10^exponent as the original computes it on the x87 (exponent = |toCoin - fromCoin| as int32): 1 .. 10^9;
// 10 and above overflow to INT_MIN; the one negative exponent (INT_MIN) gives 0.
std::int32_t coin_factor(std::int32_t exponent);
// CEverQuest::MoveMoney @0x51ad70 (from, to, fromCoin, toCoin, amount, quiet): amount 0, no PC: 0; a conversion
// needs the bank window; onto the cursor without `quiet` the cursor must be free (sound 0x8a then); no source, the
// same counter or more than the source holds: 0. To smaller coins the destination stays at most 10^9 and gets
// amount × factor; to bigger coins only amount − amount % factor moves (dst += amount / factor) and a cursor money
// attachment shows what remains. A positive amount sends OP_MoveCoin (the amount asked) and sets the
// inventory-changed flag. Last the cursor coin type is the first cursor counter above zero. 32-bit wrapping
// arithmetic, as the original.
MoneyMoveResult move_money(MoneyState& state, const MoneyEnv& env, std::int32_t from, std::int32_t to,
                           std::uint32_t from_coin, std::uint32_t to_coin, std::int32_t amount, std::uint8_t quiet);

// CInventoryWnd::ClickedMoneyButton(coin, amount) @0x692890 (coin 0 platinum .. 3 copper; a click passes -1, the
// quantity dialog's answer the amount).
struct MoneyButtonIn {
    std::int32_t coin = 0;
    std::int32_t amount = -1;
    std::uint8_t cursor_coin_type = 0;          // CDisplay +0x11
    std::array<std::int32_t, 4> cursor_coins{}; // the cursor's counters
    bool has_button = true;
    std::array<std::int32_t, 4> button_rect{};  // the button's screen rect: left, top, right, bottom
};
struct MoneyButtonOut {
    enum class Kind : std::uint8_t { DropCursorCoins, TakeAmount, AskQuantity } kind{};
    // DropCursorCoins / TakeAmount: MoveMoney(from, to, from_coin, to_coin, amount, 0), then the labels refresh.
    std::int32_t from = 0, to = 0;
    std::uint32_t from_coin = 0, to_coin = 0;
    std::int32_t amount = 0;
    // AskQuantity: +0x22c = coin; CQuantityWnd::Open(max = start = the inventory's coins of that type, x, y,
    // use_keys 1, min 0) (merchant page 2's CInvSlotMgr is told first).
    std::int32_t pending_coin = -1;
    std::int32_t x = 0, y = 0;
};
MoneyButtonOut clicked_money_button(const MoneyButtonIn& in);

} // namespace hailstone::game
