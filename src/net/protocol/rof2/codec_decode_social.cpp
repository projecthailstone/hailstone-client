#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/social.h"
#include "net/messages/guild_banner.h"
#include "net/messages/shroud.h"
#include "net/protocol/rof2/packets/shroud_packets.h"
#include "net/protocol/rof2/packets/guild_banner_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"
#include "net/messages/group.h"
#include "net/messages/fellowship.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/fellowship_packets.h"
#include "net/protocol/rof2/packets/group_packets.h"
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/packets/social_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

std::optional<GameMessage> decode_social(GameOpcode op, const AppPacket& p) {
    switch (op) {
    case GameOpcode::ShroudSelectionWindow:
        if (auto x=net::decode_shroud_selection(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ShroudRespondStats:
        if (auto x=net::decode_shroud_stats(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ShroudRespondAbility:
        if (auto x=net::decode_shroud_ability(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ShroudRespondItem:
        if (auto x=net::decode_shroud_item(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ShroudProgress:
        if (auto x=net::decode_shroud_progress(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ShroudProgress2:
        if (auto x=net::decode_shroud_progress(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::Shroud:
        if (auto x=net::decode_shroud_transition(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ShroudRemove:
        if (p.data.empty()) return GameMessage{op,std::monostate{}};
        break;
    case GameOpcode::MonsterMissionCatalog:
        if (auto x=net::decode_monster_mission_catalog(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MonsterMissionStats:
        if (auto x=net::decode_monster_mission_stats(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MonsterMissionAbility:
        if (auto x=net::decode_monster_mission_ability(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MonsterMissionItem:
        if (auto x=net::decode_monster_mission_item(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MonsterMissionAssignment:
        if (auto x=net::decode_monster_mission_assignment(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::MonsterMissionResult:
        if (auto x=net::decode_monster_mission_result(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AltStorage:
        if (auto x=net::decode_alt_storage(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AltStorageList:
        if (auto x=net::decode_alt_storage_list(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::AltStorageNoItems:
        if (p.data.empty()) return GameMessage{op,std::monostate{}};
        break;
    case GameOpcode::GuildBannerKitList:
        if (auto x=net::decode_guild_banner_kits(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GuildBannerStatus:
        if (auto x=net::decode_guild_banner_status(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GuildBannerModificationToken:
        if (auto x=net::decode_guild_banner_token(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GuildBannerAppearance:
        if (auto x=net::decode_guild_banner_appearance(p)) return GameMessage{op,std::move(*x)};
        break;

    case GameOpcode::Emote:
        if (auto x=net::decode_emote(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::WhoAllResponse:
        if (auto x=net::decode_who_response(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::RandomReply:
        if (auto x=net::decode_random_reply(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::Assist:
        if (auto x=net::decode_assist(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::GuildMOTD: case GameOpcode::GetGuildMOTDReply:
        if (auto x=net::decode_guild_motd(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GuildMemberList:
        if (auto x=net::decode_guild_member_list(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GuildMemberAdd: case GameOpcode::GuildMemberDelete:
    case GameOpcode::GuildMemberLevel: case GameOpcode::GuildMemberPublicNote:
        if (auto x=net::decode_guild_roster_change(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GuildInvite:
        if (auto x=net::decode_guild_command(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::ConsentResponse: case GameOpcode::DenyResponse:
        if (auto x=net::decode_consent_response(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::LFGGetMatchesResponse:
        if (auto x=net::decode_lfg_matches(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::LFPGetMatchesResponse:
        if (auto x=net::decode_lfp_matches(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::FellowshipRoster:
        if (auto x=net::decode_fellowship_roster(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::FellowshipKitList:
        if (auto x=net::decode_fellowship_kits(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::FellowshipVitality:
        if (auto x=net::decode_fellowship_vitality(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::FellowshipInvite:
        if (auto x=net::decode_fellowship_invite(p)) return GameMessage{op,std::move(*x)};
        break;

    case GameOpcode::GroupLeadershipAAUpdate:
        if (auto x=net::decode_group_leadership_update(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::DelegateAbility: case GameOpcode::RaidDelegateAbility:
        if (auto x=net::decode_delegate_ability(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::SetGroupTarget: case GameOpcode::MarkNPC: case GameOpcode::MarkRaidNPC:
        if (auto x=net::decode_mark_npc(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::ClearNPCMarks: case GameOpcode::RaidClearNPCMarks:
        return GameMessage{op,std::monostate{}};
    case GameOpcode::InspectBuffs:
        if (auto x=net::decode_inspect_buffs(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::GroupInvite: case GameOpcode::GroupInvite2:
        if (auto x=net::decode_group_invite(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GroupFollow: case GameOpcode::GroupFollow2:
        if (auto x=net::decode_group_follow(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::CancelInvite:
        if (auto x=net::decode_group_cancel(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GroupUpdate:
        if (auto x=net::decode_group_join(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GroupUpdateB:
        if (auto x=net::decode_group_update_b(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GroupRoles:
        if (auto x=net::decode_group_role(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GroupLeaderChange:
        if (auto x=net::decode_group_leader_change(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::GroupAcknowledge: case GameOpcode::GroupDisbandYou:
    case GameOpcode::GroupDisbandOther: case GameOpcode::GroupDelete:
        if (auto x=net::decode_group_generic(p)) return GameMessage{op,std::move(*x)};
        break;
    case GameOpcode::UpdateLeadershipAA:
        if (auto x=net::decode_leadership_rank(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::LeadershipExpUpdate:
        if (auto x=net::decode_leadership_exp(p)) return GameMessage{op,*x};
        break;
    case GameOpcode::MobManaUpdate: case GameOpcode::MobEnduranceUpdate:
        if (auto x=net::decode_mob_percent(p)) return GameMessage{op,*x};
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
