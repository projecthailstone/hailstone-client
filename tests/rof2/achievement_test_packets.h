#pragma once

// Independently authored, tiny wire fixtures. Achievement IDs deliberately differ
// from their catalog indices; bucket 3 has descriptive data but no runtime state.
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include "net/protocol/rof2/packets/achievement_packets.h"
#include "net/protocol/rof2/wire_io.h"

namespace hailstone::test::achievement_fixture {

inline constexpr std::array<std::uint32_t, 2> ids{42, 900};
inline constexpr std::array<std::uint32_t, 2> component0{100, 200}, component1{101, 201};
inline constexpr std::string_view second_name = "Second & <Achievement>";

inline void string(ByteWriter& out, std::string_view value) {
    out.bytes({reinterpret_cast<const std::byte*>(value.data()), value.size()});
    out.le<std::uint8_t>(0);
}

inline net::AppPacket catalog(std::string_view name = second_name, std::uint32_t definition_count = 2,
                             std::uint32_t bucket1_components = 1) {
    ByteWriter body;
    body.le<std::uint32_t>(1); // one root category with every received association
    body.le<std::uint32_t>(7); body.le<std::uint32_t>(0xffffffffu);
    string(body, "Fixture category"); string(body, "Independent achievements"); string(body, "");
    body.le<std::uint32_t>(0); body.le(definition_count);
    const auto definition_id = [](std::uint32_t index) { return index < ids.size() ? ids[index] : 10000 + index; };
    for (std::uint32_t index = 0; index < definition_count; ++index) {
        const auto id = definition_id(index);
        body.le(id); string(body, ""); body.le<std::uint32_t>(0);
    }
    body.le<std::uint32_t>(0); // no children
    body.le(definition_count);
    for (std::uint32_t index = 0; index < definition_count; ++index) {
        body.le(definition_id(index)); string(body, index ? name : "First achievement"); string(body, "Fixture definition");
        body.le<std::uint32_t>(0); body.le<std::uint8_t>(0); body.le<std::uint32_t>(1);
        for (std::uint8_t type = 0; type < 4; ++type) {
            const auto components = type == 2 ? 0 : type == 1 ? bucket1_components : 1;
            body.le<std::uint32_t>(components);
            for (std::uint32_t component = 0; component < components; ++component) {
                body.le<std::uint32_t>(type == 3 ? 303 : 100 + index * 100 + type + component * 1000);
                body.le(type); body.le<std::uint32_t>(type == 0 ? 3 : 5);
                string(body, type == 3 ? "Descriptive criterion" : "Runtime criterion");
                string(body, "Fixture component"); body.le<std::uint8_t>(0);
            }
        }
        body.le<std::uint32_t>(index ? 10 : 5); body.le<std::uint32_t>(index ? 1 : 0);
    }
    const auto bytes = body.take();
    uLongf length = compressBound(static_cast<uLong>(bytes.size()));
    std::vector<std::uint8_t> compressed(static_cast<std::size_t>(length));
    if (compress2(compressed.data(), &length, reinterpret_cast<const Bytef*>(bytes.data()), static_cast<uLong>(bytes.size()), Z_BEST_SPEED) != Z_OK)
        throw std::runtime_error("achievement fixture compression failed");
    compressed.resize(static_cast<std::size_t>(length));
    ByteWriter wire; wire.le(static_cast<std::uint32_t>(bytes.size())); wire.bytes({reinterpret_cast<const std::byte*>(compressed.data()), compressed.size()});
    return net::AppPacket(net::kOpAchievementDefinitions, wire.take());
}

inline void state(ByteWriter& out, net::AchievementStatus status = net::AchievementStatus::Open,
                  bool complete_bits = false, bool counts = false, std::uint32_t count = 2,
                  std::uint32_t bucket1_components = 1) {
    out.le(static_cast<std::int16_t>(status));
    for (std::uint32_t first = 0; first < bucket1_components; first += 16) {
        const auto bits = std::min<std::uint32_t>(16, bucket1_components - first);
        out.le(static_cast<std::uint16_t>(complete_bits ? (1u << bits) - 1u : 0u)); // bucket 1
    }
    out.le<std::uint16_t>(complete_bits ? 1 : 0); // bucket 0; bucket 2 is empty
    if (status == net::AchievementStatus::Completed) out.le<std::uint32_t>(123456);
    if (counts) {
        for (std::uint32_t i = 0; i < bucket1_components; ++i) out.le(count);
        out.le(count);
    }
}

inline net::AppPacket primary(std::uint32_t serial = 1, bool complete_second = false,
                             std::uint32_t definition_count = 2, std::uint32_t bucket1_components = 1) {
    ByteWriter out; out.le(serial); out.le<std::uint8_t>(1); out.le(definition_count);
    for (std::uint32_t i = 0; i < definition_count; ++i)
        state(out, complete_second && i == 1 ? net::AchievementStatus::Completed : net::AchievementStatus::Open,
              complete_second && i == 1, false, 2, bucket1_components);
    return net::AppPacket(net::kOpAchievementPrimary, out.take());
}

inline net::AppPacket progress(std::uint32_t count = 2, std::uint32_t requirement = 70000) {
    ByteWriter out; out.le<std::uint32_t>(1);
    out.le(ids[1]); out.le(component1[1]); out.le(requirement); out.le<std::uint32_t>(1); out.le(count);
    return net::AppPacket(net::kOpAchievementProgress, out.take());
}

inline net::AppPacket comparison(bool complete = true, std::uint32_t definition_count = 2,
                                std::uint32_t bucket1_components = 1) {
    ByteWriter out;
    for (std::uint32_t i = 0; i < definition_count; ++i)
        state(out, complete && i == 1 ? net::AchievementStatus::Completed : net::AchievementStatus::Open,
              complete && i == 1, false, 2, bucket1_components);
    return net::AppPacket(net::kOpAchievementSnapshot, out.take());
}

inline net::AppPacket inspect(std::string_view player = "Peer", std::uint32_t id = ids[1],
                             std::uint32_t count = 4, std::uint32_t bucket1_components = 1) {
    ByteWriter out; string(out, player); out.le(id);
    state(out, net::AchievementStatus::Open, false, true, count, bucket1_components);
    out.le<std::uint8_t>(0);
    return net::AppPacket(net::kOpAchievementInspect, out.take());
}

inline std::string metadata(std::string_view player = "Peer", std::uint32_t id = ids[1], std::uint32_t count = 4,
                            std::uint32_t bucket1_components = 1) {
    auto result = std::string(player) + "^" + std::to_string(id) + "^1^";
    for (std::uint32_t first = 0; first < bucket1_components; first += 16) result += "0^";
    result += "0^"; // bucket 0
    for (std::uint32_t i = 0; i <= bucket1_components; ++i) result += std::to_string(count) + "^";
    return result;
}

inline std::string tag(std::string_view player = "Peer", std::string_view visible = second_name) {
    return "3" + metadata(player) + "'" + std::string(visible);
}

inline net::AppPacket earned(std::uint32_t spawn_id, std::string_view player, std::uint32_t sound = 17) {
    ByteWriter out; out.le(spawn_id); out.le(ids[1]); out.le(sound);
    string(out, std::string(player) + "^900^0^1^1^123456^5^3^");
    return net::AppPacket(net::kOpAchievementEarned, out.take());
}

inline net::AppPacket guild(std::string_view player, std::string_view metadata_player = {}) {
    ByteWriter out; out.le<std::uint32_t>(3); out.zeros(8); out.fixed_string(player, 64); out.zeros(4);
    out.le(ids[1]);
    string(out, std::string(metadata_player.empty() ? player : metadata_player) + "^900^0^1^1^123456^5^3^");
    return net::AppPacket(net::rof2::to_wire(net::rof2::Op::GuildUpdate), out.take());
}

} // namespace hailstone::test::achievement_fixture
