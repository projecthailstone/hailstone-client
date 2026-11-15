// render/culling.h: the frustum planes and the box tests the zone renderer culls its chunks with.
#include <doctest/doctest.h>

#include <random>

#include "render/actor_renderer.h"
#include "render/camera.h"
#include "render/cover_map.h"
#include "render/culling.h"
#include "render/shadows.h"

using namespace hailstone::render;

namespace {

Box box_around(glm::vec3 c, float half) { return {c - glm::vec3(half), c + glm::vec3(half)}; }

bool clip_inside(const glm::mat4& m, glm::vec3 p) {
    glm::vec4 c = m * glm::vec4(p, 1.0f);
    return c.w > 0.0f && std::abs(c.x) <= c.w && std::abs(c.y) <= c.w && c.z >= 0.0f && c.z <= c.w;
}

// Conservative: a box any of whose sample points the volume holds must pass.
void check_never_drops(const glm::mat4& m, glm::vec3 lo, glm::vec3 hi, float max_half) {
    Frustum f = frustum_from(m);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    int inside = 0, culled = 0;
    for (int i = 0; i < 4000; ++i) {
        glm::vec3 c = lo + (hi - lo) * glm::vec3(u(rng), u(rng), u(rng));
        glm::vec3 half = glm::vec3(u(rng), u(rng), u(rng)) * max_half;
        Box b{c - half, c + half};
        bool any = false;
        for (int k = 0; k < 27 && !any; ++k) {
            glm::vec3 t(static_cast<float>(k % 3), static_cast<float>(k / 3 % 3), static_cast<float>(k / 9));
            any = clip_inside(m, b.min + (b.max - b.min) * t * 0.5f);
        }
        bool pass = box_in_frustum(f, b);
        if (any) {
            ++inside;
            REQUIRE(pass);
        }
        culled += !pass;
    }
    CHECK(inside > 100);
    CHECK(culled > 100); // the test does cull
}

} // namespace

TEST_CASE("culling: the camera frustum keeps what is ahead and drops what is behind, aside or past the far plane") {
    FlyCamera cam;
    cam.position = {100.0f, 200.0f, 30.0f}; // yaw 0: looking along +X
    glm::mat4 vp = cam.projection(1280, 720, 2000.0f) * cam.view();
    Frustum f = frustum_from(vp);
    CHECK(box_in_frustum(f, box_around({300, 200, 30}, 5)));       // straight ahead
    CHECK(box_in_frustum(f, box_around(cam.position, 1)));          // round the camera
    CHECK_FALSE(box_in_frustum(f, box_around({-100, 200, 30}, 5))); // behind
    CHECK_FALSE(box_in_frustum(f, box_around({2300, 200, 30}, 5))); // past the far plane
    CHECK_FALSE(box_in_frustum(f, box_around({300, 900, 30}, 5)));  // far off to the left
    CHECK_FALSE(box_in_frustum(f, box_around({300, 200, 800}, 5))); // far above
    // A box across the edge of the view stays, though its centre is outside.
    CHECK(box_in_frustum(f, Box{{300, 100, 25}, {310, 900, 35}}));
    // A long, thin box crossing the view behind and ahead of the camera.
    CHECK(box_in_frustum(f, Box{{-500, 199, 29}, {700, 201, 31}}));
}

TEST_CASE("culling: the camera frustum never drops a box that shows") {
    FlyCamera cam;
    cam.position = {-50.0f, 75.0f, 10.0f};
    cam.yaw = 0.7f;
    cam.pitch = -0.3f;
    glm::mat4 vp = cam.projection(1600, 900, 1500.0f) * cam.view();
    check_never_drops(vp, glm::vec3(-1700, -1600, -1600), glm::vec3(1600, 1700, 1600), 120.0f);
}

TEST_CASE("culling: a sun cascade keeps its slice and the casters towards the light, nothing past its sides") {
    ShadowCamera cam;
    cam.position = {0, 0, 0};
    cam.forward = {1, 0, 0};
    cam.right = {0, -1, 0};
    cam.up = {0, 0, 1};
    cam.tan_half_x = 1.0f;
    cam.tan_half_y = 0.6f;
    glm::vec3 to_light = glm::normalize(glm::vec3(0.3f, -0.4f, 0.8f));
    auto s = fit_shadow_cascades(cam, to_light, 1200.0f, kShadowCasterReach, 2048);
    for (std::uint32_t c = 0; c < kShadowCascades; ++c) {
        Frustum f = frustum_from(s.view_proj[c]);
        float mid = c == 0 ? s.splits[0] * 0.5f : 0.5f * (s.splits[static_cast<int>(c) - 1] + s.splits[static_cast<int>(c)]);
        glm::vec3 centre = cam.forward * mid;
        CHECK(box_in_frustum(f, box_around(centre, 1)));
        // A caster well towards the light, outside the slice, still shadows it.
        CHECK(box_in_frustum(f, box_around(centre + to_light * (0.9f * kShadowCasterReach), 1)));
        // Beyond the light's end of the box, and well to the side, nothing.
        CHECK_FALSE(box_in_frustum(f, box_around(centre + to_light * (kShadowCasterReach + 4000.0f), 1)));
        glm::vec3 side = glm::normalize(glm::cross(to_light, glm::vec3(0, 0, 1)));
        CHECK_FALSE(box_in_frustum(f, box_around(centre + side * 5000.0f, 1)));
    }
    check_never_drops(s.view_proj[3], glm::vec3(-2500), glm::vec3(2500), 150.0f);
}

TEST_CASE("culling: the cover map's box is what its view sees") {
    CoverMap map = fit_cover_map({1000.0f, -500.0f, 40.0f}, 1024);
    Box b = cover_box(map.cover, map.params);
    // Its corners land on the map's edges and depth range.
    glm::vec4 lo = cover_clip(map, b.min), hi = cover_clip(map, b.max);
    CHECK(lo.x == doctest::Approx(-1.0f));
    CHECK(lo.y == doctest::Approx(-1.0f));
    CHECK(lo.z == doctest::Approx(1.0f));
    CHECK(hi.x == doctest::Approx(1.0f));
    CHECK(hi.y == doctest::Approx(1.0f));
    CHECK(hi.z == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(boxes_overlap(b, box_around({1000.0f, -500.0f, 40.0f}, 1)));
    CHECK_FALSE(boxes_overlap(b, box_around({1000.0f + kCoverHalfSize + 50.0f, -500.0f, 40.0f}, 1)));
    CHECK_FALSE(boxes_overlap(b, box_around({1000.0f, -500.0f, 40.0f + kCoverAbove + 50.0f}, 1)));
    CHECK_FALSE(boxes_overlap(b, box_around({1000.0f, -500.0f, 40.0f - kCoverBelow - 50.0f}, 1)));
}

TEST_CASE("culling: a caster's sphere is kept by every cascade it reaches into, dropped by the others") {
    // The actors' and doors' shadow draws test their bounding spheres so (ActorRenderer / ObjectRenderer::draw_shadow).
    Box b = sphere_box({10.0f, -20.0f, 5.0f}, 4.0f);
    CHECK(b.min == glm::vec3(6.0f, -24.0f, 1.0f));
    CHECK(b.max == glm::vec3(14.0f, -16.0f, 9.0f));
    ShadowCamera cam;
    cam.forward = {1, 0, 0};
    cam.right = {0, -1, 0};
    cam.up = {0, 0, 1};
    cam.tan_half_x = 1.0f;
    cam.tan_half_y = 0.6f;
    glm::vec3 to_light = glm::normalize(glm::vec3(0.3f, -0.4f, 0.8f));
    auto s = fit_shadow_cascades(cam, to_light, 1200.0f, kShadowCasterReach, 2048);
    Frustum near_slice = frustum_from(s.view_proj[0]);
    // At the camera: in the first cascade. Far down the view, well past its slice and to the side: not in it.
    CHECK(box_in_frustum(near_slice, sphere_box({5.0f, 0.0f, 0.0f}, 6.0f)));
    glm::vec3 side = glm::normalize(glm::cross(to_light, glm::vec3(0, 0, 1)));
    CHECK_FALSE(box_in_frustum(near_slice, sphere_box(side * 3000.0f, 6.0f)));
    // A sphere whose centre is outside but whose edge reaches in is kept.
    glm::vec3 out = side * 3000.0f;
    CHECK(box_in_frustum(near_slice, sphere_box(out, glm::length(out) + 10.0f)));
}

TEST_CASE("culling: boxes overlap when they share a point") {
    Box a{{0, 0, 0}, {10, 10, 10}};
    CHECK(boxes_overlap(a, Box{{5, 5, 5}, {6, 6, 6}}));    // inside
    CHECK(boxes_overlap(a, Box{{10, 0, 0}, {20, 10, 10}})); // touching a face
    CHECK(boxes_overlap(a, Box{{-5, -5, -5}, {20, 20, 20}})); // around it
    CHECK_FALSE(boxes_overlap(a, Box{{11, 0, 0}, {20, 10, 10}}));
    CHECK_FALSE(boxes_overlap(a, Box{{0, 0, -9}, {10, 10, -1}}));
}

TEST_CASE("culling: a spawn is posed when it can show, in the view or in a shadow's reach") {
    FlyCamera cam;
    cam.position = {100.0f, 200.0f, 30.0f}; // yaw 0: looking along +X
    Frustum f = frustum_from(cam.projection(1280, 720, 2000.0f) * cam.view());
    const glm::vec3 eye = cam.position;
    // No view set (the viewers): always.
    CHECK(actor_needs_pose(nullptr, {-5000, 0, 0}, 3, false, eye, 260));
    // In view: always, shadows or not.
    CHECK(actor_needs_pose(&f, {300, 200, 30}, 3, false, eye, 260));
    // Behind the camera: only for a shadow, within its reach (the bounds count).
    const glm::vec3 behind(0, 200, 30); // 100 behind
    CHECK_FALSE(actor_needs_pose(&f, behind, 3, false, eye, 260));
    CHECK(actor_needs_pose(&f, behind, 3, true, eye, 260));
    CHECK_FALSE(actor_needs_pose(&f, {-300, 200, 30}, 3, true, eye, 260)); // 400 behind
    CHECK(actor_needs_pose(&f, {-300, 200, 30}, 150, true, eye, 260));     // a big one, its edge within reach
    CHECK(actor_needs_pose(&f, {-300, 200, 30}, 3, true, eye, 0));        // no reach: every spawn casts
    // Partly in view: posed.
    CHECK(actor_needs_pose(&f, {95, 200, 30}, 10, false, eye, 260));
}

TEST_CASE("camera: the FOV offset widens the view without changing the chosen FOV") {
    FlyCamera chosen, widened;
    chosen.fov_units = 100.0f;
    widened.fov_units = 90.0f;
    widened.fov_offset_units = 10.0f;
    CHECK(widened.projection(1280, 720, 1000.0f) == chosen.projection(1280, 720, 1000.0f));
    CHECK(widened.fov_units == 90.0f);
    widened.fov_offset_units = 0.0f;
    chosen.fov_units = 90.0f;
    CHECK(widened.projection(1280, 720, 1000.0f) == chosen.projection(1280, 720, 1000.0f));
}

TEST_CASE("actor_phase_passes splits the actors' passes round the grass") {
    using hailstone::render::ActorPhase;
    using hailstone::render::actor_phase_passes;
    // Opaque: the opaque and alpha-tested pass alone, before the grass; Translucent: blended and additive, after it.
    CHECK(actor_phase_passes(ActorPhase::Opaque).first == 0);
    CHECK(actor_phase_passes(ActorPhase::Opaque).end == 1);
    CHECK(actor_phase_passes(ActorPhase::Translucent).first == 1);
    CHECK(actor_phase_passes(ActorPhase::Translucent).end == 3);
    CHECK(actor_phase_passes(ActorPhase::All).first == 0);
    CHECK(actor_phase_passes(ActorPhase::All).end == 3);
}
