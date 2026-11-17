#pragma once

// Shadows from the zone's own lights (lights.wld) for the modern lighting (docs/graphics.md, "Light shadows"): the
// lights that matter most near the camera each get a cube of six depth faces in the point shadow map
// (VkContext::begin_point_shadow), holding each caster's distance from the light over the light's radius.
//
// The zone's lights and geometry stay put, so each light's cube of the zone is drawn once, when the light gets its
// slot (the cache, one a frame), and kept. When characters stand in a light's reach its cache is copied into the
// slot's live cube each frame and they are drawn over it.
//
// The lights that move (the one the player carries, the spells' brightest) have slots of their own, kept out of the
// zone lights' share (dynamic_shadow_slots). They have no cache: each frame their live cube is drawn whole, the zone
// round them and the characters, but the bodies they sit inside (set_dynamic). A light is matched with its shadow, as
// every shadowed light, by position and radius, so set_dynamic takes them exactly as the scene's light list has them.
// The match is made on the CPU (apply), not per pixel: each light carries its shadow's index in
// GpuPointLight::color.w, the scene's lights from apply, the zone's from ZoneRenderer::set_light_shadows with
// entry_lights.
//
// A face is this client's mapping, the same in the shaders that draw and read it (shaders/lighting.glsl
// point_shadow_face): faces 0..5 look along +x, -x, +y, -y, +z, -z; the face's two other axes, in order, divided by
// the distance along its own, scaled by kPointShadowFaceScale, are its x and y.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "game/light_list.h"

namespace hailstone::assets { struct PointLight; }

namespace hailstone::render {

struct SceneLighting;

inline constexpr std::uint32_t kMaxPointShadows = 8;
inline constexpr std::uint32_t kPointShadowFaces = 6;
// Each face covers a little more than its 90 degrees, so the filter's taps near an edge stay on the face.
inline constexpr float kPointShadowFaceScale = 0.97f;
// Casters nearer the light than this are left out: the lamp's own glass, the torch's head.
inline constexpr float kPointShadowNear = 3.0f;
// How far from the camera (to the light's sphere) a light can have a shadow; it fades out over the last quarter.
inline constexpr float kPointShadowRange = 360.0f;
// New cubes of the zone drawn a frame at most: each is six passes over the zone round the light, the frame's longest
// part when one came into range (two a frame before; a cube now waits a frame longer behind the one before it, under
// the quarter-second fade-in).
inline constexpr std::uint32_t kPointShadowBuildsPerFrame = 1;

// The face a direction from the light falls on, and the point on the face (-1..1 each way).
struct PointShadowTexel {
    std::uint32_t face = 0;
    glm::vec2 ndc{0.0f};
};
PointShadowTexel point_shadow_face(const glm::vec3& d);

// Whether a box can show on face `face` of a light at `light`: some of it lies on the face's side of the light.
bool box_reaches_face(const glm::vec3& light, std::uint32_t face, const glm::vec3& box_min, const glm::vec3& box_max);

// How many of `slots` (display.light_shadows) go to the lights that move: one for the light the player carries from
// 3 slots up, and one for the spells' brightest light from 5, two from 7 (so the default 8 keeps 5 for the zone).
// Below that the zone's lights, which are everywhere, have them all. A slot for a light that moves costs a whole cube
// drawn every frame, where a zone light's is drawn once; so few, and kept even when no such light shines, so the zone
// lights' caches do not churn as a spell comes and goes.
struct DynamicShadowSlots {
    std::uint32_t carried = 0;
    std::uint32_t spells = 0;
};
DynamicShadowSlots dynamic_shadow_slots(std::uint32_t slots);

// The lights that move this frame, exactly as the scene's light list was given them (the shaders match by position).
struct DynamicShadowLights {
    std::optional<game::LightSource> carried; // the player's torch or lightstone
    std::uint32_t carrier = 0;                // its spawn, left out of its cube (its body surrounds the light); 0 none
    std::vector<game::LightSource> spells;    // the glowing spell effects' (game::particle_lights)
};

// The slots of the point shadow map from frame to frame.
class PointShadows {
public:
    struct Slot {
        int light = -1;         // the zone light's number; -1 empty
        bool built = false;     // its cache cube holds the zone
        bool live = false;      // this frame, characters were drawn into its live cube
        float strength = 0.0f;  // fades in once built, out with the distance
    };
    // A slot for a light that moves: drawn whole into its live cube each frame it is used.
    struct DynamicSlot {
        bool used = false;
        glm::vec3 position{0.0f};
        float radius = 0.0f;
        std::uint32_t skip_spawn = 0; // a character left out of the cube (the carrier); 0 none
        bool skip_around = false;     // leave out the characters the light lies within (a spell's caster or target)
        float strength = 0.0f;        // out with the distance
    };

    // `slots` (at most kMaxPointShadows): how many lights may have a shadow, the zone's and the moving ones
    // (dynamic_shadow_slots); 0 none.
    explicit PointShadows(std::uint32_t slots = 0) { resize(slots); }
    void resize(std::uint32_t slots);
    // A new zone: every slot empty.
    void reset();

    // This frame's moving lights into their slots: the carried light into its own, the brightest spells round
    // `camera` (as update scores the zone's) into theirs; each within kPointShadowRange and large enough to shadow.
    // Before apply (plan_point_shadows).
    void set_dynamic(const DynamicShadowLights& lights, const glm::vec3& camera);

    // Picks the lights near `camera` (kPointShadowRange) that light the most around it: brighter, larger and nearer
    // first; a light keeps its slot while it is still among the first. Returns the slots whose cache cube is to be
    // drawn this frame (unbuilt slots, nearest first, at most kPointShadowBuildsPerFrame); mark_built them after.
    std::vector<std::uint32_t> update(std::span<const assets::PointLight> lights, const glm::vec3& camera, float dt);
    void mark_built(std::uint32_t slot) { slots_[slot].built = true; }
    void set_live(std::uint32_t slot, bool live) { slots_[slot].live = live; }

    std::span<const Slot> slots() const { return slots_; }
    std::span<const DynamicSlot> dynamic_slots() const { return dynamic_; }
    // The point shadow map the cubes were drawn into (VkContext::point_shadow_generation); a new one starts empty.
    std::uint32_t map = 0;
    // The zone lights' slots; and all of them, the size of the map (VkContext::point_shadow_slots).
    std::uint32_t slot_count() const { return static_cast<std::uint32_t>(slots_.size()); }
    std::uint32_t total() const { return total_; }

    // The cube a slot's lookups read: its cache (slot), or its live cube (total() + slot).
    std::uint32_t cube_of(std::uint32_t slot) const { return slots_[slot].live ? total_ + slot : slot; }
    // A moving light's cube: the live one of the slot after the zone's (their caches go unused).
    std::uint32_t dynamic_cube(std::uint32_t k) const { return total_ + slot_count() + k; }

    // Fills SceneLighting's point shadow fields (`size` texels per face side) from the built slots and the used
    // moving ones, fading each out over the last quarter of kPointShadowRange from `camera`, and marks each of the
    // scene's lights (SceneLighting::lights, make_scene_lighting's) with its field (GpuPointLight::color.w: the
    // field's index + 1 where the field's light has its position and radius, else 0). Records entry_lights.
    void apply(SceneLighting& lighting, std::span<const assets::PointLight> lights, const glm::vec3& camera,
               std::uint32_t size);
    // The zone light (its number in apply's `lights`) each of the last apply's point shadow fields is, in order;
    // -1 for a moving light's.
    std::span<const int> entry_lights() const { return {entry_light_.data(), entry_count_}; }

private:
    std::vector<Slot> slots_;
    std::vector<DynamicSlot> dynamic_; // DynamicShadowSlots::carried first, then the spells'
    std::uint32_t carried_slots_ = 0;
    std::uint32_t total_ = 0;
    std::array<int, kMaxPointShadows> entry_light_{};
    std::size_t entry_count_ = 0;
};

} // namespace hailstone::render
