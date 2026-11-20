#pragma once

// RoF2 items on the wire: the recursive item serialization the server sends
// in OP_CharInventory, OP_ItemPacket and OP_ItemLinkResponse, the
// InventorySlot_Struct addressing, and the inventory, loot and item-link
// packets. Layouts follow the NMS server's EQEmu RoF2 translator
// (common/patches/rof2.cpp SerializeItem, ENCODE(OP_CharInventory),
// ENCODE(OP_ItemPacket), ENCODE/DECODE(OP_MoveItem), (OP_LootItem),
// DECODE(OP_ItemLinkClick); rof2_structs.h, all #pragma pack(1)) field by
// field; nothing is read by casting a struct. The client side of each packet
// follows the original client. Per-packet notes are in
// docs/protocol/rof2-packets.md section "Items".
//
// OpenEQ crates/openeq-net/src/inventory.rs (MIT OR Apache-2.0) was used to
// cross-check the block sizes (THIRD_PARTY.md); the code follows rof2.cpp.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/byte_io.h"
#include "net/wire/app_packet.h"

#include "net/messages/item.h"
#include "net/wire/item_serialization.h"

namespace hailstone::net {

// ---------------------------------------------------------------- slots

inline constexpr std::size_t kItemSlotSize = 12;

void write_slot(ByteWriter& w, const ItemSlot& s);
ItemSlot read_slot(ByteReader& r);



// ---------------------------------------------------------------- items



AppPacket encode(const EvolvingFinalResult& v);

std::optional<EvolvingItemUpdate> decode_evolving_item_update(const AppPacket& p);





// OP_CharInventory (zone → client at zone-in): u32 count, then count items
// (ENCODE(OP_CharInventory); an empty inventory is 4 zero bytes). nullopt
// unless the payload is consumed exactly.
std::optional<std::vector<Item>> decode_char_inventory(const AppPacket& p);
AppPacket encode_char_inventory(const std::vector<Item>& items); // server direction, for tests

std::optional<ItemPacket> decode_item_packet(const AppPacket& p);
AppPacket encode_item_packet(bool link_response, const ItemPacket& v); // server direction, for tests

// OP_ItemPreviewRequest (C->S, 80 bytes; Send_OP_ItemPreviewRequest @0x6fff10 / @0x6ffc20): u32 item id, then for
// an item with an ornamentation (0x7b0700: either IT string, the ornament icon, unknowna1 >= 0, the hero's
// forge model or unknown063 set) char[30] ornament_main (+4), char[30] ornament_off (+0x22), unknowna1 (+0x40),
// ornament_hero_model (+0x44), unknown063 (+0x48) and the byte +0x4d = 1; otherwise empty strings, -1, 0, 0, 0.
// EQEmu reads only the id. Without `item` (the id-only sender @0x6ffc20) the defaults.
AppPacket encode_item_preview_request(std::uint32_t item_id, const Item* item = nullptr);
std::optional<ItemPreviewReply> decode_item_preview(const AppPacket& p);
AppPacket encode(const ItemPreviewReply& v); // server direction, for tests

// ---------------------------------------------------------------- moves

inline constexpr std::size_t kMoveItemSize = 28;
AppPacket encode(const MoveItem& v);                        // OP_MoveItem
AppPacket encode_delete_item(const MoveItem& v);            // OP_DeleteItem
std::optional<MoveItem> decode_move_item(const AppPacket& p); // either opcode

inline constexpr std::size_t kMultiMoveEntrySize = 0x24;
std::optional<AppPacket> encode_move_multiple(const std::vector<MultiMoveEntry>& moves);

// ---------------------------------------------------------------- loot

// OP_LootRequest (client → zone, u32 corpse spawn id; the server echoes it
// after the items) and OP_EndLootRequest (u32 corpse id; answered with an
// empty OP_LootComplete).
AppPacket encode_loot_request(std::uint32_t corpse_id);
AppPacket encode_end_loot_request(std::uint32_t corpse_id);
std::optional<std::uint32_t> decode_loot_request(const AppPacket& p);

inline constexpr std::size_t kLootItemSize = 20;
AppPacket encode(const LootItem& v);
std::optional<LootItem> decode_loot_item(const AppPacket& p);

inline constexpr std::size_t kMoneyOnCorpseSize = 20;
AppPacket encode(const MoneyOnCorpse& v); // server direction, for tests
std::optional<MoneyOnCorpse> decode_money_on_corpse(const AppPacket& p);

// Opcode 0x3ed4 (both directions; no name in EQEmu's RoF2 table): {platinum,
// gold, silver, copper} as int32s. The server's adds coin the client credits
// to the purse in CEverQuest_ProcessCursorQueue @0x5321a0, which sends the
// same four values back.
inline constexpr std::uint16_t kOpSplitCoin = 0x3ed4;
AppPacket encode_split_coin(const Coins& v);
std::optional<Coins> decode_split_coin(const AppPacket& p); // the opcode, and at least 16 bytes
std::optional<Coins> decode_money_update(const AppPacket& p);
AppPacket encode_money_update(const Coins& v); // server direction, for tests

// ---------------------------------------------------------------- item links

inline constexpr std::size_t kItemLinkClickSize = 52;
AppPacket encode(const ItemLinkClick& v);
std::optional<ItemLinkClick> decode_item_link_click(const AppPacket& p);

inline constexpr std::size_t kItemLinkRequestSize = 0x48;
AppPacket encode(const ItemLinkRequest& v);


std::optional<CoinReward> decode_quest_reward(const AppPacket& p);
std::optional<CoinReward> decode_cash_reward(const AppPacket& p);

AppPacket encode_consume(const ItemSlot& slot, bool automatic, bool drink);
// OP_Consume as Handle_OP_ItemVerifyReply @0x4bf800 sends it (a right click on food or drink): auto_consumed
// 0xffffffff, type 1 food / 2 drink, and a last dword whose low byte is 1 (its upper bytes are stack left-overs in
// the original; 0 here).
AppPacket encode_consume_reply(const ItemSlot& slot, bool drink);

// ---------------------------------------------------------------- using items


// OP_ItemVerifyRequest (C→Z, 16 bytes; CInvSlot::HandleRButtonUp @0x697250, Cmd_useitem @0x479200,
// PcZoneClient_UsePotionBeltSlot): the item's ItemGlobalIndex (12 bytes) and the target's spawn id (0: none).
inline constexpr std::size_t kItemVerifyRequestSize = 16;
AppPacket encode_item_verify_request(const ItemSlot& slot, std::uint32_t target_id);

inline constexpr std::size_t kItemVerifyReplySize = 20;
std::optional<ItemVerifyReply> decode_item_verify_reply(const AppPacket& p);
AppPacket encode(const ItemVerifyReply& v); // server direction, for tests

inline constexpr std::size_t kApplyPoisonSize = 12;
AppPacket encode(const ApplyPoison& v);
std::optional<ApplyPoison> decode_apply_poison(const AppPacket& p);

} // namespace hailstone::net
