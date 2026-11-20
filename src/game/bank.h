#pragma once

// The bank as the original client keeps it (CBankWnd) and the rules it checks before an item goes into or comes out
// of the bank and the shared bank (CInvSlot::HandleLButtonUp @0x695670, the
// put-down branch for Location 1 and 2).
// Nothing here does networking; net/connection/game_connection_bank.cpp sends the packets.

#include <cstdint>
#include <optional>
#include <string>

#include "game/inventory.h"
#include "net/messages/item.h"

namespace hailstone::game {

// Bank slot counts. CDisplay builds CBankWnd from "BigBankWnd" (24 slots,
// BIGB_BankSlot0..23) when the account has expansion bit 10 (0x400,
// PcProfile Expansions >> 10 & 1 → the big-bank flag at 0xde09ca) or any of bank slots
// 16..23 holds an item, else from "BankWnd" (16 slots); CBankWnd::BankSize
// = 16 + 8 × big. Two shared bank slots (BW_SharedBankSlot0..1).
inline constexpr int kBankSlotsSmall = 16, kBankSlotsBig = 24, kSharedBankSlots = 2;
int bank_slot_count(std::uint32_t expansions, const Inventory& inv);

// Coin in the bank: PcProfile's bank coin and the shared bank's (only
// platinum is kept by EQEmu). The server sends them once, in the player
// profile; afterwards the client keeps them itself (MoveMoney adjusts them
// locally; OP_MoneyUpdate only carries the coin on the person).
struct BankCoins {
    std::uint32_t platinum = 0, gold = 0, silver = 0, copper = 0;
    std::uint32_t shared_platinum = 0;
    // By wire coin type (0 copper .. 3 platinum); `shared` picks the shared bank.
    std::uint32_t* of(int type, bool shared);
};

// CEverQuest::MoveMoney itself is game::move_money (game/item_moves.h).

// ---- item rules for the bank

// ItemBase_IsNoStorage(recursive) @0x7ba990: No Storage (no_transfer), or
// (common items and bags only) any content that is.
bool is_no_storage(const net::Item& item, bool contents);
// ItemBase_IsNoRent(recursive) @0x7bc670: NoRent != 0 (not Temporary), and
// for common items and bags, no Temporary content.
bool is_permanent(const net::Item& item, bool contents);
// ItemBase_IsNoTradeNonHeirloom(recursive) @0x7b1f90: not Heirloom and not
// droppable (CanDrop), or any content that is.
bool is_no_trade_non_heirloom(const net::Item& item, bool contents);
// ItemDefinition_SharesLoreWith @0x7f6810: a's Lore > 0: the same lore
// group; < 0: the same item id; 0: never.
bool shares_lore(const net::Item& a, const net::Item& b);

// A refused move: the eqstr and its %2 ("" when unused). eqstr 0 = allowed.
struct Refusal {
    std::uint32_t eqstr = 0;
    std::string arg;
    explicit operator bool() const { return eqstr != 0; }
};

// Putting `item` into a bank slot: No Storage refuses (6191 the item, 5301 a
// bag's content, 5302 an augment).
Refusal bank_refusal(const net::Item& item);
// Putting `item` into a shared bank slot, in the original's order:
// PcBase_SharedBankHasLoreConflict @0x7c4e90 against the shared bank's items
// and their bag contents (3194 / 5307 / 5308), then No Trade and
// non-Heirloom, Temporary and No Storage (3195 "%2" for the item itself,
// 5303 a bag's content, 5304 an augment).
Refusal shared_bank_refusal(const net::Item& item, const Inventory& inv);
// Taking `item` out of the shared bank (and swapping it out for the cursor
// item): PcClient::HasLoreItem @0x57ae30 — a lore item the player already has
// on him, in the bank, limbo, alt storage, mail or real estate (379 the item,
// 5305 a bag's content, 5306 an augment). Items in the shared bank itself and
// archived items do not count.
Refusal lore_pickup_refusal(const net::Item& item, const Inventory& inv);

} // namespace hailstone::game
