#pragma once
#include <cstdint>
#include <vector>
#include <optional>
#include <string>

namespace hailstone::net {
struct MerchantClick {
    std::uint32_t npc_id = 0;
    std::uint32_t player_id = 0;
    std::uint32_t command = 1;
    float rate = 0.0f;
    std::int32_t tab_display = 0;
    std::int32_t unknown02 = -1;
};

struct MerchantBuy {
    std::uint32_t npc_id = 0;
    std::uint32_t player_id = 0;
    std::uint32_t item_slot = 0;
    std::uint32_t unknown12 = 0;
    std::uint32_t quantity = 0;
    std::uint32_t unknown20 = 0;
    std::uint32_t price = 0;
    std::uint32_t unknown28 = 0;
};

struct MerchantSell {
    std::uint32_t npc_id = 0;
    std::int16_t slot = -1;
    std::int16_t sub = -1;
    std::int16_t aug = -1;
    std::int16_t unknown01 = -1;
    std::uint32_t quantity = 0;
    std::uint32_t price = 0;
};

// OP_ShopParcelIcon is a notification, independent of whether a parcel
// merchant's window is open. A new zone sends its own current status.
enum class ParcelIconStatus : std::uint32_t { Off = 0, On = 1, OverLimit = 2 };

struct ParcelRetrieve {
    std::uint32_t merchant_id = 0;
    std::uint32_t player_id = 0;
    std::uint32_t parcel_slot = 0;
    std::uint32_t item_id = 0;
};

struct ParcelSend {
    std::uint32_t npc_id = 0;
    std::int16_t slot = -1;
    std::int16_t sub = -1;
    std::uint32_t quantity = 0;
    std::uint32_t money_flag = 0;
    std::string to;
    std::string note;
};

// A parcel send reply is an acknowledgement; the transport byte count is not a gameplay value.
struct ParcelSendAcknowledgement { bool received = true; };

struct MerchantDelItem {
    std::uint32_t npc_id = 0;
    std::uint32_t player_id = 0;
    std::uint32_t item_slot = 0;
};

struct AltCurrencyEntry {
    std::uint32_t id = 0, item_id = 0, icon = 0, stack_size = 0;
    bool shown = false;
};

struct AltCurrencyUpdate {
    std::uint32_t id = 0, amount = 0;
};

struct AltCurrencyPacket {
    std::vector<AltCurrencyEntry> populate; // mode 8
    std::optional<AltCurrencyUpdate> update; // mode 7
};

struct PointMerchantItem {
    std::string name;
    std::uint32_t item_id = 0, cost = 0, races = 0, classes = 0;
};

struct PointMerchantList {
    std::uint32_t currency = 0;
    std::vector<PointMerchantItem> items;
};

struct AltCurrencyQuote {
    std::string item_name;
    std::uint32_t cost = 0;
};

struct PointSale {
    std::uint32_t npc = 0;
    std::int16_t slot = -1, sub = -1;
    std::uint32_t charges = 0, price = 0;
};

struct AdventureMerchantItem {
    std::string name;
    std::uint32_t item_id = 0, cost = 0, theme = 0, races = 0, classes = 0;
    bool stackable = false, lore = false;
};
// Semantic requests; the RoF2 codec chooses bytes, padding and numeric command values.
struct MerchantClose { std::uint32_t npc_id=0,player_id=0; };
struct AltCurrencyReclaimRequest { std::uint32_t currency=0,count=0; bool put_in=false; };
struct PointMerchantQuery { std::uint32_t npc=0; };
struct PointMerchantPurchase { std::uint32_t npc=0,item=0; };
struct PointMerchantSelection { std::uint32_t npc=0; std::int16_t slot=-1,sub=-1; };
struct PointMerchantSaleRequest { std::uint32_t npc=0; std::int16_t slot=-1,sub=-1; std::uint32_t charges=0,cost=0; };
struct AdventureMerchantQuery { std::uint32_t type=0,npc=0; };
struct AdventureMerchantPurchase { std::uint32_t type=0,npc=0,item=0; };
struct AdventureMerchantSaleRequest { std::uint32_t npc=0; std::int16_t slot=-1,sub=-1; std::uint32_t charges=0; };

// Client-side LDoN merchant sell-back estimate. The server determines actual payment.
inline constexpr std::uint32_t adventure_sell_price(std::uint32_t ldon_price,
                                                   std::uint32_t sellback_rate,
                                                   std::uint32_t ldon_sold) {
    if (!ldon_sold || !ldon_price) return 0;
    const std::uint64_t amount = (static_cast<std::uint64_t>(ldon_price) + 1) * sellback_rate / 100;
    return static_cast<std::uint32_t>(amount < ldon_price ? amount : ldon_price);
}

} // namespace hailstone::net
