#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/trade.h"
#include "net/messages/merchant.h"
#include "net/messages/door.h"
#include "net/messages/coin.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/door_packets.h"
#include "net/protocol/rof2/packets/merchant_packets.h"
#include "net/protocol/rof2/packets/trade_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_commerce(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::TradeRequest:
        if (const auto* x = payload<TradeInvitation>(m)) return net::encode_trade_request({x->target_id,x->actor_id});
        break;
    case GameOpcode::TradeRequestAck:
        if (const auto* x = payload<TradeInvitation>(m)) return net::encode_trade_request_ack({x->target_id,x->actor_id});
        break;
    case GameOpcode::TradeAcceptClick:
        if (const auto* x = payload<TradeReadiness>(m)) return net::encode(net::TradeAccept{x->partner_id,static_cast<std::uint8_t>(x->ready),0,0,0});
        break;
    case GameOpcode::TradeBusy:
        if (const auto* x = payload<TradeRefusal>(m)) return net::encode(net::TradeBusy{x->target_id,x->actor_id,x->reason,0,0,0});
        break;
    case GameOpcode::CancelTrade:
        if (const auto* x = payload<TradeCancellation>(m)) return net::encode(net::CancelTrade{x->partner_id,x->actor_id});
        break;
    case GameOpcode::ShopRequest:
        if (const auto* x = payload<MerchantClick>(m)) return net::encode(*x);
        break;
    case GameOpcode::ShopPlayerBuy:
        if (const auto* x = payload<MerchantBuy>(m)) return net::encode(*x);
        break;
    case GameOpcode::ShopPlayerSell:
        if (const auto* x = payload<MerchantSell>(m)) return net::encode(*x);
        break;
    case GameOpcode::ShopEnd:
        if (const auto* x = payload<MerchantClose>(m)) return net::encode_shop_end(x->npc_id,x->player_id);
        break;
    case GameOpcode::AltCurrencyReclaim:
        if (const auto* x = payload<AltCurrencyReclaimRequest>(m)) return net::encode_alt_currency_reclaim(x->currency,x->count,x->put_in);
        break;
    case GameOpcode::AltCurrencyMerchantRequest:
        if (const auto* x = payload<PointMerchantQuery>(m)) return net::encode_alt_currency_merchant_request(x->npc);
        break;
    case GameOpcode::AltCurrencyPurchase:
        if (const auto* x = payload<PointMerchantPurchase>(m)) return net::encode_alt_currency_purchase(x->npc,x->item);
        break;
    case GameOpcode::AltCurrencySellSelection:
        if (const auto* x = payload<PointMerchantSelection>(m)) return net::encode_alt_currency_select(x->npc,x->slot,x->sub);
        break;
    case GameOpcode::AltCurrencySell:
        if (const auto* x = payload<PointMerchantSaleRequest>(m)) return net::encode_alt_currency_sell(x->npc,x->slot,x->sub,x->charges,x->cost);
        break;
    case GameOpcode::AdventureMerchantRequest:
        if (const auto* x = payload<AdventureMerchantQuery>(m)) return net::encode_adventure_merchant_request(x->type,x->npc);
        break;
    case GameOpcode::AdventureMerchantPurchase:
        if (const auto* x = payload<AdventureMerchantPurchase>(m)) return net::encode_adventure_merchant_purchase(x->type,x->npc,x->item);
        break;
    case GameOpcode::AdventureMerchantSell:
        if (const auto* x = payload<AdventureMerchantSaleRequest>(m)) return net::encode_adventure_merchant_sell(x->npc,x->slot,x->sub,x->charges);
        break;
    case GameOpcode::TradeSkillCombine:
        if (const auto* x=payload<TradeSkillCombine>(m)) return net::encode(*x);
        break;
    case GameOpcode::RecipesFavorite:
        if (const auto* x=payload<RecipeFavorites>(m)) return net::encode(*x);
        break;
    case GameOpcode::RecipesSearch:
        if (const auto* x=payload<RecipesSearch>(m)) return net::encode(*x);
        break;
    case GameOpcode::RecipeDetails:
        if (const auto* x=payload<std::uint32_t>(m)) return net::encode_recipe_details_request(*x);
        break;
    case GameOpcode::RecipeAutoCombine:
        if (const auto* x=payload<RecipeAutoCombine>(m)) return net::encode(*x);
        break;
    case GameOpcode::Split:
        if (const auto* x=payload<CoinSplit>(m)) {
            hailstone::ByteWriter w;
            w.le<std::uint32_t>(static_cast<std::uint32_t>(x->platinum));
            w.le<std::uint32_t>(static_cast<std::uint32_t>(x->gold));
            w.le<std::uint32_t>(static_cast<std::uint32_t>(x->silver));
            w.le<std::uint32_t>(static_cast<std::uint32_t>(x->copper));
            return net::make_packet(Op::Split,std::move(w));
        }
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
