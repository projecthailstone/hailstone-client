#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/world.h"
#include "net/messages/zone.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/social_packets.h"
#include "net/protocol/rof2/packets/world_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

std::optional<GameMessage> decode_world_zone(GameOpcode op, const AppPacket& p) {
    switch (op) {
    case GameOpcode::ApproveWorld:
        // The 544-byte server approval record has no client-used fields.
        // Its presence is an access-grant notification, not application data.
        if (p.data.size() == 544) return GameMessage{op, std::monostate{}};
        break;
    case GameOpcode::PostEnterWorld:
        // Stock EQEmu's final world-authentication notification is empty.
        if (p.data.empty()) return GameMessage{op, std::monostate{}};
        break;
    case GameOpcode::SendMaxCharacters:
        if (auto x = net::decode_max_characters(p)) return GameMessage{op, *x};
        break;
    case GameOpcode::LogServer:
        if (auto x = net::decode_log_server(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SendCharInfo:
        if (auto x = net::decode_character_select(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SendMembership:
        if (auto x = net::decode_membership(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SendMembershipDetails:
        if (auto x = net::decode_membership_details(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::GuildsList:
        if (auto x = net::decode_guilds_list(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ExpansionInfo:
        if (auto x = net::decode_expansion_info(p)) return GameMessage{op, *x};
        break;
    case GameOpcode::ApproveName:
        if (auto x = net::decode_approve_name_reply(p)) return GameMessage{op, *x};
        break;
    case GameOpcode::CharacterCreateRequest:
        if (auto x = net::decode_char_create_data(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::RandomNameGenerator:
        if (auto x = net::decode_random_name(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::MOTD:
        if (auto x = net::decode_motd(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::EnterWorld:
        if (auto x = net::decode_enter_world_name(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ZoneUnavail:
        if (auto x = net::decode_zone_unavail(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SetChatServer:
    case GameOpcode::SetChatServer2:
        if (auto x = net::decode_chat_server(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ZoneServerInfo:
        if (auto x = net::decode_zone_server_info(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ZoneEntry:
    case GameOpcode::NewSpawn:
        // Incoming OP_ZoneEntry contains a spawn, not our outbound zone entry
        // request. RoF2's wire translator names both directions identically.
        if (auto x = net::decode_spawn(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::PlayerProfile:
        if (auto x = net::decode_player_profile(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SendExpZonein:
    case GameOpcode::SendAAStats:
    case GameOpcode::ClientReady:
    case GameOpcode::WorldObjectsSent:
        // EQEmu sends these as empty notifications. Server ClientReady is
        // distinct from the client command of the same wire opcode.
        if (p.data.empty()) return GameMessage{op, std::monostate{}};
        break;
    case GameOpcode::NewZone:
        if (auto x = net::decode_new_zone(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::TimeOfDay:
        if (auto x = net::decode_time_of_day(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::Weather:
        if (auto x = net::decode_weather(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ClientUpdate:
        if (auto x = net::decode_spawn_position_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::NPCMoveUpdate:
        if (auto x = net::decode_npc_move_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SpawnAppearance:
        if (auto x = net::decode_spawn_appearance(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::WearChange:
        if (auto x = net::decode_wear_change(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ChannelMessage:
        if (auto x = net::decode_channel_message(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::ZoneChange:
        if (auto x = net::decode_zone_change(p)) return GameMessage{op, std::move(*x)};
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
