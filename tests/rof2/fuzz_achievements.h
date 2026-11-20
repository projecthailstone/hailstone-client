#pragma once

// A small independently authored catalog lets fuzz inputs reach the variable state/link
// layouts instead of stopping at the public decoders' missing-catalog guard.
#include <span>
#include <stdexcept>
#include <string_view>

#include "achievement_test_packets.h"

namespace hailstone::fuzz_achievements {
inline const net::AchievementCatalog& catalog() {
    static const auto value = [] {
        auto decoded = net::decode_achievement_catalog(test::achievement_fixture::catalog());
        if (!decoded) throw std::runtime_error("invalid achievement fuzz catalog");
        return std::move(*decoded);
    }();
    return value;
}
inline void feed(std::span<const std::byte> input) {
    if (input.empty()) return;
    const auto choice = std::to_integer<unsigned>(input[0]) % 7;
    const auto body = input.subspan(1);
    net::AppPacket packet(0, std::vector<std::byte>(body.begin(), body.end()));
    switch (choice) {
    case 0: packet.opcode = net::kOpAchievementPrimary; (void)net::decode_achievement_primary(packet, &catalog()); break;
    case 1: packet.opcode = net::kOpAchievementSnapshot; (void)net::decode_achievement_snapshot(packet, &catalog()); break;
    case 2: packet.opcode = net::kOpAchievementInspect; (void)net::decode_achievement_inspect(packet, &catalog()); break;
    case 3: packet.opcode = net::kOpAchievementEarned; (void)net::decode_achievement_earned(packet, &catalog()); break;
    case 4: packet.opcode = net::rof2::to_wire(net::rof2::Op::GuildUpdate); (void)net::decode_achievement_guild(packet, &catalog()); break;
    case 5:
        (void)net::parse_achievement_link(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()), catalog());
        break;
    case 6: packet.opcode = net::kOpAchievementDefinitions; (void)net::decode_achievement_catalog(packet); break;
    }
}
} // namespace hailstone::fuzz_achievements
