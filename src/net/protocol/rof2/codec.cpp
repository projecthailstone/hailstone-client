#include "net/protocol/rof2/codec.h"
#include "net/protocol/rof2/codec_dispatch.h"
#include "net/protocol/rof2/codec_mapping.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/packets/item_packets.h"

#include <optional>
#include <span>
#include <cstddef>
#include <cstdint>

namespace hailstone::net::rof2 {
namespace {

using namespace messages;
using protocol::CodecDirection;
using protocol::WirePacketBatch;

} // namespace

bool Codec::supports(ServiceScope scope, GameOpcode opcode, CodecDirection direction) const {
    if (scope == ServiceScope::Zone && direction == CodecDirection::Outbound &&
        (opcode == GameOpcode::Translocate || opcode == GameOpcode::RezzAnswer)) return true;
    if (scope == ServiceScope::Zone && direction == CodecDirection::Inbound &&
        (opcode == GameOpcode::Translocate || opcode == GameOpcode::RezzRequest)) return true;
    if (scope == ServiceScope::Zone && direction == CodecDirection::Outbound &&
        (opcode == GameOpcode::DuelRefusal || opcode == GameOpcode::CorpseDecay ||
         opcode == GameOpcode::VeteranRewardClaim || opcode == GameOpcode::ShieldGroup || opcode == GameOpcode::RevealTarget ||
         opcode == GameOpcode::SpellScribe || opcode == GameOpcode::OfflineMode)) return true;
    if (scope == ServiceScope::Zone &&
        ((opcode == GameOpcode::NameChangeRequest && direction == CodecDirection::Outbound) ||
         (opcode == GameOpcode::NameChangeNotice && direction == CodecDirection::Inbound))) return true;
    if (scope == ServiceScope::Zone && direction == CodecDirection::Outbound &&
        (opcode == GameOpcode::SkillAction || opcode == GameOpcode::TrophyTributeRequest ||
         opcode == GameOpcode::TeleportIndex)) return true;
    if (scope == ServiceScope::Zone &&
        ((opcode == GameOpcode::MercenaryRequest && direction == CodecDirection::Outbound) ||
         (opcode == GameOpcode::MercenaryNotice && direction == CodecDirection::Inbound))) return true;
    if (!capabilities().accepts(scope) || !detail::implemented(opcode, direction)) return false;
    for (const auto& mapping : detail::mappings())
        if (mapping.scope == scope && mapping.semantic == opcode && to_wire(mapping.wire_name)) return true;
    return false;
}

std::optional<GameOpcode> Codec::identify_wire(ServiceScope scope, std::uint16_t wire) const {
    // The aggregate spawn wire opcode expands to NewSpawn semantic records;
    // an invalid aggregate has the same required-initial-spawn significance.
    if (scope == ServiceScope::Zone && wire == to_wire(Op::ZoneSpawns))
        return GameOpcode::NewSpawn;
    std::optional<GameOpcode> found;
    for (const auto& m : detail::mappings()) {
        if (m.scope != scope || !wire || to_wire(m.wire_name) != wire ||
            !detail::implemented(m.semantic, CodecDirection::Inbound)) continue;
        if (found && *found != m.semantic) return std::nullopt;
        found = m.semantic;
    }
    return found;
}

} // namespace hailstone::net::rof2

namespace hailstone::net::rof2 {

std::optional<Item> Codec::inspect_item_wire(std::span<const std::byte> bytes) const {
    if (bytes.size() < 4 || bytes.size() > 262144) return std::nullopt;
    AppPacket packet;
    packet.data.assign(bytes.begin(), bytes.end());
    auto value = decode_item_packet(packet);
    if (!value) return std::nullopt;
    return std::move(value->item);
}

} // namespace hailstone::net::rof2
