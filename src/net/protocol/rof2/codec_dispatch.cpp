#include "net/protocol/rof2/codec_dispatch.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

// Each selected serializer sets its *own* opcode; the map verifies that it
// belongs to the requested semantic opcode. Never cast the semantic enum to
// a wire value, and never forward unsupported payloads as raw bytes.
std::optional<AppPacket> make_packet(GameOpcode opcode, const GameMessage& m) {
    if (auto encoded = encode_control(opcode, m)) return encoded;
    if (auto encoded = encode_tasks(opcode, m)) return encoded;
    if (auto encoded = encode_social(opcode, m)) return encoded;
    if (auto encoded = encode_items(opcode, m)) return encoded;
    if (auto encoded = encode_commerce(opcode, m)) return encoded;
    if (auto encoded = encode_marketplace(opcode, m)) return encoded;
    if (auto encoded = encode_world(opcode, m)) return encoded;
    if (auto encoded = encode_progression(opcode, m)) return encoded;
    if (auto encoded = encode_zone(opcode, m)) return encoded;
    if (auto encoded = encode_combat_spell(opcode, m)) return encoded;
    return std::nullopt;
}

std::optional<GameMessage> parse_packet(GameOpcode op, const AppPacket& p) {
    if (auto decoded = decode_service(op, p)) return decoded;
    if (auto decoded = decode_zone_state(op, p)) return decoded;
    if (auto decoded = decode_spell_combat(op, p)) return decoded;
    if (auto decoded = decode_items_trade(op, p)) return decoded;
    if (auto decoded = decode_social(op, p)) return decoded;
    if (auto decoded = decode_commerce(op, p)) return decoded;
    if (auto decoded = decode_marketplace(op, p)) return decoded;
    if (auto decoded = decode_world_zone(op, p)) return decoded;
    return std::nullopt;
}


} // namespace hailstone::net::rof2::detail
