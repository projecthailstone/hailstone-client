#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/merchant.h"
#include "net/messages/market.h"
#include "net/messages/guild_bank.h"
#include "net/messages/marketplace.h"
#include "net/messages/real_estate.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/guild_bank_packets.h"
#include "net/protocol/rof2/packets/market_packets.h"
#include "net/protocol/rof2/packets/marketplace_packets.h"
#include "net/protocol/rof2/packets/merchant_packets.h"
#include "net/protocol/rof2/packets/real_estate_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_marketplace(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::Trader:
        if (const auto* x=payload<TraderBegin>(m)) return net::encode(*x);
        if (const auto* x=payload<TraderPrice>(m)) return net::encode(*x);
        if (const auto* x=payload<MarketCommand>(m); x && x->kind==MarketCommand::Kind::TraderAction)
            return net::encode_trader_action(x->value);
        break;
    case GameOpcode::TraderShop:
        if (const auto* x=payload<TraderBuy>(m)) return net::encode(*x);
        if (const auto* x=payload<TraderClick>(m)) return net::encode(*x);
        if (const auto* x=payload<BazaarQuery>(m)) return net::encode(*x);
        if (const auto* x=payload<BazaarInspect>(m)) return net::encode(*x);
        if (const auto* x=payload<MarketCommand>(m)) {
            if (x->kind==MarketCommand::Kind::ShopAction) return net::encode_trader_shop_action(x->value);
            if (x->kind==MarketCommand::Kind::ShopEnd) return net::encode_trader_shop_end();
        }
        break;
    case GameOpcode::FindMarketSpawn:
        if (const auto* x=payload<MarketCommand>(m); x && x->kind==MarketCommand::Kind::FindSpawn)
            return net::encode_find_market_spawn(x->value);
        break;
    case GameOpcode::Barter:
        if (const auto* x=payload<BarterSearch>(m)) return net::encode(*x);
        if (const auto* x=payload<BarterItemInspect>(m)) return net::encode(*x);
        if (const auto* x=payload<MarketCommand>(m)) {
            switch(x->kind) {
            case MarketCommand::Kind::BuyerMode: return net::encode_buyer_mode(x->enabled);
            case MarketCommand::Kind::Welcome: return net::encode_buyer_welcome(x->text);
            case MarketCommand::Kind::SearchItem: return net::encode_buyer_item_search(x->text);
            case MarketCommand::Kind::InspectSearchItem: return net::encode_barter_search_item_inspect(x->value,x->enabled);
            case MarketCommand::Kind::RemoveLine: return net::encode_remove_buy_line(x->value);
            case MarketCommand::Kind::InspectBuyer: return net::encode_barter_inspect(x->value,x->other);
            case MarketCommand::Kind::Greeting: return net::encode_buyer_greeting(x->value);
            case MarketCommand::Kind::BarterAction: return net::encode_barter_action(x->value);
            default: break;
            }
        }
        break;
    case GameOpcode::BuyerItems:
        if (const auto* x=payload<BuyerSale>(m)) return net::encode(*x);
        if (const auto* x=payload<MarketCommand>(m)) {
            if (x->kind==MarketCommand::Kind::ModifyLine) return net::encode_modify_buy_line(x->line);
            if (x->kind==MarketCommand::Kind::BuyLines) return net::encode_buy_lines(x->lines);
        }
        break;
    case GameOpcode::ShopRetrieveParcel:
        if (const auto* x = payload<ParcelRetrieve>(m)) return net::encode(*x);
        break;
    case GameOpcode::ShopSendParcel:
        if (const auto* x = payload<ParcelSend>(m)) return net::encode(*x);
        break;
    case GameOpcode::GuildBank:
        if (const auto* x = payload<GuildBankIntent>(m)) {
            switch (x->action) {
            case GuildBankAction::Deposit: return net::encode_guild_bank_deposit(x->banker);
            case GuildBankAction::Permissions: return net::encode_guild_bank_permissions(
                x->banker,x->key,x->item_id,x->permission,x->member);
            default: return net::encode_guild_bank_item(x->action,x->banker,x->key,x->item_id,x->value);
            }
        }
        break;
    case GameOpcode::MarketplaceRequestItems:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_marketplace_request(net::kOpMarketplaceRequestItems)) : std::nullopt;
    case GameOpcode::MarketplaceRequestFunding:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_marketplace_request(net::kOpMarketplaceRequestFunding)) : std::nullopt;
    case GameOpcode::MarketplaceReady:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_marketplace_request(net::kOpMarketplaceReady)) : std::nullopt;
    case GameOpcode::MarketplaceHello:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_marketplace_hello()) : std::nullopt;
    case GameOpcode::MarketplacePurchase:
        if (const auto* x=payload<MarketplacePurchaseIntent>(m)) return net::encode_marketplace_purchase(x->entry_id,x->quantity);
        break;
    case GameOpcode::MarketplacePreviewRequest:
        if (const auto* x=payload<std::uint32_t>(m)) return net::encode_marketplace_preview_request(*x);
        break;
    case GameOpcode::MarketplaceZip:
        if (const auto* x=payload<MarketplacePostalCode>(m)) {
            std::vector<std::byte> bytes(80);
            for (std::size_t i=0;i<x->zip.size() && i<5;++i) bytes[i]=static_cast<std::byte>(x->zip[i]);
            return AppPacket(to_wire(Op::MarketplaceZip),std::move(bytes));
        }
        break;
    case GameOpcode::ReSearch:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_search(x->id,x->extra,x->text,x->text2,x->text3,x->flag);
        break;
    case GameOpcode::ReTravel:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_travel(x->id,x->extra);
        break;
    case GameOpcode::RePlotSelect:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_plot_select(x->id);
        break;
    case GameOpcode::RePurchase:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_purchase(x->id,x->extra,x->detail);
        break;
    case GameOpcode::ReSell:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_sell(x->id);
        break;
    case GameOpcode::ReTransfer:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_transfer(x->id,x->text);
        break;
    case GameOpcode::ReOptOut:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_opt_out(x->id,x->enabled);
        break;
    case GameOpcode::ReEscrow:
        if (const auto* x=payload<ReEscrow>(m)) return net::encode(*x);
        break;
    case GameOpcode::ReAccessSave:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_access_save(x->id,x->access);
        break;
    case GameOpcode::ReToStorage:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_to_storage(x->key,x->id,x->extra,x->detail);
        break;
    case GameOpcode::ReToInventory:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_to_inventory(x->key,x->id,x->extra);
        break;
    case GameOpcode::ReMenu5:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_menu5(x->id,x->extra);
        break;
    case GameOpcode::ReMenu8:
    case GameOpcode::ReMenu9:
    case GameOpcode::ReMenu13:
        if (const auto* x=payload<ReIntent>(m)) {
            const auto wire=opcode==GameOpcode::ReMenu8?Op::ReMenu8:opcode==GameOpcode::ReMenu9?Op::ReMenu9:Op::ReMenu13;
            return net::encode_re_item_op(to_wire(wire),x->id,x->extra,x->detail);
        }
        break;
    case GameOpcode::ReStoreAll:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_re_store_all()) : std::nullopt;
    case GameOpcode::RePackCrate:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_pack_crate(x->enabled);
        break;
    case GameOpcode::ReInspect:
        if (const auto* x=payload<ReIntent>(m)) return net::encode_re_inspect(x->id,x->extra);
        break;
    case GameOpcode::ReContextMenu:
        if (const auto* x=payload<ReIntent>(m)) {
            if (x->key.empty()) return net::encode_re_menu_request(x->id,x->extra,x->x,x->y);
            return net::encode_re_menu_request(x->key,x->id,x->extra,x->x,x->y,x->detail);
        }
        break;
    case GameOpcode::RePlace:
        if (const auto* x=payload<RePlaceRequest>(m)) return net::encode(*x);
        break;
    case GameOpcode::RePlaceConfirm:
        if (const auto* x=payload<RePlacementIntent>(m)) return net::encode_re_place_confirm(x->placed,x->index,x->box_a,x->box_b);
        break;
    case GameOpcode::ReOffer:
        if (const auto* x=payload<ReTransaction>(m)) return net::encode(net::kOpReOffer,*x);
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
