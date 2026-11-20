#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/tracking.h"
#include "net/messages/dz.h"
#include "net/messages/adventure.h"
#include "net/messages/task.h"
#include "game/position_tracker.h"
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
#include "net/protocol/rof2/packets/adventure_packets.h"
#include "net/protocol/rof2/packets/dz_packets.h"
#include "net/protocol/rof2/packets/movement_packets.h"
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/packets/task_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_tasks(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::AcceptNewTask:
        if (const auto* x=payload<TaskAcceptIntent>(m)) return net::encode_accept_task(x->type,x->id,x->giver);
        break;
    case GameOpcode::SharedTaskAcceptNew:
        if (const auto* x=payload<SharedTaskAcceptIntent>(m)) return net::encode_shared_task_accept(x->giver,x->id,x->multiplier);
        break;
    case GameOpcode::CancelTask:
        if (const auto* x=payload<CancelTask>(m)) return net::encode_cancel_task(x->sequence,x->type);
        break;
    case GameOpcode::TaskHistoryRequest:
        if (const auto* x=payload<std::uint32_t>(m)) return net::encode_task_history_request(*x);
        break;
    case GameOpcode::SharedTaskAddPlayer: case GameOpcode::SharedTaskRemovePlayer: case GameOpcode::SharedTaskMakeLeader:
        if (const auto* x=payload<TaskMemberIntent>(m)) {
            if (opcode==GameOpcode::SharedTaskAddPlayer) return net::encode_shared_task_add_player(x->name);
            if (opcode==GameOpcode::SharedTaskRemovePlayer) return net::encode_shared_task_remove_player(x->name);
            return net::encode_shared_task_make_leader(x->name);
        }
        break;
    case GameOpcode::SharedTaskPlayerList:
        if (messages::message_is<std::monostate>(m)) return net::encode_shared_task_player_list();
        break;
    case GameOpcode::SharedTaskQuit:
        if (messages::message_is<std::monostate>(m)) return net::encode_shared_task_quit();
        break;
    case GameOpcode::TaskTimers:
        if (messages::message_is<std::monostate>(m)) return net::make_empty(Op::TaskTimers);
        break;
    case GameOpcode::AdventureInfoRequest:
        if (const auto* x=payload<std::uint32_t>(m)) return net::encode_adventure_info_request(*x);
        break;
    case GameOpcode::AdventureRequest:
        if (const auto* x=payload<AdventureRequestIntent>(m)) return net::encode_adventure_request(x->risk,x->npc,x->type);
        break;
    case GameOpcode::LDoNButton:
        if (const auto* x=payload<bool>(m)) return net::encode_ldon_button(*x);
        break;
    case GameOpcode::LeaveAdventure:
        if (messages::message_is<std::monostate>(m)) return net::make_empty(Op::LeaveAdventure);
        break;
    case GameOpcode::AdventureStatsRequest:
        if (messages::message_is<std::monostate>(m)) return net::encode_adventure_stats_request();
        break;
    case GameOpcode::AdventureLeaderboardRequest:
        if (const auto* x=payload<AdventureLeaderboardIntent>(m)) return net::encode_adventure_leaderboard_request(x->order,x->theme,x->risk);
        break;
    case GameOpcode::DzAddPlayer: case GameOpcode::DzRemovePlayer: case GameOpcode::DzMakeLeader:
        if (const auto* x=payload<std::string>(m)) {
            if (opcode==GameOpcode::DzAddPlayer) return net::encode_dz_add_player(*x);
            if (opcode==GameOpcode::DzRemovePlayer) return net::encode_dz_remove_player(*x);
            return net::encode_dz_make_leader(*x);
        }
        break;
    case GameOpcode::DzSwapPlayer:
        if (const auto* x=payload<DzSwapIntent>(m)) return net::encode_dz_swap_player(x->add,x->remove);
        break;
    case GameOpcode::DzQuit:
        if (messages::message_is<std::monostate>(m)) return net::encode_dz_quit();
        break;
    case GameOpcode::DzListTimers:
        if (messages::message_is<std::monostate>(m)) return net::encode_dz_list_timers();
        break;
    case GameOpcode::DzPlayerList:
        if (messages::message_is<std::monostate>(m)) return net::encode_dz_player_list();
        break;
    case GameOpcode::DzChooseZoneReply:
        if (const auto* x=payload<DzChoice>(m)) return net::encode_dz_choose_zone_reply(*x);
        break;
    case GameOpcode::DzExpeditionInviteResponse:
        if (const auto* x=payload<DzInviteReply>(m)) return net::encode_dz_invite_response(x->invite,x->accept);
        break;
    case GameOpcode::GroundSpawn:
        if (messages::message_is<std::monostate>(m)) return net::make_empty(Op::GroundSpawn);
        break;
    case GameOpcode::FloatListThing:
        if (const auto* samples = payload<std::vector<game::PositionTracker::HistoryEntry>>(m)) {
            hailstone::ByteWriter w;
            for (const auto& entry : *samples) {
                for (float component : entry.sample.position) w.f32(component);
                w.le<std::uint8_t>(static_cast<std::uint8_t>(entry.sample.reason));
                w.le<std::uint16_t>(static_cast<std::uint16_t>(entry.sample.time));
                w.le<std::uint16_t>(entry.ordinal);
            }
            w.le<std::uint8_t>(0); // original client includes one trailing byte
            return AppPacket(to_wire(Op::FloatListThing), w.take());
        }
        break;
    case GameOpcode::RespawnWindow:
        if (const auto* option = payload<std::uint32_t>(m)) return net::encode_respawn_selection(*option);
        break;
    case GameOpcode::SharedTaskInviteResponse:
        if (const auto* answer = payload<SharedTaskInviteAnswer>(m))
            return net::encode_shared_task_invite_response(answer->invite_id, answer->accepted);
        break;
    case GameOpcode::Track:
        if (messages::message_is<std::monostate>(m)) return net::make_empty(Op::Track);
        break;
    case GameOpcode::TrackTarget:
        if (const auto* x=payload<std::uint32_t>(m)) {
            hailstone::ByteWriter writer; writer.le(*x);
            return AppPacket(to_wire(Op::TrackTarget),writer.take());
        }
        break;
    case GameOpcode::FindPersonRequest:
        if (const auto* x=payload<FindPersonRequest>(m)) return net::encode(*x);
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
