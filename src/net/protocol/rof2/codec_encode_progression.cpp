#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/achievement.h"
#include "net/messages/reward.h"
#include "net/messages/tribute.h"
#include "net/messages/training.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/achievement_packets.h"
#include "net/protocol/rof2/packets/player_packets.h"
#include "net/protocol/rof2/packets/reward_packets.h"
#include "net/protocol/rof2/packets/training_packets.h"
#include "net/protocol/rof2/packets/tribute_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_progression(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::SendTributes:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_send_tributes()) : std::nullopt;
    case GameOpcode::RequestGuildTributes:
        return messages::message_is<std::monostate>(m) ? std::optional(net::encode_request_guild_tributes()) : std::nullopt;
    case GameOpcode::AchievementReward:
    case GameOpcode::RewardSelection:
        if (const auto* x = payload<RewardIntent>(m)) {
            switch (x->kind) {
            case RewardIntent::Kind::RequestPending:
                if (opcode == GameOpcode::AchievementReward) return net::encode_pending_rewards_request();
                break;
            case RewardIntent::Kind::PreviewTask:
                if (opcode == GameOpcode::RewardSelection) return net::encode_task_reward_preview(x->task);
                break;
            case RewardIntent::Kind::Claim:
                if (opcode == GameOpcode::AchievementReward) return net::encode_reward_claim(x->pending,x->set,x->option);
                break;
            case RewardIntent::Kind::Inspect:
                if ((x->lane == RewardLane::Pending) == (opcode == GameOpcode::AchievementReward))
                    return net::encode_reward_inspect(x->lane,x->set,x->option,x->entry,x->item);
                break;
            }
        }
        break;
    case GameOpcode::AchievementWindowRequest:
        if (messages::message_is<std::monostate>(m)) return net::encode_achievement_window_request();
        break;
    case GameOpcode::AchievementCompare:
        if (const auto* x = payload<AchievementCompareRequest>(m)) return net::encode_achievement_compare(x->target,x->player);
        break;
    case GameOpcode::AchievementLinkRequest:
        if (const auto* x = payload<AchievementLink>(m)) return net::encode_achievement_link_request(*x);
        break;
    case GameOpcode::AchievementRewardView:
        if (const auto* x = payload<AchievementRewardView>(m)) return net::encode_achievement_reward_view(x->definition_index);
        break;
    case GameOpcode::GMTraining:
        if (const auto* x = payload<GMTraining>(m)) return net::encode(*x);
        break;
    case GameOpcode::GMTrainSkill:
        if (const auto* x = payload<TrainingSkillRequest>(m))
            return net::encode_gm_train_skill(x->trainer,x->language,x->skill);
        break;
    case GameOpcode::GMEndTraining:
        if (const auto* x = payload<TrainingCloseRequest>(m))
            return net::encode_gm_end_training(x->trainer,x->player);
        break;
    case GameOpcode::OpenTributeMaster:
    case GameOpcode::OpenGuildTributeMaster:
        if (const auto* x = payload<StartTribute>(m)) {
            auto packet = net::encode(*x);
            if (opcode == GameOpcode::OpenGuildTributeMaster) packet.opcode = to_wire(Op::OpenGuildTributeMaster);
            return packet;
        }
        break;
    case GameOpcode::SelectTribute:
        if (const auto* x = payload<TributeSelection>(m)) return net::encode_select_tribute(x->player, x->tribute);
        break;
    case GameOpcode::TributeUpdate:
        if (const auto* x = payload<TributeSettings>(m)) return net::encode(*x);
        break;
    case GameOpcode::TributeToggle:
        if (const auto* x = payload<bool>(m)) return net::encode_tribute_toggle(*x);
        break;
    case GameOpcode::TributeItem:
        if (const auto* x = payload<TributeItem>(m)) return net::encode(*x);
        break;
    case GameOpcode::TributeMoney:
        if (const auto* x = payload<TributeMoney>(m)) return net::encode(*x);
        break;
    case GameOpcode::GuildSelectTribute:
        if (const auto* x = payload<TributeSelection>(m)) return net::encode_guild_select_tribute(x->tribute, x->tier);
        break;
    case GameOpcode::GuildSaveActiveTributes:
    case GameOpcode::GuildModifyBenefits:
        if (const auto* x = payload<GuildTributeSave>(m))
            return opcode == GameOpcode::GuildSaveActiveTributes
                ? net::encode_guild_save_tributes(x->master, x->benefits)
                : net::encode_guild_modify_benefits(x->modify, x->master, x->benefits);
        break;
    case GameOpcode::GuildTributeToggleReq:
        if (const auto* x = payload<bool>(m)) return net::encode_guild_tribute_toggle(*x);
        break;
    case GameOpcode::GuildOptInOut:
    case GameOpcode::GuildTrophyOptInOut:
        if (const auto* x = payload<GuildTributeMember>(m))
            return opcode == GameOpcode::GuildOptInOut
                ? net::encode_guild_tribute_opt(x->guild,x->participating,x->member)
                : net::encode_guild_trophy_opt(x->guild,x->participating,x->member);
        break;
    case GameOpcode::GuildTributeDonateItem:
        if (const auto* x = payload<GuildTributeItem>(m))
            return net::encode_guild_donate_item(x->slot,x->quantity,x->master,x->guild);
        break;
    case GameOpcode::GuildTributeDonatePlat:
        if (const auto* x = payload<GuildTributeCoins>(m))
            return net::encode_guild_donate_platinum(x->platinum,x->master,x->guild);
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
