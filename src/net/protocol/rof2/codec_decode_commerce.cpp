#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/merchant.h"
#include "net/messages/market.h"
#include "net/messages/door.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/door_packets.h"
#include "net/protocol/rof2/packets/guild_bank_packets.h"
#include "net/protocol/rof2/packets/market_packets.h"
#include "net/protocol/rof2/packets/merchant_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

std::optional<GameMessage> decode_commerce(GameOpcode op, const AppPacket& p) {
    switch (op) {
    case GameOpcode::ShopPlayerBuy:
        if (auto x = net::decode_merchant_buy(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::ShopPlayerSell:
        if (auto x = net::decode_merchant_sell(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::ShopDelItem:
        if (auto x = net::decode_merchant_del_item(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::ShopEnd:
    case GameOpcode::ShopEndConfirm:
        return GameMessage{op,std::monostate{}};
    case GameOpcode::AltCurrency:
        if (auto x = net::decode_alt_currency(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AltCurrencyMerchantReply:
        if (auto x = net::decode_point_merchant_list(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AltCurrencySellSelection:
        if (auto x = net::decode_alt_currency_quote(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AltCurrencySell:
        if (auto x = net::decode_alt_currency_sale(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::AdventureMerchantResponse:
        if (auto x = net::decode_adventure_merchant_list(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AdventureMerchantSell:
        if (auto x = net::decode_adventure_merchant_sale(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::TradeSkillCombine:
        return GameMessage{op,std::monostate{}};
    case GameOpcode::RecipeReply:
        if (auto x=net::decode_recipe_reply(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::RecipeDetails:
        if (auto x=net::decode_recipe_details(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::RecipeAutoCombine:
        if (auto x=net::decode_recipe_auto_combine(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::Trader:
        if (auto x=net::decode_trader_message(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::BecomeTrader:
        if (auto x=net::decode_become_trader(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::TraderShop:
        if (auto x=net::decode_trader_shop(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::TraderBulkSend:
        if (auto x=net::decode_trader_bulk(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::BazaarSearch:
        if (auto x=net::decode_bazaar_results(p)) {
            std::uint16_t cost=0;
            if (p.data.size()>=6) cost=static_cast<std::uint16_t>(
                std::to_integer<unsigned>(p.data[4]) | (std::to_integer<unsigned>(p.data[5])<<8));
            return GameMessage{op,BazaarSearchUpdate{std::move(*x),cost}};
        }
        break;
    case GameOpcode::TraderDelItem:
        if (auto x=net::decode_trader_del_item(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::Barter:
    case GameOpcode::BuyerItems: {
        auto action=net::barter_action(p);
        if (!action) break;
        MarketEvent evt;
        evt.action=*action;
        switch (*action) {
        case kBuyerSearchResults:
            if (auto x=net::decode_buyer_item_results(p)) evt.detail=std::move(*x);
            else return std::nullopt;
            break;
        case kBuyerModeOn: case kBuyerModeOff: case kBuyerInventoryFull:
            break;
        case kBuyerRemoveItem:
            if (auto x=net::decode_remove_buy_line(p)) evt.detail=*x;
            else return std::nullopt;
            break;
        case kBuyerInspectBegin:
            if (auto x=net::decode_barter_inspect_reply(p)) evt.detail=*x;
            else return std::nullopt;
            break;
        case kBuyerAppearance:
            if (auto x=net::decode_buyer_appearance(p)) evt.detail=*x;
            else return std::nullopt;
            break;
        case kBuyerBrowsingBuyLine:
            if (auto x=net::decode_buyer_browsing(p)) evt.detail=std::move(*x);
            else return std::nullopt;
            break;
        case kBuyerGreeting:
            if (auto x=net::decode_buyer_greeting(p)) evt.detail=std::move(*x);
            else return std::nullopt;
            break;
        case kBarterAddBuyer:
            if (auto x=net::decode_barter_add_buyer(p)) evt.detail=std::move(*x);
            else return std::nullopt;
            break;
        case kBarterRemoveBuyer:
            if (auto x=net::decode_barter_remove_buyer(p)) evt.detail=*x;
            else return std::nullopt;
            break;
        case kBarterSearchAction:
            if (auto x=net::decode_barter_search_results(p)) evt.detail=std::move(*x);
            else return std::nullopt;
            break;
        case kBuyerLinesText:
            if (auto x=net::decode_buy_lines_text(p)) evt.detail=std::move(*x);
            else return std::nullopt;
            break;
        case kBuyerModifyBuyLine: case kBuyerSendBuyLine:
            if (auto x=net::decode_buy_line(p)) evt.detail=std::move(*x);
            else return std::nullopt;
            break;
        case kBuyerSellItem: case kBuyerBuyItem:
            if (auto x=net::decode_buyer_sale_result(p)) evt.detail=std::move(*x);
            else return std::nullopt;
            break;
        case kBarterBuyerList:
            if (auto x=net::decode_barter_buyer_list(p)) evt.detail=std::move(*x);
            else return std::nullopt;
            break;
        default: return std::nullopt;
        }
        return GameMessage{op,std::move(evt)};
    }
    case GameOpcode::ShopParcelIcon:
        if (auto x = net::decode_parcel_icon(p)) return GameMessage{op, *x};
        break;
    case GameOpcode::ShopDeleteParcel:
        if (auto x = net::decode_parcel_retrieve(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ShopSendParcel:
        if (p.data.size() >= 8) return GameMessage{op, ParcelSendAcknowledgement{}};
        break;
    case GameOpcode::ShopRetrieveParcel:
        return GameMessage{op, std::monostate{}};
    case GameOpcode::GuildBank:
        if (auto x = net::decode_guild_bank(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GuildBankItemList:
        if (auto x = net::decode_guild_bank_list(p)) return GameMessage{op,std::move(*x)};
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
