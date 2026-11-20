#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/world.h"
#include "net/messages/zone.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/world_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_world(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::SendLoginInfo:
        if (const auto* x = payload<LoginInfo>(m)) return net::encode(*x);
        break;
    case GameOpcode::EnterWorld:
        if (const auto* x = payload<EnterWorld>(m)) return net::encode(*x);
        break;
    case GameOpcode::World_Client_CRC1:
    case GameOpcode::World_Client_CRC2:
    case GameOpcode::World_Client_CRC3:
        if (const auto* x = payload<ClientFileProofSource>(m)) {
            const GameOpcode expected = x->kind == ChecksumKind::Exe ? GameOpcode::World_Client_CRC1
                : x->kind == ChecksumKind::SkillCaps ? GameOpcode::World_Client_CRC2
                : GameOpcode::World_Client_CRC3;
            if (expected != opcode) return std::nullopt;
            return net::encode(x->kind, net::make_file_checksum(x->contents, x->seed));
        }
        break;
    case GameOpcode::WorldClientReady:
        return messages::message_is<std::monostate>(m) ? std::optional(net::make_empty(Op::WorldClientReady)) : std::nullopt;
    case GameOpcode::CharacterCreateRequest:
        return messages::message_is<std::monostate>(m) ? std::optional(net::make_empty(Op::CharacterCreateRequest)) : std::nullopt;
    case GameOpcode::WorldComplete:
        return messages::message_is<std::monostate>(m) ? std::optional(net::make_empty(Op::WorldComplete)) : std::nullopt;

    case GameOpcode::CharacterCreate:
        if (const auto* x = payload<CharCreate>(m)) return net::encode(*x);
        break;
    case GameOpcode::DeleteCharacter:
        if (const auto* x = payload<DeleteCharacter>(m)) return net::encode(*x);
        break;
    case GameOpcode::RandomNameGenerator:
        if (const auto* x = payload<RandomName>(m)) return net::encode(*x);
        break;
    case GameOpcode::ApproveName:
        if (const auto* x = payload<NameApproval>(m)) return net::encode(*x);
        break;
    case GameOpcode::ZoneEntry:
        if (const auto* x = payload<ZoneEntryRequest>(m)) {
            if (x->name.empty() || x->name.size() > 63) return std::nullopt;
            return net::encode(*x);
        }
        break;
    case GameOpcode::ReqClientSpawn:
    case GameOpcode::SendExpZonein:
    case GameOpcode::WorldObjectsSent:
    case GameOpcode::ClientReady:
        if (messages::message_is<std::monostate>(m)) {
            switch (opcode) {
            case GameOpcode::ReqClientSpawn: return net::make_empty(Op::ReqClientSpawn);
            case GameOpcode::SendExpZonein: return net::make_empty(Op::SendExpZonein);
            case GameOpcode::WorldObjectsSent: return net::make_empty(Op::WorldObjectsSent);
            case GameOpcode::ClientReady: return net::make_empty(Op::ClientReady);
            default: break;
            }
        }
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
