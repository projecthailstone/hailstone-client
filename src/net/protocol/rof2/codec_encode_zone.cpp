#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/zone.h"
#include "net/messages/door.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/aggro_packets.h"
#include "net/protocol/rof2/packets/door_packets.h"
#include "net/protocol/rof2/packets/group_packets.h"
#include "net/protocol/rof2/packets/movement_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_zone(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::Camp:
        if (const auto* x = payload<CampIntent>(m)) return net::encode_camp(x->kind);
        break;
    case GameOpcode::Logout:
        return messages::message_is<std::monostate>(m) ? std::optional(net::make_empty(Op::Logout)) : std::nullopt;
    case GameOpcode::Save:
        if (const auto* x = payload<SaveRequest>(m)) return net::encode_save(*x);
        break;
    case GameOpcode::SaveOnZoneReq:
        if (const auto* x = payload<SaveRequest>(m)) return net::encode_save_on_zone(*x);
        break;
    case GameOpcode::DeleteSpawn:
        if (const auto* x = payload<SpawnDespawn>(m)) return net::encode_delete_own_spawn(x->spawn_id);
        break;
    case GameOpcode::Jump:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_jump()) : std::nullopt;
    case GameOpcode::SafeFallSuccess:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_safe_fall_success()) : std::nullopt;
    case GameOpcode::EnvDamage:
        if (const auto* x = payload<EnvironmentalDamage>(m))
            return net::encode(net::EnvDamage{x->spawn_id, x->damage, x->kind});
        break;
    case GameOpcode::SetRunMode:
        if (const auto* x = payload<RunMode>(m)) return net::encode_set_run_mode(x->running);
        break;
    case GameOpcode::AckPacket:
        if (const auto* x = payload<ViewActorAcknowledgement>(m)) {
            hailstone::ByteWriter writer;
            writer.le(x->spawn_id);
            return AppPacket(to_wire(Op::AckPacket), writer.take());
        }
        break;
    case GameOpcode::ClickDoor:
        if (const auto* x = payload<ClickDoor>(m)) return net::encode(*x);
        break;
    case GameOpcode::ClickObject:
        if (const auto* x = payload<ClickObject>(m)) return net::encode(*x);
        break;
    case GameOpcode::ClickObjectAction:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_close_object()) : std::nullopt;
    case GameOpcode::TargetMouse:
    case GameOpcode::TargetCommand:
        if (const auto* x = payload<TargetSelection>(m)) return net::encode_target(opcode == GameOpcode::TargetCommand, x->spawn_id);
        break;
    case GameOpcode::XTargetRequest:
        if (const auto* x = payload<ExtendedTargetSlot>(m))
            return net::encode_xtarget_request(x->slot, x->type, x->name);
        break;
    case GameOpcode::XTargetAutoAddHaters:
        if (const auto* x = payload<ExtendedTargetAutoHaters>(m))
            return AppPacket(to_wire(Op::XTargetAutoAddHaters), {static_cast<std::byte>(x->enabled ? 1 : 0)});
        break;
    case GameOpcode::AggroMeterLockTarget:
        if (const auto* x = payload<AggroTargetLock>(m)) return net::encode_aggro_lock(x->spawn_id);
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
