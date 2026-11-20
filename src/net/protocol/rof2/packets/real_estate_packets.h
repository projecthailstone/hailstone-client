#pragma once

// Real estate (player housing, guild halls, placed furniture): the packets RealEstateManagerClient_HandleMessage
// @0x5b2500 reads and the windows send (checked against
// 0x5b2500, 0x5b7a90, 0x5b0f80, 0x74ab50, 0x74c4c0, 0x5abd20 and ConfirmationDialog_HandleResponse's 0x89).
// None of these are in EQEmu's RoF2 table: a stock EQEmu server never sends them, a server with housing (Project
// Triune) does, with the original's numbers.
//
// Two string forms: records use a u32 length then the bytes (SerializeBuffer_ReadCXStr @0x80c9b0, no NUL); placed
// objects, escrow records and fixed bodies use NUL-terminated strings. The original reads fail-open (a short packet
// leaves defaults and misreads what follows, a missing count can crash it); these decoders refuse a short packet
// instead (a deliberate difference: nothing half-read reaches the game state).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "net/protocol/rof2/packets/item_packets.h"
#include "net/wire/app_packet.h"

#include "net/messages/real_estate.h"

namespace hailstone::net {

// Server → client, through the real estate dispatcher (0x5b2500).
inline constexpr std::uint16_t kOpReNop1 = 0x0351, kOpReNop2 = 0x3d94; // explicit no-ops
inline constexpr std::uint16_t kOpReItemSummary = 0x087f;    // u32 re, str name, rows
inline constexpr std::uint16_t kOpReItemRows = 0x1cef;       // u32 re, rows
inline constexpr std::uint16_t kOpReItemSummaryUpdate = 0x1097, kOpReItemRowAdd = 0x2cf7; // 0x5afe00
inline constexpr std::uint16_t kOpReItemSummaryRename = 0x1967;
inline constexpr std::uint16_t kOpReItemRemoved = 0x1baf;
inline constexpr std::uint16_t kOpReItemSummaryForget = 0x7d28;
inline constexpr std::uint16_t kOpReResult = 0x1228, kOpReSellResult = 0x66bf, kOpReTransferResult = 0x68ba;
inline constexpr std::uint16_t kOpReSearch = 0x2691;         // both directions
inline constexpr std::uint16_t kOpReCatalog = 0x2698;
inline constexpr std::uint16_t kOpReRecord = 0x63fd, kOpReSetCurrent = 0x6fd0, kOpReRemove = 0x4f09;
inline constexpr std::uint16_t kOpReAccess = 0x3968, kOpReAccessUpdate = 0x485d;
inline constexpr std::uint16_t kOpRePurchase = 0x45be;       // C→S buy; S→C u8 ok (shows the manage window)
inline constexpr std::uint16_t kOpReEscrow = 0x2aa6;         // both directions
inline constexpr std::uint16_t kOpReOwnedA = 0x2aa7, kOpReOwnedB = 0x3e00, kOpReOwnedC = 0x66df;
inline constexpr std::uint16_t kOpReOwnedAToggle = 0x76c6, kOpReOwnedBToggle = 0x0c13;
inline constexpr std::uint16_t kOpReNpcItem = 0x21ea;
inline constexpr std::uint16_t kOpReContextMenu = 0x21ec;    // both directions, 768-byte body
inline constexpr std::uint16_t kOpReInspect = 0x58c7;        // C→S {re, item}; S→C an item
inline constexpr std::uint16_t kOpReOffer = 0x5a63;          // both directions, 0xbc-byte body
// Server → client, own cases of HandleWorldMessage.
inline constexpr std::uint16_t kOpRePlacedItem = 0x1045, kOpRePlacedItemRemove = 0x2a9e;
inline constexpr std::uint16_t kOpReAreas = 0x49df;
inline constexpr std::uint16_t kOpRePlace = 0x626e;          // C→S request; S→C reply
inline constexpr std::uint16_t kOpRePlotSearchOpen = 0x2b35;
// Client → server only.
inline constexpr std::uint16_t kOpReTravel = 0x218c, kOpRePlotSelect = 0x796c, kOpReSell = 0x792c,
                               kOpReTransfer = 0x507f, kOpReOptOut = 0x6573, kOpReAccessSave = 0x3146,
                               kOpRePlaceConfirm = 0x711a, kOpReToStorage = 0x3424, kOpReToInventory = 0x2036,
                               kOpReStoreAll = 0x6917, kOpRePackCrate = 0x613d,
                               kOpReLeave = 0x1da9; // /leaverealestate
// 0x06c8 is not real estate's: its one sender is CContainerMgr opening a container.


ReRecord read_re_record(ByteReader& r);
void write_re_record(ByteWriter& w, const ReRecord& v);
std::optional<ReRecord> decode_re_record(const AppPacket& p);
AppPacket encode_re_record(std::uint16_t op, const ReRecord& v); // server direction, for tests

std::optional<ReCatalog> decode_re_catalog(const AppPacket& p);
AppPacket encode(const ReCatalog& v); // server direction, for tests

std::optional<ReAccessMessage> decode_re_access(const AppPacket& p);
AppPacket encode(std::uint16_t op, const ReAccessMessage& v); // server direction, for tests

ReItemRow read_re_item_row(ByteReader& r);
void write_re_item_row(ByteWriter& w, const ReItemRow& v);
std::optional<ReItemList> decode_re_item_list(const AppPacket& p); // 0x087f / 0x1cef / 0x2cf7
AppPacket encode(std::uint16_t op, const ReItemList& v);           // server direction, for tests
std::optional<ReRename> decode_re_rename(const AppPacket& p);
std::optional<ReItemRef> decode_re_item_removed(const AppPacket& p);
std::optional<ReItemRef> decode_re_placed_item_remove(const AppPacket& p);

// One dword: 0x1228, 0x66bf, 0x68ba, 0x7d28, 0x2b35 (a byte).
std::optional<std::int32_t> decode_re_dword(const AppPacket& p);

// 0x2aa7 / 0x3e00 / 0x66df: u32 n, n × u32 (the owned-estate lists A / B / C, replaced). 0x76c6 / 0x0c13: u32 id,
// the byte at +0x68 (1 adds to list A / B, 0 removes).
std::optional<std::vector<std::uint32_t>> decode_re_id_list(const AppPacket& p);
AppPacket encode_re_id_list(std::uint16_t op, const std::vector<std::uint32_t>& ids); // server direction, for tests
std::optional<ReListToggle> decode_re_list_toggle(const AppPacket& p);

std::optional<ReSearchReply> decode_re_search_reply(const AppPacket& p);
AppPacket encode(const ReSearchReply& v); // server direction, for tests

std::optional<ReEscrow> decode_re_escrow(const AppPacket& p);
AppPacket encode(const ReEscrow& v);

// The 0xbc-byte transaction body of 0x45be / 0x792c / 0x507f and the 0x5a63 offer: u32 re, u32 a, u32 b,
// char[64] at +0x0c, char[32] at +0x4c, char[64] at +0x6c, u32 +0xac, +0xb0, +0xb4, u8 +0xb8, +0xb9, +0xba.
inline constexpr std::size_t kReTransactionSize = 0xbc;
std::optional<ReTransaction> decode_re_transaction(const AppPacket& p);
AppPacket encode(std::uint16_t op, const ReTransaction& v);

RePlacedItem read_re_placed_item(ByteReader& r);
void write_re_placed_item(ByteWriter& w, const RePlacedItem& v);
std::optional<RePlacedItem> decode_re_placed_item(const AppPacket& p);
AppPacket encode(const RePlacedItem& v); // server direction, for tests

std::optional<std::vector<ReArea>> decode_re_areas(const AppPacket& p);
AppPacket encode(const std::vector<ReArea>& v); // server direction, for tests

std::optional<ReNpcItem> decode_re_npc_item(const AppPacket& p);

// 0x21ec (768 bytes both ways). The request (0x542a90, from the items window's right click on a row): char[18]
// key (the item's 16-digit serial) at +0, a slot {-1, -1, 0xffff} at +0x14, +0x20 re, +0x24 item, +0x28 0, the name
// zeroed, 40 entries 0xff-filled, u8 1 at +0x2ec, the mouse's x, y at +0x2f0 / +0x2f4, the item's +0xec at +0x2f8.
// The reply (CRealEstateItemsWnd_ShowItemContextMenu @0x7375f0): the same, +0x2c cstr name, 40 entries at +0x6c +
// 0x10 i {i32 type (-1 unused), i32 parameter, u32 text (an eqstr; types 9 and 0xa a dbstr of kind 0x1c), i32 the
// sub-menu's parameter}; with +0x2ec set types 3, 4, 0xb, 0xe and type 2 with parameter 1 are left out. A chosen
// entry's type picks what the window does (CRealEstateItemsWnd::WndNotification's 0x65 + index arms).
// A right click in the world (CEverQuest::RMouseUp on a placed object or a real estate NPC item) calls the same
// builder with the empty key (sixteen '0'), +0x2ec 0 and +0x2f8 -1.
inline constexpr std::size_t kReMenuSize = 768;
std::optional<ReMenu> decode_re_menu(const AppPacket& p);
AppPacket encode(const ReMenu& v); // the reply (server direction, for tests)
AppPacket encode_re_menu_request(std::string_view key, std::uint32_t re, std::uint32_t item, std::int32_t x,
                                 std::int32_t y, std::uint32_t f2f8);
AppPacket encode_re_menu_request(std::uint32_t re, std::uint32_t item, std::int32_t x, std::int32_t y);
// The menu's other requests (WndNotification's arms): types 8 / 9 / 0x13 send {re, item, the entry's parameter} on
// 0x050c / 0x51e3 / 0x64a3; type 5 sends 0x7ad1 {re, item, 140 zero bytes}. Their meaning is not known.
inline constexpr std::uint16_t kOpReMenu8 = 0x050c, kOpReMenu9 = 0x51e3, kOpReMenu13 = 0x64a3, kOpReMenu5 = 0x7ad1;
AppPacket encode_re_item_op(std::uint16_t op, std::uint32_t re, std::uint32_t item, std::uint32_t param);
AppPacket encode_re_menu5(std::uint32_t re, std::uint32_t item);

// 0x58c7 (S→C): an item to inspect, serialized as in OP_ItemPacket after its type.
std::optional<Item> decode_re_inspect(const AppPacket& p);

// Client → server.
// 0x2691: subtype (0 my plots / search, 1 my items, 2 a neighborhood row, 4 the plot search window), id, 0, char[80]
// at +0x0c, char[64] at +0x5c (the plot search's name: 0x74b920), char[64] at +0x9c, u8 flag +0xdc, u8 1
// +0xdd, 2 bytes of padding (224 bytes).
AppPacket encode_re_search(std::uint32_t subtype, std::uint32_t id, std::string_view s0 = {}, std::string_view s1 = {},
                           std::string_view s2 = {}, std::uint8_t flag = 0);
// 0x218c: {u32 id, u32 secondary (-1 none), u32 0}: travel to a neighborhood, or into a plot's house.
AppPacket encode_re_travel(std::uint32_t id, std::uint32_t secondary);
AppPacket encode_re_plot_select(std::uint32_t plot); // 0x796c
// 0x45be (buy): re at +0, the price key at +0xac, the upkeep key at +0xb0, +0xb4 the byte at 0xdd5d00 (0 here).
AppPacket encode_re_purchase(std::uint32_t re, std::uint32_t price_key, std::uint32_t upkeep_key);
AppPacket encode_re_sell(std::uint32_t re);                                 // 0x792c {re, -1, -1}
AppPacket encode_re_transfer(std::uint32_t re, std::string_view target);    // 0x507f, the name at +0x6c
AppPacket encode_re_opt_out(std::uint32_t re, bool opt_out);                // 0x6573
AppPacket encode_re_escrow(ReEscrowAction action, std::uint32_t re, std::vector<ReEscrow::Entry> entries = {});
// 0x3146: one estate's access table (RealEstateAccess::Serialize): u32 re then the table.
AppPacket encode_re_access_save(std::uint32_t re, const ReAccess& access);
// 0x3424 / 0x2036 (move to storage / inventory; 0x73f6c0 / 0x542470): char[18] key (the item's serial) at
// +0, a slot {-1, -1, 0xffff} at +0x14, +0x20 re, +0x24 item, +0x28 0, and for storage the destination at +0x2c: the
// container index of the estate's type (0x7f5ec0: 2 a house (closet), 3 a plot (shed), 6 a crate). 48 / 44 bytes.
AppPacket encode_re_to_storage(std::string_view key, std::uint32_t re, std::uint32_t item, std::uint32_t destination);
AppPacket encode_re_to_inventory(std::string_view key, std::uint32_t re, std::uint32_t item);
AppPacket encode_re_store_all();          // 0x6917
AppPacket encode_re_pack_crate(bool pack); // 0x613d
AppPacket encode_re_inspect(std::uint32_t re, std::uint32_t item); // 0x58c7
AppPacket encode(const RePlaceRequest& v);
std::optional<RePlaceRequest> decode_re_place_request(const AppPacket& p); // server side, for tests
// The cursor's item at slot 33 (the old form; the items window's own row uses RePlaceRequest).
AppPacket encode_re_place_request(std::string_view key, std::uint16_t slot, std::int32_t x, std::int32_t y);
std::optional<RePlaceReply> decode_re_place_reply(const AppPacket& p);
AppPacket encode(const RePlaceReply& v); // server direction, for tests
// 0x711a (96 bytes; Send_Opcode_0x711a @0x5b1f90 → RealEstate::SerializeConfirm @0x5b3fb0): +0x00 the key (18
// bytes), +0x14 the item index (12 bytes), +0x20 re, +0x24 item, +0x28 0, +0x2c heading, pitch, roll, scale, x,
// y, z (f32), +0x48 / +0x54 the actor's bounding box (0x4907f0: the second vec3, then the first).
AppPacket encode_re_place_confirm(const RePlacedItem& v, const ReItemIndex& index = {}, const glm::vec3& box_a = {},
                                  const glm::vec3& box_b = {});
std::optional<RePlaceConfirm> decode_re_place_confirm(const AppPacket& p); // server side, for tests
// 0x6213 (S→C, any body; HandleWorldMessage): ItemPlacement_Stop, the placement cancelled.
inline constexpr std::uint16_t kOpRePlaceStop = 0x6213;

} // namespace hailstone::net
