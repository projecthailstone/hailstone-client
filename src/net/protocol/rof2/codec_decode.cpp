#include "net/protocol/rof2/codec.h"
#include "net/protocol/rof2/codec_dispatch.h"
#include "net/protocol/rof2/codec_mapping.h"
#include "net/messages/zone_offer.h"
#include "net/messages/mercenary.h"
#include "net/messages/name_change.h"
#include "net/messages/player_commands.h"
#include "net/messages/ability.h"
#include "net/messages/achievement.h"
#include "net/messages/social.h"
#include "net/messages/reward.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/packets/merc_packets.h"
#include "net/protocol/rof2/packets/achievement_packets.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/packets/player_packets.h"
#include "net/protocol/rof2/packets/reward_packets.h"
#include "net/protocol/rof2/packets/social_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hailstone::net::rof2 {
using namespace messages;
using protocol::CodecDirection;
namespace {
CodecError error(CodecErrorKind kind, std::string detail) { return {kind, std::move(detail)}; }
}

CodecResult<MessageBatch> Codec::decode(ServiceScope scope, const AppPacket& packet) const {
    if (!capabilities().accepts(scope))
        return CodecResult<MessageBatch>::failure(error(CodecErrorKind::UnsupportedOpcode, "RoF2 codec does not implement this service"));
    if (scope == ServiceScope::Zone &&
        (packet.opcode == to_wire(Op::Translocate) || packet.opcode == to_wire(Op::RezzRequest))) {
        ZoneOffer offer;
        if (packet.opcode == to_wire(Op::Translocate)) {
            auto decoded = net::decode_translocate(packet);
            if (!decoded) return CodecResult<MessageBatch>::failure(
                error(CodecErrorKind::MalformedPacket, "invalid RoF2 translocation offer"));
            offer.kind = ZoneOfferKind::Translocate;
            offer.zone = decoded->zone;
            offer.source = decoded->caster;
        } else {
            auto decoded = net::decode_rezz_request(packet);
            if (!decoded) return CodecResult<MessageBatch>::failure(
                error(CodecErrorKind::MalformedPacket, "invalid RoF2 resurrection offer"));
            offer.kind = ZoneOfferKind::Resurrection;
            offer.zone = decoded->zone;
            offer.instance = decoded->instance;
            offer.source = decoded->rezzer;
        }
        if (zone_offers_.size() >= 32) zone_offers_.erase(zone_offers_.begin());
        offer.token = next_offer_token_++;
        zone_offers_.emplace(offer.token, packet);
        const auto op = offer.kind == ZoneOfferKind::Translocate ? GameOpcode::Translocate : GameOpcode::RezzRequest;
        return CodecResult<MessageBatch>::success({{op, std::move(offer)}});
    }
    if (scope == ServiceScope::Zone && packet.opcode == net::kOpDuelRefusal) {
        if (auto v = net::decode_duel_refusal(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::DuelRefusal, *v}});
        return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket, "invalid RoF2 duel refusal"));
    }
    if (scope == ServiceScope::Zone) {
        // Original RoF2 client control notifications not represented in EQEmu's stock opcode table.
        // These are decoded before the ordinary opcode-map dispatch; GameConnection never sees wire numbers.
        if (packet.opcode == net::kOpOfflineModeRefused)
            return CodecResult<MessageBatch>::success({{GameOpcode::NetworkControl,
                NetworkControlNotice{NetworkControlNotice::Kind::OfflineRefused, false, {}}}});
        if (packet.opcode == net::kOpUptime) {
            if (auto value = net::decode_uptime(packet))
                return CodecResult<MessageBatch>::success({{GameOpcode::UptimeReply,*value}});
            return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,"invalid original RoF2 uptime notice"));
        }
        if (packet.opcode == net::kOpSplitCoin) {
            if (auto value = net::decode_split_coin(packet))
                return CodecResult<MessageBatch>::success({{GameOpcode::SplitCoinCredit,*value}});
            return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,"invalid original RoF2 coin credit"));
        }
        if (packet.opcode == 0xcadf)
            return CodecResult<MessageBatch>::success({{GameOpcode::TeleportIndex,std::monostate{}}});
        if (packet.opcode == 0x57da || packet.opcode == 0x4d09 ||
            packet.opcode == net::kOpChatBlockOn || packet.opcode == net::kOpChatBlockOff) {
            std::string character;
            for (std::byte b : packet.data) {
                if (b == std::byte{0}) break;
                character.push_back(static_cast<char>(b));
                if (character.size() >= 64) break; // client-name bound; do not retain arbitrary payloads
            }
            const bool attack = packet.opcode == 0x57da || packet.opcode == 0x4d09;
            NetworkControlNotice value{attack ? NetworkControlNotice::Kind::AttackLock :
                NetworkControlNotice::Kind::ChatBlock,
                packet.opcode == 0x57da || packet.opcode == net::kOpChatBlockOn,
                std::move(character)};
            return CodecResult<MessageBatch>::success({{GameOpcode::NetworkControl,std::move(value)}});
        }
    }
    MessageBatch results;
    if (scope == ServiceScope::Zone) {
        NameChangeNotice notice;
        bool name_wire = true;
        if (packet.opcode == to_wire(Op::InvokeNameChangeImmediate)) {
            notice.subject=NameSubject::Character;notice.prompt=true;notice.immediate=true;
        } else if (packet.opcode == to_wire(Op::InvokeNameChangeLazy)) {
            notice.subject=NameSubject::Character;notice.prompt=true;
        } else if (packet.opcode == to_wire(Op::InvokeChangePetNameImmediate)) {
            notice.subject=NameSubject::Pet;notice.prompt=true;notice.immediate=true;
        } else if (packet.opcode == to_wire(Op::InvokeChangePetName)) {
            notice.subject=NameSubject::Pet;notice.prompt=true;
        } else if (packet.opcode == 0x63d7 || packet.opcode == 0x4cd9) {
            notice.subject=NameSubject::Mercenary;notice.prompt=true;notice.immediate=packet.opcode==0x63d7;
        } else if (packet.opcode == to_wire(Op::GMNameChange) || packet.opcode == to_wire(Op::ChangePetName) || packet.opcode == 0x36db) {
            notice.subject=packet.opcode==to_wire(Op::GMNameChange)?NameSubject::Character:
                packet.opcode==to_wire(Op::ChangePetName)?NameSubject::Pet:NameSubject::Mercenary;
            if (packet.data.size()<132) return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,"short RoF2 name-change response"));
            const auto* buf=packet.data.data();
            const auto result=static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(buf[128])) |
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(buf[129]))<<8) |
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(buf[130]))<<16) |
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(buf[131]))<<24);
            notice.has_result=true;notice.result=static_cast<std::int32_t>(result);
        } else name_wire=false;
        if (name_wire) {
            results.push_back(GameMessage{GameOpcode::NameChangeNotice,notice});
            return CodecResult<MessageBatch>::success(std::move(results));
        }
    }
    if (scope == ServiceScope::Zone) {
        if (auto notice = decode_mercenary_notice(packet)) {
            results.push_back(GameMessage{GameOpcode::MercenaryNotice,std::move(*notice)});
            return CodecResult<MessageBatch>::success(std::move(results));
        }
    }
    if (scope == ServiceScope::Zone) {
        if (packet.opcode == to_wire(Op::Begging) || packet.opcode == to_wire(Op::PickPocket)) {
            const auto coin = packet.opcode == to_wire(Op::Begging)
                ? net::decode_begging(packet) : net::decode_pick_pocket(packet);
            if (!coin) return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket, "invalid ability coin reply"));
            return CodecResult<MessageBatch>::success(MessageBatch{{
                packet.opcode == to_wire(Op::Begging) ? GameOpcode::Begging : GameOpcode::PickPocket, *coin}});
        }
        if (packet.opcode == to_wire(Op::Bind_Wound)) {
            const auto result = net::decode_bind_wound(packet);
            if (!result) return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket, "invalid bind-wound reply"));
            return CodecResult<MessageBatch>::success(MessageBatch{{GameOpcode::Bind_Wound, BindWoundResult{*result}}});
        }
        if (packet.opcode == to_wire(Op::CancelSneakHide)) {
            return CodecResult<MessageBatch>::success(MessageBatch{{GameOpcode::CancelSneakHide, std::monostate{}}});
        }
    }
    // RoF2's OP_ZoneSpawns is an optional aggregate. EQEmu's translator
    // normally splits it into OP_ZoneEntry records, but other RoF2 servers
    // can send concatenated spawn records. The game sees individual semantic
    // spawns either way, and never interprets their packed offsets.
    if (scope == ServiceScope::Zone && packet.opcode == to_wire(Op::ZoneSpawns)) {
        auto spawns = net::decode_spawns(packet);
        if (!spawns)
            return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket, "RoF2 aggregate spawn payload is malformed"));
        results.reserve(spawns->size());
        for (auto& spawn : *spawns)
            results.push_back(GameMessage{GameOpcode::NewSpawn, std::move(spawn)});
        return CodecResult<MessageBatch>::success(std::move(results));
    }
    if (scope == ServiceScope::Zone) {
        const auto emit = [&](GameOpcode op, Payload value) {
            MessageBatch answer;
            answer.push_back(GameMessage{op, std::move(value)});
            return CodecResult<MessageBatch>::success(std::move(answer));
        };
        const auto* catalog = achievement_catalog_ ? &*achievement_catalog_ : nullptr;
        if (packet.opcode == net::kOpAchievementDefinitions) {
            if (auto decoded = net::decode_achievement_catalog(packet)) {
                previous_achievement_catalog_ = std::move(achievement_catalog_);
                achievement_catalog_ = *decoded;
                return emit(GameOpcode::AchievementDefinitions, std::move(*decoded));
            }
        } else if (packet.opcode == net::kOpAchievementPrimary) {
            if (auto decoded = net::decode_achievement_primary(packet,catalog)) return emit(GameOpcode::AchievementPrimary,std::move(*decoded));
        } else if (packet.opcode == net::kOpAchievementProgress) {
            if (auto decoded = net::decode_achievement_progress(packet)) return emit(GameOpcode::AchievementProgress,std::move(*decoded));
        } else if (packet.opcode == net::kOpAchievementSnapshot) {
            if (auto decoded = net::decode_achievement_snapshot(packet,catalog)) return emit(GameOpcode::AchievementSnapshot,std::move(*decoded));
            if (previous_achievement_catalog_)
                if (auto decoded = net::decode_achievement_snapshot(packet,&*previous_achievement_catalog_))
                    return emit(GameOpcode::AchievementSnapshot,std::move(*decoded));
        } else if (packet.opcode == net::kOpAchievementInspect) {
            if (auto decoded = net::decode_achievement_inspect(packet,catalog)) return emit(GameOpcode::AchievementInspect,std::move(*decoded));
            if (previous_achievement_catalog_)
                if (auto decoded = net::decode_achievement_inspect(packet,&*previous_achievement_catalog_))
                    return emit(GameOpcode::AchievementInspect,std::move(*decoded));
        } else if (packet.opcode == net::kOpAchievementEarned) {
            if (auto decoded = net::decode_achievement_earned(packet,catalog)) return emit(GameOpcode::AchievementEarned,std::move(*decoded));
        } else if (packet.opcode == net::kOpAchievementWindow) {
            if (auto decoded = net::decode_achievement_window(packet)) return emit(GameOpcode::AchievementWindow,*decoded);
        } else if (packet.opcode == to_wire(Op::GuildUpdate)) {
            if (packet.data.size() < sizeof(std::uint32_t))
                return CodecResult<MessageBatch>::failure(error(CodecErrorKind::TruncatedPacket,
                    "short RoF2 guild update discriminator"));
            const auto action = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.data[0])) |
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.data[1])) << 8) |
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.data[2])) << 16) |
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.data[3])) << 24);
            // EQEmu's guild-achievement notification uses action 3. Missing
            // catalog data is not permission to reinterpret it as a different
            // guild command with the same opcode.
            if (action == 3) {
                if (!catalog)
                    return CodecResult<MessageBatch>::failure(error(CodecErrorKind::IncompleteMessage,
                        "guild achievement received before achievement catalog"));
                if (auto decoded = net::decode_achievement_guild(packet,catalog))
                    return emit(GameOpcode::AchievementGuild,std::move(*decoded));
                return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,
                    "invalid guild achievement payload"));
            }
            if (auto decoded = net::decode_guild_update(packet))
                return emit(GameOpcode::GuildUpdate,std::move(*decoded));
            return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,
                "invalid RoF2 guild update payload"));
        }
        if (packet.opcode == net::kOpAchievementDefinitions || packet.opcode == net::kOpAchievementPrimary ||
            packet.opcode == net::kOpAchievementProgress || packet.opcode == net::kOpAchievementSnapshot ||
            packet.opcode == net::kOpAchievementInspect || packet.opcode == net::kOpAchievementEarned ||
            packet.opcode == net::kOpAchievementWindow)
            return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,"invalid RoF2 achievement record or catalog unavailable"));
    }
    // AchievementRewardView is a client request (action 5) on the pending
    // reward wire opcode. The inbound wire value always carries a RewardPacket,
    // never an echoed request. Likewise ItemPreviewRequest and
    // MarketplacePreviewRequest share an outbound opcode but the inbound
    // payload is the item preview response, not either request layout.
    if (scope == ServiceScope::Zone && packet.opcode == to_wire(Op::AchievementReward)) {
        if (auto reward = net::decode_reward_packet(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::AchievementReward, std::move(*reward)}});
        return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,
            "invalid RoF2 pending reward reply"));
    }
    if (scope == ServiceScope::Zone && packet.opcode == to_wire(Op::ItemPreviewRequest)) {
        if (auto preview = net::decode_item_preview(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::ItemPreviewRequest, std::move(*preview)}});
        return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,
            "invalid RoF2 item preview response"));
    }
    // Some named RoF2 records have the same wire value and identical shared
    // fields. EQEmu emits one guild-rank record (guild, rank, name, flags),
    // irrespective of which server-side operation caused it. Deliver one
    // normalized rank change rather than racing two identical decoders.
    // Server-originated MOTD and raid notes are OP_RaidUpdate action 35/36.
    // Client commands with those actions are OP_RaidInvite instead; an
    // incoming RaidInvite must not be misdecoded as an update notification.
    if (scope == ServiceScope::Zone && packet.opcode == to_wire(Op::RaidUpdate)) {
        if (auto update = net::decode_raid_update(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::RaidUpdate, std::move(*update)}});
        return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,
            "invalid RoF2 raid action payload"));
    }
    if (scope == ServiceScope::Zone && packet.opcode == to_wire(Op::SetGuildRank)) {
        if (auto rank = net::decode_guild_roster_change(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::GuildMemberRankAltBanker, std::move(*rank)}});
        return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,
            "invalid RoF2 guild rank/banker record"));
    }
    // EQEmu's member-update and details opcodes collide in the named map.
    // The common name/zone/last-seen fields are represented once as a member
    // update; do not interpret the final, version-dependent dword as an
    // offline flag on the generic path.
    if (scope == ServiceScope::Zone && packet.opcode == to_wire(Op::GuildMemberUpdate)) {
        if (auto member = net::decode_guild_member_update(packet))
            return CodecResult<MessageBatch>::success({{GameOpcode::GuildMemberUpdate, std::move(*member)}});
        return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket,
            "invalid RoF2 guild member update/details record"));
    }
    bool known_wire = false;
    bool candidate_decoder = false;
    for (const auto& m : detail::mappings()) {
        if (m.scope != scope || !packet.opcode || to_wire(m.wire_name) != packet.opcode) continue;
        known_wire = true;
        if (!detail::implemented(m.semantic, CodecDirection::Inbound)) continue;
        candidate_decoder = true;
        if (auto decoded = detail::parse_packet(m.semantic, packet)) results.push_back(std::move(*decoded));
    }
    if (results.size() == 1) return CodecResult<MessageBatch>::success(std::move(results));
    if (results.size() > 1)
        return CodecResult<MessageBatch>::failure(error(CodecErrorKind::MalformedPacket, "RoF2 opcode collision: multiple payload decoders accepted the wire packet"));
    return CodecResult<MessageBatch>::failure(error(
        candidate_decoder ? CodecErrorKind::MalformedPacket : CodecErrorKind::UnsupportedOpcode,
        known_wire ? (candidate_decoder ? "RoF2 payload failed its structured decoder" : "RoF2 wire opcode has no implemented decoder")
                   : "unrecognized application wire opcode (may be server-specific)"));
}

} // namespace hailstone::net::rof2
