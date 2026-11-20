#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/zone.h"
#include "net/messages/door.h"
#include "net/messages/social.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/aggro_packets.h"
#include "net/protocol/rof2/packets/door_packets.h"
#include "net/protocol/rof2/packets/group_packets.h"
#include "net/protocol/rof2/packets/missile_packets.h"
#include "net/protocol/rof2/packets/player_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"
#include "net/protocol/rof2/packets/zoning_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

std::optional<GameMessage> decode_zone_state(GameOpcode op, const AppPacket& p) {
    switch (op) {
    case GameOpcode::MobRename:
        if (auto x = net::decode_mob_rename(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::DeleteSpawn:
        if (auto id = net::decode_delete_spawn(p))
            return GameMessage{op, SpawnDespawn{*id, p.data.size() > 4 && p.data[4] != std::byte{0}}};
        break;
    case GameOpcode::RequestClientZoneChange:
        if (auto x = net::decode_request_client_zone_change(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SetFace:
        if (auto x = net::decode_face_change(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::Illusion:
        if (auto x = net::decode_illusion(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ChangeSize:
        if (auto x = net::decode_change_size(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::PlayerStateAdd:
        if (auto x = net::decode_player_state(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::PlayerStateRemove:
        if (auto x = net::decode_player_state(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SpawnDoor:
        if (auto x = net::decode_spawn_doors(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::MoveDoor:
        if (auto x = net::decode_move_door(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::GroundSpawn:
        if (auto x = net::decode_ground_object(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ClickObject:
        if (auto x = net::decode_click_object(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ClickObjectAction:
        if (auto x = net::decode_click_object_action(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ClearObject:
        if (auto x = net::decode_clear_object(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SendZonepoints:
        if (auto x = net::decode_zone_points(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::RemoveAllDoors:
        if (p.data.empty()) return GameMessage{op, std::monostate{}};
        break;
    case GameOpcode::TargetCommand:
        if (auto id = net::decode_target(p)) return GameMessage{op, TargetSelection{*id}};
        break;
    case GameOpcode::XTargetResponse:
        if (auto x = net::decode_xtarget_response(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::AggroMeterTargetInfo:
        if (auto x = net::decode_aggro_target_info(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::AggroMeterUpdate:
        if (auto x = net::decode_aggro_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::MobHealth:
        if (auto x = net::decode_mob_health(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::HPUpdate:
        if (auto x = net::decode_hp_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ManaChange:
        if (auto x = net::decode_mana_change(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ManaUpdate:
        if (auto x = net::decode_points_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::EnduranceUpdate:
        if (auto x = net::decode_points_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ExpUpdate:
        if (auto x = net::decode_exp_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::LevelUpdate:
        if (auto x = net::decode_level_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::Consider:
        if (auto x = net::decode_consider(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::Damage:
        if (auto x = net::decode_combat_damage(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::Death:
        if (auto x = net::decode_death(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::Animation:
        if (auto x = net::decode_animation(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::RespawnWindow:
        if (auto x = net::decode_respawn_window(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ZonePlayerToBind: {
        auto bytes = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(p.data.data()), p.data.size());
        if (auto value = net::rof2::decode_zone_to_bind(bytes))
            return GameMessage{op, BindZoneTransition{std::move(*value), p.data.size()}};
        break;
    }
    case GameOpcode::SomeItemPacketMaybe:
        if (auto value = net::rof2::decode_missile_packet(p.data))
            return GameMessage{op, std::move(*value)};
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
