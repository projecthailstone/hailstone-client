#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/marketplace.h"
#include "net/messages/real_estate.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/protocol/rof2/packets/marketplace_packets.h"
#include "net/protocol/rof2/packets/real_estate_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

std::optional<GameMessage> decode_marketplace(GameOpcode op, const AppPacket& p) {
    switch (op) {
    case GameOpcode::MarketplaceItems:
        if (auto x=net::decode_marketplace_items(p,static_cast<std::int64_t>(std::time(nullptr)))) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MarketplaceFunding:
        if (auto x=net::decode_marketplace_funding(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MarketplaceStates:
        if (auto x=net::decode_marketplace_states(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MarketplaceWallet:
        if (auto x=net::decode_marketplace_wallet(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MarketplacePurchaseResult:
        if (auto x=net::decode_marketplace_purchase_result(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MarketplacePreview:
        if (auto x=net::decode_item_preview(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MarketplaceUnavailable:
        return GameMessage{op,std::monostate{}};
    case GameOpcode::MarketplaceZipStatus:
        return GameMessage{op,static_cast<std::uint8_t>(p.data.size()>0x48 && p.data[0x48]!=std::byte{0})};
    case GameOpcode::ReNop1:
    case GameOpcode::ReNop2:
    case GameOpcode::ReItemSummaryUpdate:
    case GameOpcode::RePlaceStop:
        return GameMessage{op,std::monostate{}};
    case GameOpcode::ReCatalog:
        if (auto x=net::decode_re_catalog(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReRecord:
    case GameOpcode::ReSetCurrent:
    case GameOpcode::ReRemove:
        if (auto x=net::decode_re_record(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReAccess:
    case GameOpcode::ReAccessUpdate:
        if (auto x=net::decode_re_access(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReItemSummary:
    case GameOpcode::ReItemRows:
    case GameOpcode::ReItemRowAdd:
        if (auto x=net::decode_re_item_list(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReItemSummaryRename:
        if (auto x=net::decode_re_rename(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReItemRemoved:
        if (auto x=net::decode_re_item_removed(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::RePlacedItemRemove:
        if (auto x=net::decode_re_placed_item_remove(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReItemSummaryForget:
    case GameOpcode::ReResult:
    case GameOpcode::ReSellResult:
    case GameOpcode::ReTransferResult:
    case GameOpcode::RePlotSearchOpen:
        if (auto x=net::decode_re_dword(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReSearch:
        if (auto x=net::decode_re_search_reply(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReEscrow:
        if (auto x=net::decode_re_escrow(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReOwnedA:
    case GameOpcode::ReOwnedB:
    case GameOpcode::ReOwnedC:
        if (auto x=net::decode_re_id_list(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReOwnedAToggle:
    case GameOpcode::ReOwnedBToggle:
        if (auto x=net::decode_re_list_toggle(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReNpcItem:
        if (auto x=net::decode_re_npc_item(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReContextMenu:
        if (auto x=net::decode_re_menu(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReInspect:
        if (auto x=net::decode_re_inspect(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReOffer:
        if (auto x=net::decode_re_transaction(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::RePlacedItem:
        if (auto x=net::decode_re_placed_item(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReAreas:
        if (auto x=net::decode_re_areas(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::RePlace:
        if (auto x=net::decode_re_place_reply(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::RePurchase:
        if (!p.data.empty()) return GameMessage{op,static_cast<std::uint8_t>(p.data[0])};
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
