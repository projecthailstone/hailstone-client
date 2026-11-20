#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/item.h"
#include "net/messages/item_actions.h"
#include "net/messages/coin.h"
#include "net/messages/evolving.h"
#include "net/messages/augment.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/augment_packets.h"
#include "net/protocol/rof2/packets/evolving_packets.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/protocol/rof2/packets/trade_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_items(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::MoveItem:
        if (const auto* x = payload<MoveItem>(m)) return net::encode(*x);
        break;
    case GameOpcode::MoveMultipleItems:
        if (const auto* x = payload<std::vector<MultiMoveEntry>>(m))
            return net::encode_move_multiple(*x);
        break;
    case GameOpcode::DeleteItem:
        if (const auto* x = payload<MoveItem>(m)) return net::encode_delete_item(*x);
        break;
    case GameOpcode::LootRequest:
        if (const auto* x = payload<LootSessionIntent>(m)) return net::encode_loot_request(x->corpse_id);
        break;
    case GameOpcode::EndLootRequest:
        if (const auto* x = payload<LootSessionIntent>(m)) return net::encode_end_loot_request(x->corpse_id);
        break;
    case GameOpcode::LootItem:
        if (const auto* x = payload<LootItem>(m)) return net::encode(*x);
        break;
    case GameOpcode::ItemLinkClick:
        if (const auto* x = payload<ItemLinkClick>(m)) return net::encode(*x);
        break;
    case GameOpcode::ItemLinkResponse:
        if (const auto* x = payload<ItemLinkRequest>(m)) return net::encode(*x);
        break;
    case GameOpcode::ItemVerifyRequest:
        if (const auto* x = payload<ItemVerificationIntent>(m))
            return net::encode_item_verify_request(x->slot, x->target);
        break;
    case GameOpcode::ItemPreviewRequest:
        if (const auto* x = payload<ItemPreviewIntent>(m))
            return net::encode_item_preview_request(x->item_id, x->instance ? &*x->instance : nullptr);
        break;
    case GameOpcode::ApplyPoison:
        if (const auto* x = payload<ApplyPoison>(m)) return net::encode(*x);
        break;
    case GameOpcode::Consume:
        if (const auto* x = payload<ItemConsumptionIntent>(m)) {
            if (x->confirmation) return net::encode_consume_reply(x->slot, x->drink);
            return net::encode_consume(x->slot, x->automatic, x->drink);
        }
        break;
    case GameOpcode::EvolveItem:
        if (const auto* x = payload<EvolvingRequest>(m)) return net::encode(*x);
        if (const auto* x = payload<EvolvingFinalResult>(m)) return net::encode(*x);
        break;
    case GameOpcode::AugmentItem:
        if (const auto* x = payload<AugmentItem>(m)) return net::encode(*x);
        break;
    case GameOpcode::AugmentInfo:
        if (const auto* x = payload<AugmentInformationIntent>(m))
            return net::encode_augment_info_request(x->solvent_id, x->window);
        break;
    case GameOpcode::MoveCoin:
        if (const auto* x = payload<MoveCoin>(m)) return net::encode(*x);
        break;
    case GameOpcode::CursorCoinSplit:
        if (const auto* x = payload<Coins>(m)) return net::encode_split_coin(*x);
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
