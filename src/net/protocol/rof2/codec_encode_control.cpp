#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/player_text.h"
#include "net/messages/player_commands.h"
#include "net/messages/zone.h"
#include "net/messages/social.h"
#include "net/messages/spell.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/ability_packets.h"
#include "net/protocol/rof2/packets/player_packets.h"
#include "net/protocol/rof2/packets/social_packets.h"
#include "net/protocol/rof2/packets/spell_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_control(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::SwapSpell:
        if (const auto* x = payload<SpellBookSwap>(m)) return net::encode(SwapSpell{x->from_slot,x->to_slot});
        break;
    case GameOpcode::Bug:
        if (const auto* x = payload<BugReport>(m)) return net::encode(*x);
        break;
    case GameOpcode::Consider:
        if (const auto* x = payload<Consider>(m)) return net::encode(*x);
        break;
    case GameOpcode::PVPLeaderBoardRequest:
        if (const auto* x = payload<std::uint32_t>(m)) return net::encode_pvp_leaderboard_request(*x);
        break;
    case GameOpcode::VetClaimRequest:
        if (const auto* x = payload<std::uint32_t>(m)) return net::encode_vet_claim(*x);
        break;
    case GameOpcode::Dye:
        if (const auto* x = payload<DyeIntent>(m)) return net::encode_dye(x->tints);
        break;
    case GameOpcode::UpdateAura:
        if (const auto* x = payload<AuraUpdate>(m)) return net::encode(*x);
        break;
    case GameOpcode::ClearNPCMarks: case GameOpcode::RaidClearNPCMarks:
        if (messages::message_is<std::monostate>(m))
            return net::make_empty(opcode == GameOpcode::ClearNPCMarks ? Op::ClearNPCMarks : Op::RaidClearNPCMarks);
        break;
    case GameOpcode::GuildDelete:
        if (const auto* x = payload<GuildDeleteIntent>(m)) {
            hailstone::ByteWriter w; w.le(x->guild_id); w.fixed_string(x->character.substr(0,63),64);
            return net::make_packet(Op::GuildDelete,std::move(w));
        }
        break;
    case GameOpcode::DuelDecline:
        if (const auto* x = payload<DuelDeclineIntent>(m))
            return x->eqemu_response ? net::encode_duel_decline_eqemu(x->partner,x->player)
                                     : net::encode_duel_decline(x->partner,x->player);
        break;
    case GameOpcode::GMSummon: case GameOpcode::CorpseDrag:
        if (const auto* x = payload<CorpseCommand>(m))
            return net::encode_corpse_command(opcode == GameOpcode::GMSummon ? Op::GMSummon : Op::CorpseDrag,*x);
        break;
    case GameOpcode::CorpseDrop:
        if (const auto* x = payload<std::string>(m)) return net::encode_corpse_drop(*x);
        break;
    case GameOpcode::Shielding:
        if (const auto* x = payload<std::uint32_t>(m)) return net::encode_shielding(*x);
        break;
    case GameOpcode::KeyRing:
        if (messages::message_is<std::monostate>(m)) return net::encode_key_ring_request();
        break;
    case GameOpcode::ResetAA:
        if (messages::message_is<std::monostate>(m)) return net::encode_reset_aa();
        break;
    case GameOpcode::ClearSurname:
        if (messages::message_is<std::monostate>(m)) return net::make_empty(Op::ClearSurname);
        break;
    case GameOpcode::PopupResponse:
        if (const auto* x = payload<PopupAnswer>(m)) return net::encode_popup_response(x->kind,x->id);
        break;
    case GameOpcode::Bandolier:
        if (const auto* x = payload<BandolierPacket>(m)) return net::encode_bandolier(x->action,x->number,x->name);
        if (const auto* x = payload<BandolierHoldIntent>(m)) return net::encode_bandolier_hold(x->set,x->slot,x->shift);
        break;
    case GameOpcode::PotionBelt:
        if (const auto* x = payload<PotionBeltIntent>(m))
            return x->hold ? net::encode_potion_belt_hold(x->slot,x->shift)
                           : net::encode_potion_belt(x->clear,x->slot,x->item_id);
        break;
    case GameOpcode::RequestTitles:
        if (messages::message_is<std::monostate>(m)) return net::encode_request_titles();
        break;
    case GameOpcode::SetTitle:
        if (const auto* x = payload<TitleSelection>(m)) return net::encode_set_title(x->suffix, x->title_id);
        break;
    case GameOpcode::RequestDuel:
        if (const auto* x = payload<DuelRequest>(m)) return net::encode(*x);
        break;
    case GameOpcode::DuelAccept:
        if (const auto* x = payload<DuelRequest>(m)) return net::encode_duel_accept(x->first, x->second);
        break;
    case GameOpcode::Surname:
        if (const auto* x = payload<Surname>(m)) return net::encode(*x);
        break;
    case GameOpcode::SetServerFilter:
        if (const auto* x = payload<ServerFilterSelection>(m)) {
            hailstone::ByteWriter w;
            for (auto value : x->values) w.le<std::uint32_t>(value);
            return net::make_packet(Op::SetServerFilter, std::move(w));
        }
        break;
    case GameOpcode::GuildDemote:
        if (const auto* x = payload<GuildRankDemotion>(m)) {
            hailstone::ByteWriter w;
            w.fixed_string(x->actor.substr(0, 63), 64);
            w.fixed_string(x->member.substr(0, 63), 64);
            w.le<std::uint32_t>(x->new_rank);
            return net::make_packet(Op::GuildDemote, std::move(w));
        }
        break;
    case GameOpcode::GuildCreate:
        if (const auto* x = payload<std::string>(m)) {
            hailstone::ByteWriter w; w.fixed_string(x->substr(0,63),64);
            return net::make_packet(Op::GuildCreate, std::move(w));
        }
        break;
    case GameOpcode::GuildStatus:
        if (const auto* x = payload<std::string>(m)) {
            hailstone::ByteWriter w; w.fixed_string(x->substr(0,63),64); w.zeros(76);
            return net::make_packet(Op::GuildStatus, std::move(w));
        }
        break;
    case GameOpcode::GuildPromote:
        if (const auto* x = payload<GuildPromote>(m)) return net::encode(*x);
        break;
    case GameOpcode::RaidNote:
        if (const auto* x = payload<RaidNoteCommand>(m)) return net::encode_set_raid_note(x->actor,x->raid_id,x->member,x->note);
        break;
    case GameOpcode::RaidMotd:
        if (const auto* x = payload<RaidMotdCommand>(m)) return net::encode_set_raid_motd(x->actor,x->motd);
        break;
    case GameOpcode::PlayerStateAdd: case GameOpcode::PlayerStateRemove:
        if (const auto* x = payload<PlayerState>(m)) return net::encode_player_state(opcode == GameOpcode::PlayerStateAdd, *x);
        break;
    case GameOpcode::SetStartCity:
        if (const auto* x = payload<std::string>(m)) return net::encode_set_start_city(*x);
        break;
    case GameOpcode::LDoNOpen:
        if (messages::message_is<std::monostate>(m)) return net::make_empty(Op::LDoNOpen);
        break;
    case GameOpcode::HideCorpse:
        if (const auto* x = payload<HideCorpseIntent>(m)) return net::encode_hide_corpse(x->action,x->corpse);
        break;
    case GameOpcode::TGB:
        if (const auto* x = payload<std::uint32_t>(m)) {
            hailstone::ByteWriter w; w.le(*x); return net::make_packet(Op::TGB, std::move(w));
        }
        break;
    case GameOpcode::Petition:
        if (const auto* x = payload<std::string>(m)) return net::encode_petition(*x);
        break;
    case GameOpcode::Report:
        if (const auto* x = payload<std::string>(m)) return net::encode_report(*x);
        break;
    case GameOpcode::Feedback:
        if (const auto* x = payload<Feedback>(m)) return net::encode(*x);
        break;
    case GameOpcode::SetFace:
        if (const auto* x = payload<FaceChange>(m)) return net::encode(*x);
        break;
    case GameOpcode::Hide:
        if (messages::message_is<std::monostate>(m)) return net::encode_hide_cancel();
        break;
    case GameOpcode::ReLeave:
        if (messages::message_is<std::monostate>(m))
            return AppPacket(to_wire(Op::ReLeave), std::vector<std::byte>(4));
        break;
    case GameOpcode::UptimeRequest:
        if (messages::message_is<std::monostate>(m)) return AppPacket(to_wire(Op::UptimeRequest));
        break;
    case GameOpcode::ServerTransferRequest:
        if (messages::message_is<std::monostate>(m)) return AppPacket(to_wire(Op::ServerTransferRequest));
        break;
    case GameOpcode::InspectRequest:
        if (const auto* x=payload<InspectRequest>(m)) return net::encode_inspect_request(x->target,x->player);
        break;
    case GameOpcode::InspectMessageUpdate:
        if (const auto* x=payload<InspectMessageIntent>(m)) return net::encode_inspect_message(x->text);
        break;
    case GameOpcode::ReadBook:
        if (const auto* x=payload<ReadBook>(m)) return net::encode(*x);
        break;
    case GameOpcode::BookButton:
        if (const auto* x=payload<BookButtonIntent>(m)) return net::encode_book_button(x->slot,x->sub,x->aug,x->target);
        break;
    case GameOpcode::LDoNInspect:
        if (messages::message_is<std::monostate>(m)) return net::make_empty(Op::LDoNInspect);
        break;
    case GameOpcode::WhoAllRequest:
        if (const auto* x=payload<WhoRequest>(m)) return net::encode(*x);
        break;
    case GameOpcode::FriendsWho:
        if (const auto* x=payload<FriendsWhoIntent>(m)) return net::encode_friends_who(x->friends,x->all,x->server);
        break;
    case GameOpcode::RandomReq:
        if (const auto* x=payload<RandomRollIntent>(m)) return net::encode_random_request(x->low,x->high);
        break;
    case GameOpcode::Consent:
    case GameOpcode::ConsentDeny:
        if (const auto* x=payload<ConsentIntent>(m)) return net::encode_consent(opcode == GameOpcode::ConsentDeny,x->name);
        break;
    case GameOpcode::Emote:
        if (const auto* x=payload<SocialEmoteIntent>(m)) return net::encode_social_emote(x->text);
        break;
    case GameOpcode::Assist:
        if (const auto* x=payload<std::uint32_t>(m)) return net::encode_assist(*x);
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
