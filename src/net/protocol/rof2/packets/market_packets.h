#pragma once

// The bazaar: trader mode (OP_Trader, OP_BecomeTrader), shopping at a trader
// and the bazaar search (OP_TraderShop, OP_BazaarSearch, OP_TraderBulkSend,
// OP_TraderDelItem), and the barter hall's buyer packets (OP_Barter,
// OP_BuyerItems). RoF2 has no OP_TraderBuy of its own: purchases travel as
// OP_TraderShop action 10. Layouts follow the original's senders and readers
// (CBazaarWnd, CBazaarSearchWnd, CBazaarConfirmationWnd, CMerchantWnd and
// HandleWorldMessage_Bazaar @0x4ba870 in eqgame.exe) and the NMS
// server's EQEmu RoF2 translator (common/patches/rof2.cpp ENCODE/DECODE of
// these opcodes, rof2_structs.h RoF2BazaarTraderBuyerActions, zone/
// trading.cpp). Per-packet notes: docs/protocol/rof2-packets.md "Bazaar".

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/protocol/rof2/opcode_map.h"
#include "net/wire/app_packet.h"

#include "net/messages/market.h"

namespace hailstone::net {


inline constexpr std::size_t kItemGuidSize = 18;  // EqItemGuid: 17 characters and a NUL

// ---------------------------------------------------------------- trader mode (OP_Trader)

inline constexpr std::size_t kTraderBeginSize = 4404;
AppPacket encode(const TraderBegin& v);
std::optional<TraderBegin> decode_trader_begin(const AppPacket& p);

// A 12-byte {action, 0, 0}: action 2 ends trader mode (Send_OP_Trader_00638030
// @0x638030), 11 asks for the trader's own listing (after TraderAck2).
AppPacket encode_trader_action(std::uint32_t action);

inline constexpr std::size_t kTraderPriceSize = 32;
AppPacket encode(const TraderPrice& v);
std::optional<TraderPrice> decode_trader_price(const AppPacket& p);

inline constexpr std::size_t kTraderBuySize = 284;
AppPacket encode(const TraderBuy& v, rof2::Op op = rof2::Op::TraderShop);
std::optional<TraderBuy> decode_trader_buy(const AppPacket& p);

std::optional<TraderMessage> decode_trader_message(const AppPacket& p);
// Server direction encoders, for tests.
AppPacket encode_trader_status(std::uint32_t action, std::uint32_t word1, std::uint32_t word2 = 0);
AppPacket encode_trader_listing(const std::vector<std::pair<std::string, std::uint32_t>>& listing);

AppPacket encode(const BecomeTrader& v);
std::optional<BecomeTrader> decode_become_trader(const AppPacket& p);

// ---------------------------------------------------------------- shopping (OP_TraderShop)

AppPacket encode(const TraderClick& v);
std::optional<TraderClick> decode_trader_click(const AppPacket& p);
// CMerchantWnd::AboutToHide @0x6efe60 at a trader: OP_TraderShop with action 4 only (4 bytes).
AppPacket encode_trader_shop_end();
// A 12-byte {action, 0, 0} on OP_TraderShop: 26 the bazaar search window's
// first opening (CBazaarSearchWnd::AboutToShow), 9 its welcome button.
AppPacket encode_trader_shop_action(std::uint32_t action);

inline constexpr std::size_t kBazaarQuerySize = 132;
AppPacket encode(const BazaarQuery& v);
std::optional<BazaarQuery> decode_bazaar_query(const AppPacket& p);

inline constexpr std::size_t kBazaarInspectSize = 40;
AppPacket encode(const BazaarInspect& v);
std::optional<BazaarInspect> decode_bazaar_inspect(const AppPacket& p);

std::optional<TraderShopMessage> decode_trader_shop(const AppPacket& p);
AppPacket encode_bazaar_welcome(std::uint32_t traders, std::uint32_t items); // server direction, for tests
AppPacket encode_bazaar_add_trader(const BazaarTrader& t);                   // server direction, for tests

// OP_TraderBulkSend (CBazaarSearchWnd_HandleTraderList @0x635890): u32 count,
// then {u16 zone, u16 instance, u32 character id, u32 spawn id, name}.
std::optional<std::vector<BazaarTrader>> decode_trader_bulk(const AppPacket& p);
AppPacket encode_trader_bulk(const std::vector<BazaarTrader>& v); // server direction, for tests

std::optional<BazaarResults> decode_bazaar_results(const AppPacket& p);
AppPacket encode(const BazaarResults& v); // server direction, for tests

std::optional<TraderDelItem> decode_trader_del_item(const AppPacket& p);
AppPacket encode(const TraderDelItem& v); // server direction, for tests

// Opcode 0x0e44 {u32 spawn id} (not in EQEmu's RoF2 table): CBazaarSearchWnd's Find Trader and CBarterSearchWnd's
// Find (@0x635990 / @0x62d810, 6 bytes with the opcode), before the find path to that spawn.
inline constexpr std::uint16_t kOpFindMarketSpawn = 0x0e44;
AppPacket encode_find_market_spawn(std::uint32_t spawn_id);

// ---------------------------------------------------------------- barter (buyers)
//
// The original's senders and readers: Handle_OP_Barter @0x4222d0, Handle_OP_BuyerItems @0x422d60, the barter
// manager's senders (Send_OP_Barter_00420eb0 / 00420ff0 / 00421470 / 00421d20, Send_OP_BuyerItems_004229a0 /
// 00422b70) and CBarterWnd, CBarterSearchWnd and CBarterMerchantWnd's, checked against EQEmu's RoF2 translator
// (rof2.cpp ENCODE / DECODE of OP_Barter and OP_BuyerItems, zone/trading.cpp). Where the two differ the note says so.



// RightClickedOnPlayer @0x536d80 on a buyer: OP_Barter {11, spawn id, one byte 0 and three uninitialised}
// (12 bytes). EQEmu echoes it with the approval at +8 (ShowBuyLines: 0 when the buyer is busy) and sends the
// buyer's lines as OP_BuyerItems 14. Handle_OP_Barter 11 opens the barter merchant window on a nonzero approval and
// closes it on 0. The window's close (Send_OP_Barter_00420eb0) sends {12, spawn id, 4 uninitialised bytes}.
AppPacket encode_barter_inspect(std::uint32_t action, std::uint32_t buyer_id);
std::optional<BarterInspectReply> decode_barter_inspect_reply(const AppPacket& p);

// OP_BuyerItems 14 {14, u32, the line} (a viewed buyer's line, CBarterMerchantWnd_UpdateBuyLine) and 7 {7, u32,
// the line} (our own line, 0x422970). EQEmu writes 14 as {14, slot, slot, line} and 7 as {7, 0, line}; its
// removals {7 / 14, 0, slot, 0} read as a line that is not enabled.
std::optional<BuyLine> decode_buy_line(const AppPacket& p);
AppPacket encode_buy_line(std::uint32_t action, const BuyLine& v); // server direction (EQEmu's layout), for tests

std::optional<BuyerAppearance> decode_buyer_appearance(const AppPacket& p);
AppPacket encode_buyer_appearance(const BuyerAppearance& v); // server direction, for tests


// OP_BuyerItems 9 BuyerSellItem, client → zone: {u32 9, the record}. CBarterMerchantWnd::WndNotification (the
// quantity dialog's answer) fills method 0, buyer spawn id and the line copied from the merchant's list;
// CBarterSearchWnd::sub_0062cf70 method 1 / 2, the line's buyer spawn id (+0x6c) and character id (+0x70) and the
// line with its buyer fields. Both leave the strings empty and put the quantity at +0x170. EQEmu's DECODE reads the
// same bytes: method, 4 B, buyer spawn id, buyer character id, 11 B (+0x10, +0x14 and the three empty strings), the
// line (with method 0, 13 bytes for the buyer fields), the quantity, 4 B.
AppPacket encode(const BuyerSale& v);
// The record after the action (the original's 0x423500), for a sale and its result.
std::optional<BuyerSale> decode_buyer_sale(const AppPacket& p);
std::optional<BuyerSaleResult> decode_buyer_sale_result(const AppPacket& p);
// Server direction (EQEmu's ENCODE of Barter_SellerTransactionComplete / BuyerTransactionComplete), for tests.
AppPacket encode_buyer_sale_result(std::uint32_t action, std::uint32_t sub_action, std::uint32_t error,
                                   const std::string& buyer, const std::string& item, const std::string& seller,
                                   std::uint32_t cost, const std::vector<BuyLineTrade>& trade, std::uint32_t quantity);

inline constexpr std::size_t kBarterSearchSize = 84;
AppPacket encode(const BarterSearch& v);

std::optional<BarterSearchResults> decode_barter_search_results(const AppPacket& p);
AppPacket encode_barter_search_results(const BarterSearchResults& v); // server direction, for tests

// OP_Barter {2} / {3}, 4 bytes, both ways. Send_OP_Barter_00420ff0 asks for buyer mode on (2) or off (3) when it
// differs from the barter manager's flag (+0x8fc0); asking for off clears the flag at once. The server's {2}
// (Handle_OP_Barter 2) sets the flag, sends the welcome text (0x13) and the active lines (OP_BuyerItems 6); {3}
// clears it.
AppPacket encode_buyer_mode(bool on);
std::optional<bool> decode_buyer_mode(const AppPacket& p);

// OP_Barter {0, char[64] text} (68 bytes, Send_OP_Barter_00631750): the items whose name holds the text, for a new
// buy line. The answer (Handle_OP_Barter 0): {0, u32 count, count × 76 B {char[64] name, u32 item id, u32 icon,
// u32}} (CBarterWnd_AddSearchResultItem).
AppPacket encode_buyer_item_search(const std::string& text);
std::optional<std::vector<BuyerItemResult>> decode_buyer_item_results(const AppPacket& p);
AppPacket encode_buyer_item_results(const std::vector<BuyerItemResult>& v); // server direction, for tests

// OP_BuyerItems 6 (Handle_OP_Barter 2): {u32 6, u16 count, count × line} with the lines that are enabled and
// active; 7 (Send_OP_BuyerItems_004229a0 / 00422b70): {u32 7, line}. Our lines carry zero buyer fields (13 bytes).
// The server answers each with OP_BuyerItems 7.
AppPacket encode_buy_lines(const std::vector<BuyLine>& lines);
AppPacket encode_modify_buy_line(const BuyLine& line);

// OP_Barter 8 (Send_OP_Barter_00421470, 8 bytes): {8, slot}. EQEmu reads the slot at +4 and echoes it as
// OP_BuyerItems {7, 0, slot, 0}, which empties our line; Handle_OP_Barter 8 {8, slot} empties it too.
AppPacket encode_remove_buy_line(std::uint32_t slot);
std::optional<std::uint32_t> decode_remove_buy_line(const AppPacket& p);

// The action word of an OP_Barter / OP_BuyerItems packet, or nullopt when it is shorter than one.
std::optional<std::uint32_t> barter_action(const AppPacket& p);

// OP_Barter 0x13 (Send_OP_Barter_00421d20 and Handle_OP_Barter 2, 328 bytes): {u32 0x13, u32 0, u8 0, 63 B, char[256]
// message at +0x48}; RoF2 BuyerWelcomeMessageUpdate_Struct has the message at +72 too.
AppPacket encode_buyer_welcome(const std::string& message);

// OP_Barter 0x11 (CBarterSearchWnd's Welcome, 4 bytes): the barter hall's welcome; EQEmu answers with a chat line.
// OP_Barter 0x17 (CBarterSearchWnd::AboutToShow, 4 bytes, once per zone): the buyers for the player combo; EQEmu
// answers with OP_Barter 0x1a per buyer.
AppPacket encode_barter_action(std::uint32_t action);
// OP_Barter 0x14 (CBarterSearchWnd's Greeting, 8 bytes): {0x14, the line's buyer character id}.
AppPacket encode_buyer_greeting(std::uint32_t buyer_id);

AppPacket encode(const BarterItemInspect& v);
// OP_Barter 0x15 (CBarterWnd, a right click on ItemSearchList, 12 bytes): {0x15, item id, 2 with Shift else 0}.
AppPacket encode_barter_search_item_inspect(std::uint32_t item_id, bool shift);

// OP_Barter 0x10 (Handle_OP_Barter: eqstr 6060 "%1 is browsing your Buy Lines."): {0x10, char[64] name}.
std::optional<std::string> decode_buyer_browsing(const AppPacket& p);
std::optional<BuyerGreeting> decode_buyer_greeting(const AppPacket& p);

std::optional<BarterBuyer> decode_barter_add_buyer(const AppPacket& p);
std::optional<std::uint32_t> decode_barter_remove_buyer(const AppPacket& p);
std::optional<std::vector<BarterBuyer>> decode_barter_buyer_list(const AppPacket& p);

// OP_BuyerItems 5 (BarterBuyLines_ParseFromServer @0x422110): {5, a string "<count>$<record>$..."}, each record the
// BART ini's line format (game/barter.h). EQEmu never sends it.
std::optional<std::string> decode_buy_lines_text(const AppPacket& p);

} // namespace hailstone::net
