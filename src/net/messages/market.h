#pragma once

// Protocol-independent gameplay values extracted from the RoF2 packet declarations.
// Application codecs translate these values to their own wire layouts.
// Serial lengths, packet opcodes and binary read/write remain in protocol/rof2/.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace hailstone::net {

// RoF2BazaarTraderBuyerActions (the first dword of OP_Trader / OP_TraderShop).
inline constexpr std::uint32_t kTraderOff = 0, kTraderBegin = 1, kTraderEnd = 2, kTraderPriceUpdate = 3,
                               kTraderEndTransaction = 4, kBazaarSearchAction = 7, kBazaarWelcome = 9,
                               kTraderBuyItem = 10, kTraderListItems = 11, kTraderCustomerBrowsing = 13,
                               kBazaarInspectAction = 18, kTraderItemMove = 19, kTraderReconcile = 20,
                               kTraderItemMessage = 21, kTraderAck2 = 22, kBazaarAddTrader = 24,
                               kBazaarRemoveTrader = 25, kBazaarFirstOpen = 26, kTraderClick = 28;

inline constexpr int kTraderSlots = 200;          // EQ::invtype::BAZAAR_SIZE

// The start (Send_OP_Trader_00639730 @0x639730): 4404 bytes, action 1, then
// the 200 trader slots' item GUIDs (18 bytes each, the server's 16-digit
// serial numbers; unused slots are zero) and their prices in copper.
struct TraderBegin {
    std::array<std::string, kTraderSlots> serials;
    std::array<std::uint32_t, kTraderSlots> prices{};
};

// The 32-byte price message (0x6377c0 / CBazaarWnd::WndNotification and
// Send_OP_Trader_00638d80): action 3 (a price change; 0 removes the item) or
// 20 (reconcile, sent for every slot after a sale), the item GUID, a dword
// (the item's count for action 20), the price.
struct TraderPrice {
    std::uint32_t action = kTraderPriceUpdate;
    std::string serial;
    std::uint32_t count = 0;
    std::uint32_t price = 0;
};

// TraderBuy_Struct (284 bytes): OP_TraderShop action 10 both ways (the server
// also sends it to the trader as OP_Trader 10 when something sold).
struct TraderBuy {
    std::uint32_t action = kTraderBuyItem;
    std::uint32_t method = 0;     // 0 at the trader, 1 by parcel, 2 straight to the inventory
    std::uint32_t sub_action = 0; // the answer: 0 success, 1 failed, 3 data out of date, ... (see HandleWorldMessage_Bazaar)
    std::uint32_t unknown012 = 0;
    std::uint32_t trader_id = 0;  // the trader's spawn id (search purchases: the character id)
    std::string buyer_name, seller_name, item_name, serial;
    std::uint32_t item_id = 0;
    std::uint32_t price = 0;      // each
    std::uint32_t already_sold = 0;
    std::uint32_t unknown276 = 0; // low word: vouchers
    std::uint32_t quantity = 0;
};

inline constexpr std::uint32_t kTraderBuyFailed = 1; // sub action 1: "Your attempt to purchase ... was unsuccessful."

// EQEmu's Client::TradeRequestFailed answers a refused purchase with the request itself (sub action 0, which the
// original reads as a success) and the trader id and already-sold count set to 0xffffffff; no answer of the original
// server's has both. Such an answer is read as sub action 1.
inline bool is_eqemu_trade_failure(const TraderBuy& v) {
    return v.sub_action == 0 && v.trader_id == 0xffffffffu && v.already_sold == 0xffffffffu;
}

// What the server sends on OP_Trader (CBazaarWnd::HandleTraderMsg @0x63c450).
struct TraderMessage {
    std::uint32_t action = 0;
    std::uint32_t word1 = 0, word2 = 0;        // +4 / +8: sub action, entity id, ...
    std::vector<std::pair<std::string, std::uint32_t>> listing; // action 11: (GUID, price) for the 200 slots
    std::optional<TraderBuy> sale;             // action 10
    std::string name;                          // action 21: +4 char[64]
    std::uint32_t code21 = 0;                  // action 21: +0x44
};

// OP_BecomeTrader (8 bytes, RoF2 BecomeTrader_Struct): {spawn id, trader flag};
// Handle_OP_BecomeTrader @0x4b6390 sets the spawn's Trader field (+800).
struct BecomeTrader {
    std::uint32_t spawn_id = 0;
    std::uint32_t trader = 0;
};

// RightClickedOnPlayer @0x536d80 on a trader: {28, spawn id, a byte 0 and three
// bytes the original leaves uninitialised} (12 bytes). The answer is the same
// shape with the approval in the third dword's low byte; on approval the
// trader's items follow as merchant item packets (0x64) and CMerchantWnd opens.
struct TraderClick {
    std::uint32_t action = kTraderClick;
    std::uint32_t trader_id = 0;
    std::uint32_t approval = 0;
};

// The bazaar search (CBazaarSearchWnd::doQuery @0x632e40): OP_TraderShop
// action 7, 132 bytes. The defaults are what doQuery starts from (its
// combo boxes' first choice keeps them): 0xffffffff is "any" for class,
// race, stat, slot and type, as EQEmu reads it.
struct BazaarQuery {
    // The trader combo's item data: 0 all traders ("All Traders", eqstr 6755),
    // 0xffffffff this zone's traders (eqstr 783), else a trader's character id.
    // The packet's scope byte is 1 unless it is 0xffffffff.
    std::uint32_t trader = 0;
    std::uint32_t class_ = 0xffffffff;
    std::uint32_t race = 0xffffffff;
    std::uint32_t stat = 0xffffffff;
    std::uint32_t slot = 0xffffffff;
    std::uint32_t type = 0xffffffff;
    std::string name;
    std::uint32_t min_cost = 0, max_cost = 0; // platinum
    std::uint32_t min_level = 1, max_level = 100;
    std::uint32_t max_results = 200;
    std::uint32_t prestige = 0;
    std::uint32_t augment = 0;
};

// The item inspect of a search result (Send_OP_TraderShop_00633520 @0x633520):
// OP_TraderShop action 18, 40 bytes: {18, 0, trader id, GUID, item id, flags
// (2 with shift)}. The server answers with the item (packet type 0, a link view).
struct BazaarInspect {
    std::uint32_t trader_id = 0;
    std::string serial;
    std::uint32_t item_id = 0;
    std::uint32_t flags = 0;
};

// OP_TraderShop from the server (HandleWorldMessage_Bazaar case OP_TraderShop):
// 9 welcome (traders at +8, items at +12), 10 a purchase answer, 24 / 25 a
// trader joins / leaves the search window's list, 28 the click answer.
struct BazaarTrader {
    std::uint16_t zone_id = 0, zone_instance = 0;
    std::uint32_t trader_id = 0; // the character id
    std::uint32_t entity_id = 0; // the spawn id (0: another bazaar shard)
    std::string name;
};

struct TraderShopMessage {
    std::uint32_t action = 0;
    std::uint32_t traders = 0, items = 0; // 9
    std::optional<TraderBuy> buy;         // 10
    BazaarTrader trader;                  // 24 (and 25: trader_id only)
    std::optional<TraderClick> click;     // 28
};

// OP_BazaarSearch results (CBazaarSearchWnd::HandleSearchResults @0x635530):
// u32 action, u16 zone, u32 trader, u32 count, then per item {u32 trader
// (character id), GUID string, u32 cost, u32 quantity, u32 item id, u32 icon,
// name, u32 the searched stat} (BazaarSearchResult_Unserialize @0x636b30). The
// server's "done" and delivery cost packets share the opcode and carry a
// non-positive count.
struct BazaarResult {
    std::uint32_t trader_id = 0;
    std::string serial;
    std::uint32_t cost = 0;
    std::uint32_t quantity = 0;
    std::uint32_t item_id = 0;
    std::uint32_t icon = 0;
    std::string name;
    std::uint32_t stat = 0;
};

struct BazaarResults {
    std::uint32_t action = 0;
    std::uint16_t zone_id = 0;
    std::uint32_t trader_id = 0;
    std::vector<BazaarResult> results;
};

// OP_TraderDelItem (29 bytes): {u32, u32 trader id, char[17] GUID, u32}; the
// item leaves the open trader window (0x6ef450).
struct TraderDelItem {
    std::uint32_t trader_id = 0;
    std::string serial;
};

// RoF2BuyerActions (the first dword of OP_Barter / OP_BuyerItems), as Handle_OP_Barter / Handle_OP_BuyerItems
// switch on them.
inline constexpr std::uint32_t kBuyerSearchResults = 0x00, kBarterSearchAction = 0x01, kBuyerModeOn = 0x02,
                               kBuyerModeOff = 0x03, kBuyerLinesText = 0x05, kBuyerBuyLine = 0x06,
                               kBuyerModifyBuyLine = 0x07, kBuyerRemoveItem = 0x08, kBuyerSellItem = 0x09,
                               kBuyerBuyItem = 0x0a, kBuyerInspectBegin = 0x0b, kBuyerInspectEnd = 0x0c,
                               kBuyerAppearance = 0x0d, kBuyerSendBuyLine = 0x0e, kBuyerItemInspect = 0x0f,
                               kBuyerBrowsingBuyLine = 0x10, kBarterWelcomeMessage = 0x11,
                               kBuyerWelcomeMessage = 0x13, kBuyerGreeting = 0x14, kBarterItemInspect = 0x15,
                               kBuyerInventoryFull = 0x16, kBarterOpenWindow = 0x17, kBarterBuyerList = 0x19,
                               kBarterAddBuyer = 0x1a, kBarterRemoveBuyer = 0x1b;

// One buy line, the original's 0xb8-byte BarterBuyLine: written by BarterBuyLine_WriteToBuffer @0x7d7060 (u32 slot,
// u8 enabled; when enabled u32 item id, the name, u32 icon, u32 quantity, u8 toggle, u32 cost, u32 n, n × {u32 id,
// u32 quantity, u32 icon, name}, u32 buyer spawn id, u32 buyer character id, u32 zone, buyer name) and read back by
// 0x7d6ef0 (each field only while the packet holds it, so EQEmu's lines without the last four read as zeros; a
// line that is not enabled stops after the flag and keeps the defaults, icon 500).
struct BuyLineTrade {
    std::uint32_t item_id = 0, quantity = 0, icon = 0; // +0, +4, +8
    std::string name;                                  // +0xc char[64]
};

struct BuyLine {
    std::uint32_t slot = 0;          // +0x00 the buyer's line number (0..199)
    bool enabled = false;            // +0x04 the line is in use
    std::uint32_t item_id = 0;       // +0x08
    std::string item_name;           // +0x0c char[64]
    std::uint32_t icon = 500;        // +0x4c
    std::uint32_t quantity = 0;      // +0x50 how many the buyer still wants
    bool toggle = false;             // +0x54 active (the buyer buys with it)
    std::uint32_t cost = 0;          // +0x58 copper each
    std::vector<BuyLineTrade> trade_items; // +0x5c the compensation items, each per item bought (at most 10)
    std::uint32_t buyer_spawn_id = 0; // +0x6c (search results)
    std::uint32_t buyer_id = 0;       // +0x70 the buyer's character id
    std::uint32_t zone = 0;           // +0x74 the buyer's zone (EQEmu: u16 zone, u16 instance)
    std::string buyer_name;           // +0x78 char[64]
};

// The most compensation items a received buy line may list. The window adds at most 10 (kMaxCompensation); this is
// the bound the decoders keep a server's line to so it cannot claim billions.
inline constexpr std::uint32_t kMaxBuyLineTrades = 64;

inline constexpr std::uint32_t kKronoItemId = 88888; // 0x15b38: a Krono buy line (CBarterWnd_AddCompensationItem)

struct BarterInspectReply {
    std::uint32_t spawn_id = 0;
    std::uint32_t approval = 0;
};

// OP_Barter 13 BuyerAppearance (EQEmu Buyer_SetAppearance_Struct, 76 bytes: {13, spawn id, 64 B, u32 enabled}):
// Handle_OP_Barter reads the byte at +72 (the struct's `enabled`) into the spawn's buyer flag.
struct BuyerAppearance {
    std::uint32_t spawn_id = 0;
    bool enabled = false;
};

// The ways a sale to a buyer is made (EQEmu BarterPurchaseMethods): at the buyer (CBarterMerchantWnd), or from the
// barter search window with the buyer in this zone (1) or elsewhere (2): CBarterSearchWnd::sub_0062cf70 sets
// 1 + (the line's zone != the current zone).
inline constexpr std::uint32_t kBarterAtBuyer = 0, kBarterInBazaar = 1, kBarterOutsideBazaar = 2;

// A sale's record, the original's 0x178-byte struct (constructor 0x4232b0, BarterBuyLine_Serialize @0x62ad20
// writes it, 0x423500 reads it): six u32 (+0 method, +4 result, +8 buyer spawn id, +0xc buyer character id,
// +0x10, +0x14), three strings (+0x18 buyer [64], +0x58 item [32], +0x78 seller [64]), the buy line (+0xb8), u32
// +0x170 and u32 +0x174.
struct BuyerSale {
    std::uint32_t method = kBarterAtBuyer; // +0x00
    std::uint32_t result = 1;              // +0x04 (the constructor's 1)
    std::uint32_t buyer_spawn_id = 0;      // +0x08
    std::uint32_t buyer_id = 0;            // +0x0c
    std::string buyer_name, item_name, seller_name;
    BuyLine line;                          // +0xb8
    std::uint32_t quantity = 0;            // +0x170: the quantity sold (sent)
    std::uint32_t count = 0;               // +0x174: the quantity in a result (read)
};

// OP_BuyerItems 9 / 10, zone → client (Handle_OP_BuyerItems): 9 our sale's answer, 10 a buy made by our buyer mode.
// Case 9 reads +4 as the result: 0 success (Barter_ReportItemSold with +0x174 items, buyer name +0x18), 1 failed,
// 2 nothing, 3 pending, 4 out of date (+0 method 0: reopen the merchant window, else search again), 5 the buyer's
// inventory is full, 6 the seller lacks the quantity, 7 / 9 Krono, 8 compensation needs the same zone. Case 10 is
// Barter_ReportItemBought (seller name +0x78). EQEmu writes {action, sub action (+0), error code (+4), 16 B, buyer,
// item, seller, a line with the item's name, cost and the compensation (item id 0xffffff, no icon or quantity),
// 0, 0, 0, "\xff\xff\xff", 0 (+0x170), the quantity (+0x174)}, so its error code is the original's result and its
// sub action (1 on any failure) the method.
inline constexpr std::uint32_t kBarterSuccess = 0, kBarterFailure = 1, kBarterNoResult = 2, kBarterPending = 3,
                               kBarterOutOfDate = 4, kBarterBuyerFull = 5, kBarterSellerLacksItem = 6,
                               kBarterNoKrono = 7, kBarterSameZone = 8, kBarterKronoFailed = 9;

struct BuyerSaleResult {
    std::uint32_t action = 0; // kBuyerSellItem / kBuyerBuyItem
    BuyerSale sale;
};

// OP_Barter 1, the barter search (Send_OP_Barter_0062bc90, 84 bytes): u32 1, char[64] text, u32 transaction id (the
// __EQGetTime of the search), u32 0, u32 buyer character id (the player combo's choice; 0 all), u32 scope (1 for
// "Local Barterers"). EQEmu reads BarterSearchRequest_Struct (83 bytes, packed): the same up to the scope byte, then
// a u16 zone that the original's scope dword leaves 0.
struct BarterSearch {
    std::string text;
    std::uint32_t transaction_id = 0;
    std::uint32_t buyer_id = 0;
    bool local = false;
};

// OP_BuyerItems 1 (CBarterSearchWnd_HandleSearchResults @0x62cdd0): {u32 1, the search text, u32 transaction id,
// u32, u32, u8 (0x62e430), u32 count, count × the line with its buyer fields}. EQEmu writes the zone as u16 zone
// and u16 instance, which the original reads as the u32 at +0x74.
struct BarterSearchResults {
    std::string text;
    std::uint32_t transaction_id = 0;
    std::vector<BuyLine> lines;
};

struct BuyerItemResult {
    std::string name;
    std::uint32_t item_id = 0, icon = 0;
};

// OP_Barter 0x0f (32 bytes): inspect a buy line's item or one of its compensation items. Send_OP_Barter_0062b200 (the
// barter search window): {0x0f, 0, the buyer's spawn id when the line's zone is this one else its character id, the
// line's zone, the line's slot, the compensation index (-1 the item bought), the item id, u32 2 with Shift else 0}.
// CBarterMerchantWnd (a right click on DetailList): {0x0f, 0, the merchant's spawn id, the zone, the line's slot,
// the compensation index, its item id, 2 with Shift else 0}. EQEmu reads the item id at +24 and sends its link.
struct BarterItemInspect {
    std::uint32_t buyer = 0, zone = 0, slot = 0;
    std::int32_t index = -1;
    std::uint32_t item_id = 0;
    bool shift = false;
};

// OP_Barter 0x14 (Handle_OP_Barter: eqstr 6070 "%1 greets you, '%2'" with the text, else 788): name at +8, text at
// +0x48.
struct BuyerGreeting {
    std::string name, text;
};

// The barter search window's buyers (CBarterSearchWnd's hash +0x220, keyed by character id): OP_Barter 0x1a {0x1a,
// u32 zone, u32 character id, u32 spawn id, char[64] name} adds one (0x62d620; EQEmu
// BuyerAddBuyertoBarterWindow_Struct), 0x1b {0x1b, character id} removes one (0x62d670), OP_BuyerItems 0x19
// {0x19, u32 count, count × {u32 zone, u32 character id, u32 spawn id, name}} replaces them all
// (CBarterSearchWnd_HandleBuyerList; EQEmu never sends it).
struct BarterBuyer {
    std::uint32_t zone = 0, buyer_id = 0, spawn_id = 0;
    std::string name;
};



struct BazaarSearchUpdate {
    BazaarResults results;
    std::uint16_t voucher_cost = 0;
};
using MarketDetail = std::variant<std::monostate, std::uint32_t, std::string,
    std::vector<BuyerItemResult>, BarterInspectReply, BuyerAppearance, BuyerGreeting,
    BarterBuyer, BarterSearchResults, BuyLine, BuyerSaleResult, std::vector<BarterBuyer>>;
struct MarketEvent {
    std::uint32_t action = 0;
    MarketDetail detail;
};
struct MarketCommand {
    enum class Kind { TraderAction, ShopAction, ShopEnd, FindSpawn, BuyerMode,
        Welcome, SearchItem, InspectSearchItem, ModifyLine, RemoveLine, InspectBuyer,
        Greeting, BarterAction, BuyLines } kind{};
    std::uint32_t value = 0, other = 0;
    bool enabled = false;
    std::string text;
    BuyLine line;
    std::vector<BuyLine> lines;
};
} // namespace hailstone::net
