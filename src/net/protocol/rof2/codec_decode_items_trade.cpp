#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/trade.h"
#include "net/messages/item.h"
#include "net/messages/item_actions.h"
#include "net/messages/augment.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/augment_packets.h"
#include "net/protocol/rof2/packets/evolving_packets.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/protocol/rof2/packets/merchant_packets.h"
#include "net/protocol/rof2/packets/trade_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

std::optional<GameMessage> decode_items_trade(GameOpcode op, const AppPacket& p) {
    switch (op) {
    case GameOpcode::CharInventory:
        if (auto x = net::decode_char_inventory(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ItemPacket:
    case GameOpcode::ItemLinkResponse:
        if (auto x = net::decode_item_packet(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::MoveItem:
    case GameOpcode::DeleteItem:
    case GameOpcode::DeleteCharge:
        if (auto x = net::decode_move_item(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::MoneyOnCorpse:
        if (auto x = net::decode_money_on_corpse(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::LootRequest:
        if (auto x = net::decode_loot_request(p)) return GameMessage{op, LootSessionIntent{*x}};
        break;
    case GameOpcode::LootItem:
        if (auto x = net::decode_loot_item(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::LootComplete:
        if (p.data.empty()) return GameMessage{op, {}};
        break;
    case GameOpcode::MoneyUpdate:
        if (auto x = net::decode_money_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::Sound:
        if (auto x = net::decode_quest_reward(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::CashReward:
        if (auto x = net::decode_cash_reward(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ItemVerifyReply:
        if (auto x = net::decode_item_verify_reply(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ApplyPoison:
        if (auto x = net::decode_apply_poison(p)) return GameMessage{op, std::move(*x)};
        break;

    case GameOpcode::AugmentInfo:
        if (auto x = net::decode_augment_info(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::EvolveItem:
        if (auto x = net::decode_evolving_item_update(p)) return GameMessage{op, std::move(*x)};
        if (auto x = net::decode_evolving_open(p)) return GameMessage{op, std::move(*x)};
        if (auto x = net::decode_evolving_details(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::TradeRequest:
    case GameOpcode::TradeRequestAck:
        if (auto x = net::decode_trade_request(p)) return GameMessage{op, TradeInvitation{x->to_id,x->from_id}};
        break;
    case GameOpcode::TradeAcceptClick:
        if (auto x = net::decode_trade_accept(p)) return GameMessage{op, TradeReadiness{x->partner_id,x->ready!=0}};
        break;
    case GameOpcode::TradeCoins:
        if (auto x = net::decode_trade_coins(p)) return GameMessage{op, TradeCoinDelta{x->type,x->amount}};
        break;
    case GameOpcode::TradeBusy:
        if (auto x = net::decode_trade_busy(p)) return GameMessage{op, TradeRefusal{x->to_id,x->from_id,x->type}};
        break;
    case GameOpcode::CancelTrade:
        if (auto x = net::decode_cancel_trade(p)) return GameMessage{op, TradeCancellation{x->from_id,x->action}};
        break;
    case GameOpcode::FinishTrade:
    case GameOpcode::FinishWindow:
    case GameOpcode::FinishWindow2:
        return GameMessage{op, std::monostate{}};
    case GameOpcode::ShopRequest:
        if (auto x = net::decode_merchant_click(p)) return GameMessage{op,*x};
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
