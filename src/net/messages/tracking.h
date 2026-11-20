#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "game/tracking.h"
namespace hailstone::net {
struct FindPersonRequest {
    std::uint32_t kind = 1;
    std::uint16_t id = 0;
    std::int32_t item_a = -1, item_b = -1;
    glm::vec3 at{0.0f};    // player location
    glm::vec3 point{0.0f}; // desired destination
};
struct TrackReply { bool timer=false; std::vector<game::TrackEntry> entries; };
} // namespace hailstone::net
