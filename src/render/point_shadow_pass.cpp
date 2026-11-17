#include "render/point_shadow_pass.h"

#include "assets/model.h"
#include "core/log.h"
#include "render/actor_renderer.h"
#include "render/object_renderer.h"
#include "render/scene_lighting.h"
#include "render/zone_renderer.h"

namespace hailstone::render {

void sync_point_shadows(PointShadows& shadows, const VkContext& vk) {
    if (shadows.total() != vk.point_shadow_slots() || shadows.map != vk.point_shadow_generation()) {
        shadows.resize(vk.point_shadow_slots());
        shadows.map = vk.point_shadow_generation();
    }
}

std::vector<std::uint32_t> plan_point_shadows(PointShadows& shadows, const VkContext& vk,
                                              std::span<const assets::PointLight> lights, const glm::vec3& camera,
                                              float dt, const ActorRenderer* actors, SceneLighting& lighting,
                                              const ObjectRenderer* objects) {
    sync_point_shadows(shadows, vk);
    std::vector<std::uint32_t> builds = shadows.update(lights, camera, dt);
    for (std::uint32_t i = 0; i < shadows.slot_count(); ++i) {
        const PointShadows::Slot& s = shadows.slots()[i];
        bool live = false;
        if (s.built && (actors || objects)) {
            const auto& l = lights[static_cast<std::size_t>(s.light)];
            live = (actors && actors->any_in_reach(l.position, l.radius)) ||
                   (objects && objects->any_in_reach(l.position, l.radius));
        }
        shadows.set_live(i, live);
    }
    shadows.apply(lighting, lights, camera, vk.point_shadow_size());
    return builds;
}

void draw_point_shadows(VkContext& vk, const FrameInfo& frame, PointShadows& shadows,
                        std::span<const assets::PointLight> lights, std::span<const std::uint32_t> builds,
                        ZoneRenderer& zone, ActorRenderer* actors, ObjectRenderer* objects) {
    // The zone's lights learn which of this frame's shadows is theirs (plan_point_shadows's apply), before the scene.
    zone.set_light_shadows(frame, shadows.entry_lights());
    ZoneRenderer::PointReach reach; // a light's chunks in reach, found once for its six faces
    for (std::uint32_t slot : builds) {
        const auto& l = lights[static_cast<std::size_t>(shadows.slots()[slot].light)];
        log::debug("light shadow {}: light {} at ({:.0f}, {:.0f}, {:.0f}), reach {:.0f}", slot,
                   shadows.slots()[slot].light, l.position.x, l.position.y, l.position.z, l.radius);
        zone.point_reach(l.position, l.radius, reach);
        for (std::uint32_t face = 0; face < kPointShadowFaces; ++face) {
            vk.begin_point_shadow(frame, slot, face, true);
            zone.draw_point_shadow(frame, reach, face);
            vk.end_point_shadow(frame);
        }
        shadows.mark_built(slot);
    }
    // The lights that move (PointShadows::set_dynamic): their whole cube each frame, the zone, the placed objects and
    // the characters but the ones they sit inside.
    for (std::uint32_t k = 0; k < shadows.dynamic_slots().size(); ++k) {
        const PointShadows::DynamicSlot& d = shadows.dynamic_slots()[k];
        if (!d.used) continue;
        const std::uint32_t cube = shadows.dynamic_cube(k);
        zone.point_reach(d.position, d.radius, reach);
        for (std::uint32_t face = 0; face < kPointShadowFaces; ++face) {
            vk.begin_point_shadow(frame, cube, face, true);
            zone.draw_point_shadow(frame, reach, face);
            if (objects) objects->draw_point_shadow(frame, d.position, d.radius, face);
            if (actors) actors->draw_point_shadow(frame, d.position, d.radius, face, d.skip_spawn, d.skip_around);
            vk.end_point_shadow(frame);
        }
    }
    if (!actors && !objects) return;
    for (std::uint32_t slot = 0; slot < shadows.slot_count(); ++slot) {
        const PointShadows::Slot& s = shadows.slots()[slot];
        if (!s.live) continue;
        const auto& l = lights[static_cast<std::size_t>(s.light)];
        const std::uint32_t live = shadows.cube_of(slot);
        vk.copy_point_shadow(frame, slot, live);
        for (std::uint32_t face = 0; face < kPointShadowFaces; ++face) {
            vk.begin_point_shadow(frame, live, face, false);
            if (actors) actors->draw_point_shadow(frame, l.position, l.radius, face);
            if (objects) objects->draw_point_shadow(frame, l.position, l.radius, face);
            vk.end_point_shadow(frame);
        }
    }
}

} // namespace hailstone::render
