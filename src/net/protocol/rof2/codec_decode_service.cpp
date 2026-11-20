#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/player_text.h"
#include "net/messages/player_commands.h"
#include "net/messages/player_progress.h"
#include "net/messages/adventure.h"
#include "net/messages/task.h"
#include "net/messages/zone.h"
#include "net/messages/progression.h"
#include "net/messages/tribute.h"
#include "net/messages/training.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/aa_packets.h"
#include "net/protocol/rof2/packets/ability_packets.h"
#include "net/protocol/rof2/packets/adventure_packets.h"
#include "net/protocol/rof2/packets/dz_packets.h"
#include "net/protocol/rof2/packets/movement_packets.h"
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/packets/player_packets.h"
#include "net/protocol/rof2/packets/reward_packets.h"
#include "net/protocol/rof2/packets/social_packets.h"
#include "net/protocol/rof2/packets/spell_packets.h"
#include "net/protocol/rof2/packets/task_packets.h"
#include "net/protocol/rof2/packets/tracking_packets.h"
#include "net/protocol/rof2/packets/training_packets.h"
#include "net/protocol/rof2/packets/tribute_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

std::optional<GameMessage> decode_service(GameOpcode op, const AppPacket& p) {
    switch (op) {
    case GameOpcode::SharedTaskInvite:
        if (auto x = net::decode_shared_task_invite(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SendFindableNPCs:
        if (auto x = net::decode_findable_npc(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SpecialMesg:
        if (auto x = net::decode_special_message(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SimpleMessage:
        if (auto x = net::decode_simple_message(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::FormattedMessage:
        if (auto x = net::decode_formatted_message(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::PVPStats:
        if (auto x = net::decode_pvp_stats(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::PVPLeaderBoardReply:
        if (auto x = net::decode_pvp_leaderboard(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::CrystalCountUpdate:
        if (auto x = net::decode_crystal_counts(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::VetRewardsAvaliable:
        if (auto x = net::decode_vet_rewards(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::VetClaimReply:
        if (auto x = net::decode_vet_claim_reply(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::UpdateAura:
        if (auto x = net::decode_aura_update(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SendTitleList:
        if (auto x = net::decode_title_list(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::SetTitleReply:
        if (auto x = net::decode_title_reply(p)) return GameMessage{op, std::move(*x)};
        break;
    case GameOpcode::NewTitlesAvailable:
        if (p.data.empty()) return GameMessage{op, std::monostate{}};
        break;
    case GameOpcode::DuelAccept:
        if (p.data.size() == 8) {
            if (auto accepted = net::decode_duel_request(p)) return GameMessage{op, *accepted};
        }
        break;
    case GameOpcode::RequestDuel:
        if (auto x = net::decode_duel_request(p)) return GameMessage{op, *x};
        break;
    case GameOpcode::Surname:
        if (auto x = net::decode_surname(p)) return GameMessage{op, *x};
        break;
    case GameOpcode::Bandolier:
        if (auto x = net::decode_bandolier(p)) return GameMessage{op, *x};
        break;
    case GameOpcode::OnLevelMessage:
        if (auto x = net::decode_level_message(p)) return GameMessage{op, *x};
        break;

    case GameOpcode::InspectAnswer:
        if (auto x=net::decode_inspect_answer(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ReadBook:
        if (auto x=net::decode_read_book(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::TaskDescription:
        if (auto x=net::decode_task_description(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::TaskActivity:
        if (auto x=net::decode_task_activity(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::CompletedTasks:
        if (auto x=net::decode_completed_tasks(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::TaskSelectWindow: case GameOpcode::SharedTaskSelectWindow:
        if (auto x=net::decode_task_select(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::TaskHistoryReply:
        if (auto x=net::decode_task_history(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::TaskActivityComplete:
        if (auto x=net::decode_task_activity_complete(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::CancelTask:
        if (auto x=net::decode_cancel_task(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::SharedTaskMemberList:
        if (auto x=net::decode_shared_task_members(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::SharedTaskMemberChange:
        if (auto x=net::decode_shared_task_member_change(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::TaskRequestTimer:
        if (auto x=net::decode_task_request_timer(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::AdventureInfo: case GameOpcode::AdventureDetails:
        if (auto x=net::decode_adventure_text(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AdventureData:
        if (auto x=net::decode_adventure_data(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AdventureUpdate:
        if (auto x=net::decode_adventure_count(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AdventureFinish:
        if (auto x=net::decode_adventure_finish(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AdventurePointsUpdate:
        if (auto x=net::decode_adventure_points(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AdventureStatsReply:
        if (auto x=net::decode_adventure_stats(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AdventureLeaderboardReply:
        if (auto x=net::decode_adventure_leaderboard(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::DzExpeditionInfo:
        if (auto x=net::decode_dz_info(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::DzExpeditionInvite:
        if (auto x=net::decode_dz_invite(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::DzExpeditionLockoutTimers:
        if (auto x=net::decode_dz_lockouts(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::DzMemberList: case GameOpcode::DzMemberListStatus:
        if (auto x=net::decode_dz_members(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::DzMemberListName:
        if (auto x=net::decode_dz_member_name(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::DzSetLeaderName:
        if (auto x=net::decode_dz_leader(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::DzExpeditionEndsWarning:
        if (auto x=net::decode_dz_ends_warning(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::DzChooseZone:
        if (auto x=net::decode_dz_choose_zone(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::DzCompass:
        if (auto x=net::decode_dz_compass(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::Track:
        if (auto x=net::decode_track_reply(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::FindPersonReply:
        if (auto x=net::decode_find_person_reply(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::LFGuild:
        if (auto x=net::decode_lfguild(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::RewardSelection:
        if (auto value = net::decode_reward_packet(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::SkillUpdate:
        if (auto value=net::decode_skill_update(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::GMTraining:
        if (auto value=net::decode_gm_training(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::GMTrainSkillConfirm:
        if (auto value=net::decode_gm_train_skill_confirm(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::TributeInfo:
        if (auto value = net::decode_tribute_info(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::TributeUpdate:
        if (auto value = net::decode_tribute_settings(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::TributeTimer:
        if (auto value = net::decode_tribute_timer(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::TributePointUpdate:
        if (auto value = net::decode_tribute_points(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::SelectTribute:
        if (auto value = net::decode_tribute_description(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::OpenTributeMaster:
        if (auto value = net::decode_start_tribute(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::TributeItem:
        if (auto value = net::decode_tribute_item(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::TributeMoney:
        if (auto value = net::decode_tribute_money(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::SendGuildTributes:
        if (auto value = net::decode_guild_tribute_info(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::GuildSelectTribute:
        if (auto value = net::decode_guild_tribute_description(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::GuildTributeFavorAndTimer:
        if (auto value = net::decode_guild_tribute_favor(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::GuildOptInOut:
        if (auto value = net::decode_guild_tribute_opt(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::GuildTributeDonateItem:
        if (auto value = net::decode_guild_donate_item(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::GuildTributeDonatePlat:
        if (auto value = net::decode_guild_donate_platinum(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::OpenGuildTributeMaster:
        if (auto value = net::decode_start_tribute(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::GuildSendActiveTributes:
    case GameOpcode::GuildTributeToggleReply:
        if (auto value=net::decode_guild_tribute_active(p)) return GameMessage{op,std::move(*value)};
        break;
    case GameOpcode::GuildModifyBenefits:
        return GameMessage{op,std::monostate{}};
    case GameOpcode::SendAATable:
        if (auto value = net::decode_send_aa_table(p)) return GameMessage{op, std::move(*value)};
        break;
    case GameOpcode::RespondAA:
        if (auto value = net::decode_respond_aa(p)) return GameMessage{op, std::move(*value)};
        break;
    case GameOpcode::AAExpUpdate:
        if (auto value = net::decode_aa_exp_update(p)) return GameMessage{op, std::move(*value)};
        break;
    case GameOpcode::AAAction:
        if (auto value = net::decode_aa_timer(p)) return GameMessage{op, std::move(*value)};
        break;
    case GameOpcode::ClearAA:
        return GameMessage{op, std::monostate{}};
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
