#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace hailstone::net {
struct InspectRequest {
    std::uint32_t target = 0, player = 0;
};
inline constexpr int kInspectSlots = 23;
struct InspectAnswer {
    std::uint32_t requester = 0;
    std::uint32_t player = 0;
    std::array<std::string, kInspectSlots> names;
    std::array<std::uint32_t, kInspectSlots> icons{};
    std::string text;
};
struct ReadBook {
    std::uint32_t window = 0xffffffff;
    std::int16_t slot = -1, sub = -1, aug = -1, slot_unknown = 0;
    std::uint32_t type = 0;
    std::uint32_t target = 0;
    std::uint8_t can_cast = 0, can_scribe = 0;
    std::string text;
};
struct InspectMessageIntent {std::string text;};
struct BookButtonIntent {std::int16_t slot = -1, sub = -1, aug = -1; std::uint32_t target = 0;};
} // namespace hailstone::net
