#include "render/point_shadows.h"

#include <algorithm>
#include <cmath>

#include "assets/model.h"
#include "render/scene_lighting.h"

namespace hailstone::render {

namespace {

// How much a light lights round the camera: its brightness and reach over its distance (to its sphere).
template <class Light> float light_score(const Light& l, const glm::vec3& camera) {
    float luma = glm::dot(l.color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    float gap = std::max(glm::length(l.position - camera) - l.radius, 0.0f);
    return luma * l.radius / (gap + 40.0f);
}

// 1 near, 0 at kPointShadowRange (to the light's sphere).
float range_fade(const glm::vec3& position, float radius, const glm::vec3& camera) {
    float gap = std::max(glm::length(position - camera) - radius, 0.0f);
    float x = std::clamp((gap - 0.75f * kPointShadowRange) / (0.25f * kPointShadowRange), 0.0f, 1.0f);
    return 1.0f - x * x * (3.0f - 2.0f * x);
}

// Whether a light can have a shadow from `camera`: large enough that its cube holds more than the near cut, and in
// range.
bool shadowable(const glm::vec3& position, float radius, const glm::vec3& camera) {
    return radius > kPointShadowNear * 2.0f && glm::length(position - camera) - radius <= kPointShadowRange;
}

} // namespace

PointShadowTexel point_shadow_face(const glm::vec3& d) {
    glm::vec3 a = glm::abs(d);
    PointShadowTexel out;
    if (a.x >= a.y && a.x >= a.z) {
        out.face = d.x >= 0.0f ? 0u : 1u;
        out.ndc = glm::vec2(d.y, d.z) / std::max(a.x, 1e-6f);
    } else if (a.y >= a.z) {
        out.face = d.y >= 0.0f ? 2u : 3u;
        out.ndc = glm::vec2(d.x, d.z) / std::max(a.y, 1e-6f);
    } else {
        out.face = d.z >= 0.0f ? 4u : 5u;
        out.ndc = glm::vec2(d.x, d.y) / std::max(a.z, 1e-6f);
    }
    out.ndc *= kPointShadowFaceScale;
    return out;
}

bool box_reaches_face(const glm::vec3& light, std::uint32_t face, const glm::vec3& box_min, const glm::vec3& box_max) {
    int axis = static_cast<int>(face / 2);
    return face % 2 == 0 ? box_max[axis] > light[axis] : box_min[axis] < light[axis];
}

DynamicShadowSlots dynamic_shadow_slots(std::uint32_t slots) {
    DynamicShadowSlots out;
    out.carried = slots >= 3 ? 1u : 0u;
    out.spells = slots >= 7 ? 2u : slots >= 5 ? 1u : 0u;
    return out;
}

void PointShadows::resize(std::uint32_t slots) {
    total_ = std::min(slots, kMaxPointShadows);
    DynamicShadowSlots dynamic = dynamic_shadow_slots(total_);
    carried_slots_ = dynamic.carried;
    slots_.assign(total_ - dynamic.carried - dynamic.spells, Slot{});
    dynamic_.assign(dynamic.carried + dynamic.spells, DynamicSlot{});
}

void PointShadows::reset() {
    for (Slot& s : slots_) s = Slot{};
    for (DynamicSlot& s : dynamic_) s = DynamicSlot{};
}

void PointShadows::set_dynamic(const DynamicShadowLights& lights, const glm::vec3& camera) {
    for (DynamicSlot& s : dynamic_) s = DynamicSlot{};
    auto take = [&](DynamicSlot& s, const game::LightSource& l) {
        s.used = true;
        s.position = l.position;
        s.radius = l.radius;
        s.strength = range_fade(l.position, l.radius, camera);
    };
    // The carried light: its carrier's body is all round it, and would black out the world.
    if (carried_slots_ > 0 && lights.carried && shadowable(lights.carried->position, lights.carried->radius, camera)) {
        take(dynamic_[0], *lights.carried);
        dynamic_[0].skip_spawn = lights.carrier;
    }
    // The spells' brightest. They have no identity from frame to frame (an effect's emitters merge and part), and need
    // none: each cube is drawn anew. Who cast them is not known here, so the characters the light lies within (the
    // caster's hands, the target's body) are left out instead.
    const std::size_t spell_slots = dynamic_.size() - carried_slots_;
    if (spell_slots == 0) return;
    std::vector<const game::LightSource*> spells;
    for (const auto& l : lights.spells)
        if (shadowable(l.position, l.radius, camera)) spells.push_back(&l);
    std::size_t keep = std::min(spells.size(), spell_slots);
    std::partial_sort(spells.begin(), spells.begin() + static_cast<std::ptrdiff_t>(keep), spells.end(),
                      [&](const game::LightSource* a, const game::LightSource* b) {
                          return light_score(*a, camera) > light_score(*b, camera);
                      });
    for (std::size_t i = 0; i < keep; ++i) {
        DynamicSlot& s = dynamic_[carried_slots_ + i];
        take(s, *spells[i]);
        s.skip_around = true;
    }
}

std::vector<std::uint32_t> PointShadows::update(std::span<const assets::PointLight> lights, const glm::vec3& camera,
                                                float dt) {
    std::vector<std::uint32_t> builds;
    if (slots_.empty()) return builds;
    // The candidates, best first; a light that has a slot counts half again, so two close ones do not trade places
    // every frame.
    struct Candidate { int light; float score; };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const auto& l = lights[i];
        if (!(l.radius > kPointShadowNear * 2.0f)) continue;
        if (glm::length(l.position - camera) - l.radius > kPointShadowRange) continue;
        float score = light_score(l, camera);
        if (std::any_of(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.light == static_cast<int>(i); }))
            score *= 1.5f;
        candidates.push_back({static_cast<int>(i), score});
    }
    std::size_t keep = std::min(candidates.size(), slots_.size());
    std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(keep), candidates.end(),
                      [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    candidates.resize(keep);
    // Slots whose light is no longer chosen are freed; the chosen lights without a slot take the free ones.
    for (Slot& s : slots_)
        if (s.light >= 0 && std::none_of(candidates.begin(), candidates.end(),
                                         [&](const Candidate& c) { return c.light == s.light; }))
            s = Slot{};
    for (const Candidate& c : candidates) {
        if (std::any_of(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.light == c.light; })) continue;
        for (Slot& s : slots_)
            if (s.light < 0) {
                s = Slot{};
                s.light = c.light;
                break;
            }
    }
    // The cubes to draw: the unbuilt slots, nearest light first.
    for (std::uint32_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].light >= 0 && !slots_[i].built) builds.push_back(i);
    std::sort(builds.begin(), builds.end(), [&](std::uint32_t a, std::uint32_t b) {
        return glm::length(lights[static_cast<std::size_t>(slots_[a].light)].position - camera) <
               glm::length(lights[static_cast<std::size_t>(slots_[b].light)].position - camera);
    });
    if (builds.size() > kPointShadowBuildsPerFrame) builds.resize(kPointShadowBuildsPerFrame);
    // A built shadow fades in over a quarter second, so a cube arriving does not pop.
    for (Slot& s : slots_)
        s.strength = s.built ? std::min(s.strength + dt * 4.0f, 1.0f) : 0.0f;
    return builds;
}

void PointShadows::apply(SceneLighting& lighting, std::span<const assets::PointLight> lights, const glm::vec3& camera,
                         std::uint32_t size) {
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < slots_.size(); ++i) {
        const Slot& s = slots_[i];
        if (s.light < 0 || !s.built || static_cast<std::size_t>(s.light) >= lights.size()) continue;
        const auto& l = lights[static_cast<std::size_t>(s.light)];
        float strength = s.strength * range_fade(l.position, l.radius, camera);
        if (strength <= 0.0f) continue;
        lighting.point_shadow_light[n] = glm::vec4(l.position, l.radius);
        lighting.point_shadow_slot[n] = glm::vec4(static_cast<float>(cube_of(i) * kPointShadowFaces), strength, 0.0f, 0.0f);
        entry_light_[n] = s.light;
        ++n;
    }
    for (std::uint32_t k = 0; k < dynamic_.size(); ++k) {
        const DynamicSlot& s = dynamic_[k];
        if (!s.used || s.strength <= 0.0f) continue;
        lighting.point_shadow_light[n] = glm::vec4(s.position, s.radius);
        lighting.point_shadow_slot[n] = glm::vec4(static_cast<float>(dynamic_cube(k) * kPointShadowFaces), s.strength,
                                                  0.0f, 0.0f);
        entry_light_[n] = -1;
        ++n;
    }
    entry_count_ = n;
    lighting.point_shadow_params = glm::vec4(static_cast<float>(n), size ? 1.0f / static_cast<float>(size) : 0.0f,
                                             kPointShadowFaceScale, kPointShadowNear);
    // Each of the scene's lights with its field, the first that has its position and radius (the shaders' match
    // before it moved here: within 0.1 unit, its radius within 0.01), so a pixel reads its shadow straight away.
    const int count = std::clamp(static_cast<int>(lighting.params.y + 0.5f), 0, static_cast<int>(kMaxSceneLights));
    for (int i = 0; i < count; ++i) {
        GpuPointLight& l = lighting.lights[static_cast<std::size_t>(i)];
        l.color.w = 0.0f;
        for (std::uint32_t k = 0; k < n; ++k) {
            const glm::vec3 d = glm::vec3(lighting.point_shadow_light[k]) - glm::vec3(l.position_radius);
            if (glm::dot(d, d) <= 0.01f && std::abs(lighting.point_shadow_light[k].w - l.position_radius.w) <= 0.01f) {
                l.color.w = static_cast<float>(k + 1);
                break;
            }
        }
    }
}

} // namespace hailstone::render
