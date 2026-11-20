#include "game/placement.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>

#include "game/coords.h"
#include "game/real_estate.h"

namespace hailstone::game {

namespace {

constexpr float kUnit = 2.0f * std::numbers::pi_v<float> / 512.0f;

float as_float(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

float wrap512(float a) {
    a = std::fmod(a, 512.0f);
    return a < 0 ? a + 512.0f : a;
}

} // namespace

glm::mat4 placed_transform(const net::RePlacedItem& o) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), placed_world_position(o));
    m = glm::rotate(m, o.heading * kUnit, glm::vec3(0, 0, 1));
    m = glm::rotate(m, -o.pitch * kUnit, glm::vec3(0, 1, 0));
    m = glm::rotate(m, o.roll * kUnit, glm::vec3(1, 0, 0));
    float s = std::isfinite(o.scale) && o.scale > 0.0f ? o.scale : 1.0f;
    return glm::scale(m, glm::vec3(s));
}

bool placed_menu_in_reach(const glm::vec3& actor, float actor_radius, const glm::vec3& player, float player_height,
                          std::uint32_t estate, std::uint32_t current_estate) {
    return glm::distance(actor, player) <= player_height + 50.0f + actor_radius || estate == current_estate;
}

bool npc_item_menu_in_reach(const glm::vec3& spawn, const glm::vec3& player, std::uint32_t estate,
                            std::uint32_t current_estate) {
    return glm::distance(spawn, player) < 60.0f || estate == current_estate;
}

bool place_mode_allowed(int mode, const net::RePlacedItem& o) {
    switch (mode) {
    case 0:
    case 2: return !(o.wall_only && o.wall_allowed == 1);
    case 1: return o.wall_allowed == 1;
    case 3: return !o.no_free_mode && !o.npc_item;
    default: return false;
    }
}

std::uint32_t place_mode_name(int mode) {
    switch (mode) {
    case 0: return 5266;
    case 1: return 5286;
    case 2: return 5267;
    case 3: return 5268;
    default: return 0;
    }
}

std::uint32_t place_query(const std::vector<net::ReArea>& areas,
                          const std::unordered_map<std::uint32_t, std::uint32_t>& codes, std::uint32_t root,
                          const glm::mat4& transform, const glm::vec3& lo, const glm::vec3& hi) {
    glm::vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c{(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
        // In the space RealEstateManager's occupancy tests the areas in (the server's axes); the original passes
        // the actor's own position and box (0x5ae010), and which axes the area matrices use is open.
        corners[i] = world_to_server(glm::vec3(transform * glm::vec4(c, 1.0f)));
    }
    bool found = false;
    std::uint32_t code = 1;
    for (const auto& a : areas) {
        auto it = codes.find(a.id);
        if (it == codes.end()) continue;
        // 0x8d2b00: every corner in the area.
        bool all = std::all_of(std::begin(corners), std::end(corners),
                               [&](const glm::vec3& c) { return RealEstateManager::point_in_area(a, c); });
        if (!all) continue;
        found = true;
        if (it->second == 0) return 0;
        code = it->second;
    }
    if (found) return code;
    auto r = codes.find(root);
    return r == codes.end() ? 1u : r->second;
}

void Placement::begin(const net::RePlaceReply& reply) {
    *this = Placement();
    active_ = true;
    ghost_ = reply.item;
    index_ = reply.index;
    b1_ = reply.b1, b2_ = reply.b2, b3_ = reply.b3;
    c1_ = reply.c1, c2_ = reply.c2, c3_ = reply.c3;
    // AreaManager map 2 (0x4d5b90 clears it first; 0x8d6c50 inserts).
    for (const auto& [key, value] : reply.codes) codes_[key] = value;
    mode_ = static_cast<int>(PlaceMode::Cursor);
    if (!place_mode_allowed(mode_, ghost_)) set_mode(-1);
}

bool Placement::set_mode(int mode) {
    if (!active_) return false;
    if (mode == -1) {
        // Toggle: the next allowed mode after the current one, 0..5 round (none allowed: no change).
        for (int step = 1; step <= 6; ++step) {
            int m = (mode_ + step) % 6;
            if (place_mode_allowed(m, ghost_)) {
                mode_ = m;
                return true;
            }
        }
        return false;
    }
    if (!place_mode_allowed(mode, ghost_)) return false;
    mode_ = mode;
    return true;
}

void Placement::rotate(float heading, float pitch, float roll) {
    if (!active_) return;
    ghost_.heading = wrap512(ghost_.heading + heading);
    ghost_.pitch = wrap512(ghost_.pitch + pitch);
    ghost_.roll = wrap512(ghost_.roll + roll);
}

void Placement::reset_rotation(bool heading, bool pitch, bool roll) {
    (void)roll;
    if (!active_ || mode_ == static_cast<int>(PlaceMode::Wall)) return;
    if (heading) ghost_.heading = as_float(ghost_.tail[4]);
    if (pitch) ghost_.pitch = as_float(ghost_.tail[5]);
}

void Placement::adjust_scale(float delta) {
    if (!active_ || ghost_.npc_item || delta == 0.0f) return;
    float lo = as_float(ghost_.tail[0]), hi = as_float(ghost_.tail[1]);
    float s = ghost_.scale + delta;
    if (s < lo) s = lo;
    else if (s > hi) s = hi;
    ghost_.scale = s;
}

void Placement::reset_scale() {
    if (!active_ || ghost_.npc_item) return;
    ghost_.scale = as_float(ghost_.tail[2]);
}

void Placement::move_to(const glm::vec3& world) {
    if (active_) ghost_.server_pos = world;
}

void Placement::validate(const std::vector<net::ReArea>& areas, std::uint32_t root, const glm::vec3& lo,
                         const glm::vec3& hi) {
    if (!active_) return;
    code_ = place_query(areas, codes_, root, placed_transform(ghost_), lo, hi);
}

std::optional<net::RePlacementIntent> Placement::confirm(const glm::vec3& box_a, const glm::vec3& box_b) const {
    if (!active_ || code_ != 0) return std::nullopt;
    return net::RePlacementIntent{ghost_,index_,box_a,box_b};
}

} // namespace hailstone::game
