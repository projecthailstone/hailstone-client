#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/zone.h"
#include "net/messages/social.h"
#include "net/messages/guild_banner.h"
#include "net/messages/shroud.h"
#include "net/protocol/rof2/packets/shroud_packets.h"
#include "net/protocol/rof2/packets/guild_banner_packets.h"
#include "net/messages/pet.h"
#include "net/messages/progression.h"
#include "net/messages/group.h"
#include "net/messages/fellowship.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/aa_packets.h"
#include "net/protocol/rof2/packets/fellowship_packets.h"
#include "net/protocol/rof2/packets/group_packets.h"
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/packets/pet_packets.h"
#include "net/protocol/rof2/packets/social_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_social(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::ShroudRequestStats:
        if (const auto* x=payload<ShroudIdRequest>(m)) return net::encode_shroud_request_stats(x->selection_id);
        break;
    case GameOpcode::ShroudSelect:
        if (const auto* x=payload<ShroudIdRequest>(m)) return net::encode_shroud_select(x->selection_id);
        break;
    case GameOpcode::ShroudSelectCancel:
        if (messages::message_is<std::monostate>(m)) return net::encode_shroud_cancel();
        break;
    case GameOpcode::ShroudRequestAbility:
        if (const auto* x=payload<ShroudDetailRequest>(m)) return net::encode_shroud_request_ability(x->selection_id,x->detail_id);
        break;
    case GameOpcode::ShroudRequestItem:
        if (const auto* x=payload<ShroudDetailRequest>(m)) return net::encode_shroud_request_item(x->selection_id,x->detail_id);
        break;
    case GameOpcode::MonsterMissionCatalogRequest:
        if (messages::message_is<std::monostate>(m)) return net::encode_monster_mission_catalog_request();
        break;
    case GameOpcode::MonsterMissionStatsRequest:
        if (const auto* x=payload<ShroudIdRequest>(m)) return net::encode_monster_mission_stats_request(x->selection_id);
        break;
    case GameOpcode::MonsterMissionAbilityRequest:
        if (const auto* x=payload<ShroudDetailRequest>(m)) return net::encode_monster_mission_ability_request(x->selection_id,x->detail_id);
        break;
    case GameOpcode::MonsterMissionItemRequest:
        if (const auto* x=payload<ShroudDetailRequest>(m)) return net::encode_monster_mission_item_request(x->selection_id,x->detail_id);
        break;
    case GameOpcode::MonsterMissionSelect:
        if (const auto* x=payload<ShroudIdRequest>(m)) return net::encode_monster_mission_select(x->selection_id);
        break;
    case GameOpcode::AltStorageOpen:
        if (const auto* x=payload<ShroudIdRequest>(m)) return net::encode_alt_storage_open(x->selection_id);
        break;
    case GameOpcode::AltStorageInspect:
        if (const auto* x=payload<AltStorageRequest>(m)) return net::encode_alt_storage_inspect(x->key,x->modifier);
        break;
    case GameOpcode::AltStorageWithdraw:
        if (const auto* x=payload<AltStorageRequest>(m)) return net::encode_alt_storage_withdraw(x->key);
        break;
    case GameOpcode::GuildBannerOpen:
        if (const auto* x=payload<GuildBannerOpen>(m)) return net::encode_guild_banner_open(x->guild_id,x->character_id);
        break;
    case GameOpcode::GuildBannerKitRequest:
        if (const auto* x=payload<GuildBannerKitRequest>(m)) return net::encode_guild_banner_kit_request(x->character_id,x->show_all,x->filter,x->owned);
        break;
    case GameOpcode::GuildBannerStatusRequest:
        if (const auto* x=payload<GuildBannerStatusRequest>(m)) return net::encode_guild_banner_status_request(x->guild_id,x->character_id);
        break;
    case GameOpcode::GuildBannerSave:
        if (const auto* x=payload<GuildBannerSave>(m)) return net::encode_guild_banner_save(x->character_id,x->style,x->token_id);
        break;
    case GameOpcode::GuildBannerPlant:
        if (const auto* x=payload<GuildBannerPlant>(m)) return net::encode_guild_banner_plant(x->kit_id,x->from_list,x->x,x->y,x->z,x->reapply_style);
        break;
    case GameOpcode::GuildBannerRemove:
        if (const auto* x=payload<GuildBannerRemove>(m)) return net::encode_guild_banner_remove(x->guild_id);
        break;

    case GameOpcode::LFGuild:
        if (const auto* x=payload<LFGuildPlayer>(m)) return net::encode(*x);
        if (const auto* x=payload<LFGuildGuild>(m)) return net::encode(*x);
        if (const auto* x=payload<LFGuildPlayerSearch>(m))
            return net::encode_lfguild_search_players(x->from_level,x->to_level,x->min_aa,x->time_zone,x->classes);
        if (const auto* x=payload<LFGuildGuildSearch>(m))
            return net::encode_lfguild_search_guilds(x->level,x->aa,x->time_zone,x->class_);
        break;
    case GameOpcode::LFGCommand:
        if (const auto* x=payload<LfgCommand>(m)) return net::encode(*x);
        break;
    case GameOpcode::LFPCommand:
        if (const auto* x=payload<LfpCommand>(m)) return net::encode(*x);
        break;
    case GameOpcode::LFGGetMatchesRequest:
        if (const auto* x=payload<GroupSearchQuery>(m)) return net::encode_lfg_matches_request(x->from_level,x->to_level,x->class_mask);
        break;
    case GameOpcode::LFPGetMatchesRequest:
        if (const auto* x=payload<GroupSearchQuery>(m)) return net::encode_lfp_matches_request(x->from_level,x->to_level);
        break;
    case GameOpcode::FellowshipRequest:
        if (const auto* x=payload<FellowshipIntent>(m)) return net::encode_fellowship_request(x->action,x->target,x->motd,x->camp_type);
        break;
    case GameOpcode::FellowshipCampRequest:
        if (const auto* x=payload<FellowshipKitQuery>(m)) return net::encode_fellowship_kit_request(x->available_only,x->category_mask);
        break;
    case GameOpcode::GuildInvite:
        if (const auto* x=payload<GuildCommand>(m)) return net::encode_guild_invite(*x);
        break;
    case GameOpcode::GuildRemove:
        if (const auto* x=payload<GuildCommand>(m)) return net::encode_guild_remove(*x);
        break;
    case GameOpcode::GuildInviteAccept:
        if (const auto* x=payload<GuildInviteAccept>(m)) return net::encode(*x);
        break;
    case GameOpcode::SetGuildMOTD:
        if (const auto* x=payload<GuildMotd>(m)) return net::encode_set_guild_motd(*x);
        break;
    case GameOpcode::GuildWar:
        if (const auto* x=payload<GuildWarPeace>(m)) return net::encode_guild_war(*x);
        break;
    case GameOpcode::GuildPeace:
        if (const auto* x=payload<GuildWarPeace>(m)) return net::encode_guild_peace(*x);
        break;
    case GameOpcode::GuildLeader: case GameOpcode::GuildPublicNote:
    case GameOpcode::GuildManageBanker: case GameOpcode::GuildUpdate: case GameOpcode::GetGuildMOTD:
        if (const auto* x=payload<GuildManagementIntent>(m)) {
            using A=GuildManagementIntent::Action;
            switch (x->action) {
            case A::SetUrl: if(opcode==GameOpcode::GuildUpdate) return net::encode_guild_update_text(0,x->text); break;
            case A::SetChannel: if(opcode==GameOpcode::GuildUpdate) return net::encode_guild_update_text(1,x->text); break;
            case A::RenameRank: if(opcode==GameOpcode::GuildUpdate) return net::encode_guild_rank_name(x->rank,x->text); break;
            case A::SetRankPermission: if(opcode==GameOpcode::GuildUpdate) return net::encode_guild_rank_permission(x->rank,x->permission,x->enabled,x->guild_id); break;
            case A::MakeLeader: if(opcode==GameOpcode::GuildLeader) return net::encode_guild_leader(x->actor,x->member); break;
            case A::ManageBanker: if(opcode==GameOpcode::GuildManageBanker) return net::encode_guild_manage_banker(x->member,x->flags); break;
            case A::PublicNote: if(opcode==GameOpcode::GuildPublicNote) return net::encode_guild_public_note(x->guild_id,x->actor,x->member,x->text); break;
            case A::RequestMotd: if(opcode==GameOpcode::GetGuildMOTD) return net::encode_get_guild_motd(x->guild_id,x->actor); break;
            }
        }
        break;
    case GameOpcode::RaidInvite:
        if (const auto* x=payload<RaidGeneral>(m)) return net::encode(*x);
        break;
    case GameOpcode::LeadershipExpToggle:
        if (const auto* x=payload<bool>(m)) return net::encode_leadership_exp_toggle(*x);
        break;
    case GameOpcode::PurchaseLeadershipAA:
        if (const auto* x=payload<std::uint32_t>(m)) return net::encode_purchase_leadership(*x);
        break;
    case GameOpcode::DoGroupLeadershipAbility:
        if (const auto* x=payload<GroupAbilityIntent>(m)) return net::encode_do_group_leadership_ability(x->ability,x->parameter);
        break;
    case GameOpcode::DelegateAbility:
    case GameOpcode::RaidDelegateAbility:
        if (const auto* x=payload<DelegateAbility>(m)) return net::encode(*x,opcode==GameOpcode::DelegateAbility?Op::DelegateAbility:Op::RaidDelegateAbility);
        break;
    case GameOpcode::GroupInvite:
    case GameOpcode::GroupInvite2:
        if (const auto* x=payload<GroupInvite>(m)) return net::encode(*x,opcode==GameOpcode::GroupInvite2);
        break;
    case GameOpcode::GroupFollow:
    case GameOpcode::GroupFollow2:
        if (const auto* x=payload<GroupFollow>(m)) return net::encode(*x,opcode==GameOpcode::GroupFollow2);
        break;
    case GameOpcode::CancelInvite:
        if (const auto* x=payload<GroupCancel>(m)) return net::encode(*x);
        break;
    case GameOpcode::GroupDisband:
        if (const auto* x=payload<GroupGeneric>(m)) return net::encode_group_generic(Op::GroupDisband,*x);
        break;
    case GameOpcode::GroupMakeLeader:
        if (const auto* x=payload<GroupMakeLeader>(m)) return net::encode(*x);
        break;
    case GameOpcode::GroupMentor:
        if (const auto* x=payload<GroupMentorIntent>(m)) return net::encode_group_mentor(x->percent,x->name);
        break;
    case GameOpcode::GroupRoles:
        if (const auto* x=payload<GroupRole>(m)) return net::encode_group_role(*x);
        break;
    case GameOpcode::SendAATable:
        if (messages::message_is<std::monostate>(m)) return net::make_empty(Op::SendAATable);
        break;
    case GameOpcode::UpdateAA:
        if (messages::message_is<std::monostate>(m)) return net::make_empty(Op::UpdateAA);
        break;
    case GameOpcode::AAAction:
        if (const auto* cmd = payload<AAIntent>(m)) {
            std::uint32_t action = 0;
            switch (cmd->operation) {
            case AAOperation::Activate: action = net::kAAActivate; break;
            case AAOperation::SetExperiencePercent: action = net::kAASetPercent; break;
            case AAOperation::DisableExperience: action = net::kAAExpOff; break;
            case AAOperation::TrainRank: action = net::kAATrain; break;
            case AAOperation::TrainAllRanks: action = net::kAATrainAll; break;
            }
            return net::encode(net::AAAction{action, cmd->rank_id, cmd->target_id, cmd->percent});
        }
        break;
    case GameOpcode::PetCommands:
        if (const auto* x = payload<PetCommandIntent>(m))
            return net::encode(net::PetCommandPacket{x->command, x->target,
                static_cast<std::uint8_t>(x->from_reissue ? 1 : 0)});
        break;
    case GameOpcode::ControlBoat:
        if (const auto* b = payload<BoatControl>(m)) {
            hailstone::ByteWriter w;
            w.le<std::uint32_t>(b->spawn_id);
            w.le<std::uint8_t>(b->take_helm ? 1 : 0);
            w.zeros(3);
            return AppPacket(to_wire(Op::ControlBoat), w.take());
        }
        break;
    case GameOpcode::YellForHelp:
        return messages::message_is<std::monostate>(m)
            ? std::optional(net::make_empty(Op::YellForHelp)) : std::nullopt;
    case GameOpcode::Rewind:
        return messages::message_is<std::monostate>(m)
            ? std::optional(net::make_empty(Op::Rewind)) : std::nullopt;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
