#include "net/protocol/rof2/codec.h"
#include "net/protocol/rof2/codec_dispatch.h"
#include "net/protocol/rof2/codec_mapping.h"
#include "net/messages/zone_offer.h"
#include "net/messages/mercenary.h"
#include "net/messages/name_change.h"
#include "net/messages/player_commands.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/packets/merc_packets.h"
#include "net/protocol/rof2/packets/ability_packets.h"
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/packets/player_packets.h"
#include "net/protocol/rof2/packets/task_packets.h"

#include <optional>
#include <string>
#include <utility>

namespace hailstone::net::rof2 {
using namespace messages;
using protocol::CodecDirection;
using protocol::WirePacketBatch;
namespace {
CodecError error(CodecErrorKind kind, std::string detail) { return {kind, std::move(detail)}; }
}

CodecResult<WirePacketBatch> Codec::encode(ServiceScope scope, const GameMessage& message) const {
    if (!capabilities().accepts(scope) ||
        !supports(scope, message.opcode, CodecDirection::Outbound))
        return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::UnsupportedOpcode,
            "RoF2 outbound command is not implemented in this service scope"));
    // Validate opcode-selected payload alternatives before a serializer or
    // stateful one-use reply token can mutate protocol state.
    if (auto invalid = detail::validate_command_variant(message))
        return CodecResult<WirePacketBatch>::failure(std::move(*invalid));
    if (scope == ServiceScope::Zone &&
        (message.opcode == GameOpcode::Translocate || message.opcode == GameOpcode::RezzAnswer)) {
        const auto* response = messages::message_get<ZoneOfferResponse>(message);
        if (!response) return CodecResult<WirePacketBatch>::failure(
            error(CodecErrorKind::UnsupportedPayload, "zone offer reply requires a typed response"));
        auto found = zone_offers_.find(response->token);
        if (found == zone_offers_.end()) return CodecResult<WirePacketBatch>::failure(
            error(CodecErrorKind::InvalidValue, "zone offer token is no longer valid"));
        const bool translocate = message.opcode == GameOpcode::Translocate;
        if (found->second.opcode != to_wire(translocate ? Op::Translocate : Op::RezzRequest))
            return CodecResult<WirePacketBatch>::failure(
                error(CodecErrorKind::InvalidValue, "zone offer response does not match original offer kind"));
        auto packet = translocate ? net::answer_translocate(found->second, response->accepted)
                                  : net::answer_rezz(found->second, response->accepted);
        // Token removal happens at admission, not during a speculative encode.
        return CodecResult<WirePacketBatch>::success({std::move(packet)});
    }
    if (scope == ServiceScope::Zone && message.opcode == GameOpcode::NameChangeRequest) {
        const auto* req = messages::message_get<NameChangeRequest>(message);
        if (!req) return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::UnsupportedPayload,"name-change request requires names"));
        hailstone::ByteWriter writer;
        writer.fixed_string(std::string_view(req->requested_name).substr(0,63),64);
        writer.fixed_string(std::string_view(req->character_name).substr(0,63),64);
        writer.le(std::uint32_t{0});
        const std::uint16_t opcode=req->subject == NameSubject::Character ? to_wire(Op::GMNameChange) :
            req->subject == NameSubject::Pet ? to_wire(Op::ChangePetName) : 0x36db;
        WirePacketBatch batch;batch.emplace_back(opcode,writer.take());
        return CodecResult<WirePacketBatch>::success(std::move(batch));
    }
    if (scope == ServiceScope::Zone && message.opcode == GameOpcode::MercenaryRequest) {
        const auto* req = messages::message_get<MercenaryRequest>(message);
        if (!req) return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::UnsupportedPayload, "mercenary request requires typed command"));
        auto packet = encode_mercenary_request(*req);
        if (!packet) return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::InvalidValue, "unsupported mercenary command"));
        WirePacketBatch batch;batch.push_back(std::move(*packet));
        return CodecResult<WirePacketBatch>::success(std::move(batch));
    }
    if (scope == ServiceScope::Zone && message.opcode == GameOpcode::SkillAction) {
        const auto* action = messages::message_get<game::mechanics::SkillSend>(message);
        if (!action) return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::UnsupportedPayload, "skill action requires typed values"));
        auto packet = encode_skill_send(*action);
        if (!packet) return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::InvalidValue, "unknown skill action"));
        return CodecResult<WirePacketBatch>::success({std::move(*packet)});
    }
    if (scope == ServiceScope::Zone) {
        std::optional<AppPacket> custom;
        switch (message.opcode) {
        case GameOpcode::DuelRefusal:
            if (const auto* x = messages::message_get<DuelRefusal>(message)) custom = net::encode(*x);
            break;
        case GameOpcode::CorpseDecay:
            if (const auto* x = messages::message_get<CorpseDecayIntent>(message)) custom = net::encode_decay_corpse(x->corpse_id,x->player_id);
            break;
        case GameOpcode::VeteranRewardClaim:
            if (messages::message_is<std::monostate>(message)) custom = net::encode_veteran_reward_claim();
            break;
        case GameOpcode::ShieldGroup:
            if (const auto* x = messages::message_get<RaidShieldIntent>(message)) custom = net::encode_shield_group(x->members);
            break;
        case GameOpcode::RevealTarget:
            if (messages::message_is<std::monostate>(message)) custom = net::encode_reveal();
            break;
        case GameOpcode::SpellScribe:
            if (const auto* x = messages::message_get<SpellScribeIntent>(message)) custom = net::encode_spell_scribe(x->actor,x->target);
            break;
        case GameOpcode::OfflineMode:
            if (messages::message_is<OfflineModeIntent>(message)) custom = AppPacket(net::kOpOfflineMode);
            break;
        default: break;
        }
        if (supports(scope, message.opcode, CodecDirection::Outbound) &&
            (message.opcode == GameOpcode::DuelRefusal || message.opcode == GameOpcode::CorpseDecay ||
             message.opcode == GameOpcode::VeteranRewardClaim || message.opcode == GameOpcode::ShieldGroup || message.opcode == GameOpcode::RevealTarget ||
             message.opcode == GameOpcode::SpellScribe || message.opcode == GameOpcode::OfflineMode)) {
            if (!custom) return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::UnsupportedPayload, "invalid original RoF2 command value"));
            return CodecResult<WirePacketBatch>::success({std::move(*custom)});
        }
    }
    if (scope == ServiceScope::Zone && message.opcode == GameOpcode::TeleportIndex) {
        const auto* index = messages::message_get<std::int32_t>(message);
        if (!index) return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::UnsupportedPayload,"teleport index requires a signed zone-point index"));
        hailstone::ByteWriter w; w.le(static_cast<std::uint32_t>(*index));
        return CodecResult<WirePacketBatch>::success({AppPacket(0xcadf, w.take())});
    }
    if (scope == ServiceScope::Zone && message.opcode == GameOpcode::TrophyTributeRequest) {
        if (!messages::message_is<std::monostate>(message))
            return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::UnsupportedPayload, "trophy list request has no payload"));
        // Client RoF2 trophy window request; absent from EQEmu's named RoF2 opcode list.
        return CodecResult<WirePacketBatch>::success({AppPacket(0x51a9)});
    }
    auto packet = detail::make_packet(message.opcode, message);
    if (!packet)
        return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::UnsupportedPayload, "RoF2 outbound opcode expects a different typed value"));
    // The existing serializer is authoritative about the application opcode.
    // Confirm the selected service's mapping instead of overriding its output.
    bool mapped = false;
    for (const auto& m : detail::mappings()) {
        if (m.scope == scope && m.semantic == message.opcode && to_wire(m.wire_name) == packet->opcode)
            mapped = true;
    }
    if (!mapped)
        return CodecResult<WirePacketBatch>::failure(error(CodecErrorKind::InvalidValue, "serializer returned a wire opcode not defined for this RoF2 message"));
    WirePacketBatch batch;
    batch.emplace_back(std::move(*packet));
    return CodecResult<WirePacketBatch>::success(std::move(batch));
}

void Codec::on_send_admitted(ServiceScope scope, const GameMessage& message) {
    if (scope != ServiceScope::Zone ||
        (message.opcode != GameOpcode::Translocate && message.opcode != GameOpcode::RezzAnswer)) return;
    if (const auto* response = messages::message_get<ZoneOfferResponse>(message))
        zone_offers_.erase(response->token);
}


} // namespace hailstone::net::rof2
