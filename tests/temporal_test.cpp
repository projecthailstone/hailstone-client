// The temporal upscaler's maths (render/temporal.h): jitter, reprojection, history cuts.
#include <doctest/doctest.h>

#include <cmath>

#include "render/camera.h"
#include "render/object_renderer.h"
#include "render/temporal.h"

using namespace hailstone::render;

namespace {

// `p` through `m`, in NDC.
glm::vec3 project(const glm::mat4& m, const glm::vec3& p) {
    const glm::vec4 c = m * glm::vec4(p, 1.0f);
    return glm::vec3(c) / c.w;
}

// The pixel (x right, y down, in pixels) a point at NDC `ndc` falls on, as the scene's flipped viewport maps it.
glm::vec2 pixel(glm::vec2 ndc, float w, float h) {
    return {(ndc.x * 0.5f + 0.5f) * w, (0.5f - ndc.y * 0.5f) * h};
}

FlyCamera camera_at(glm::vec3 pos, float yaw, float pitch) {
    FlyCamera c;
    c.position = pos;
    c.yaw = yaw;
    c.pitch = pitch;
    return c;
}

} // namespace

TEST_CASE("temporal: upscaler names") {
    CHECK(parse_upscaler("taa") == Upscaler::Taa);
    CHECK(parse_upscaler("fsr3") == Upscaler::Fsr3);
    CHECK(parse_upscaler(upscaler_name(Upscaler::Fsr3)) == Upscaler::Fsr3);
    CHECK(upscaler_name(Upscaler::Fsr3) == "fsr3");
    // The Options window's choices are the enum's values (client_app_zone.cpp display_option "upscaler").
    CHECK(static_cast<int>(Upscaler::Fsr1) == 0);
    CHECK(static_cast<int>(Upscaler::Taa) == 1);
    CHECK(static_cast<int>(Upscaler::Fsr3) == 2);
    CHECK(parse_upscaler("fsr1") == Upscaler::Fsr1);
    CHECK(parse_upscaler("") == Upscaler::Fsr1);
    CHECK(parse_upscaler("dlss") == Upscaler::Fsr1); // unknown: the spatial default
    CHECK(parse_upscaler(upscaler_name(Upscaler::Taa)) == Upscaler::Taa);
    CHECK(parse_upscaler(upscaler_name(Upscaler::Fsr1)) == Upscaler::Fsr1);
}

TEST_CASE("temporal: Halton sequence") {
    CHECK(halton(0, 2) == 0.0f);
    CHECK(halton(1, 2) == doctest::Approx(0.5f));
    CHECK(halton(2, 2) == doctest::Approx(0.25f));
    CHECK(halton(3, 2) == doctest::Approx(0.75f));
    CHECK(halton(1, 3) == doctest::Approx(1.0f / 3.0f));
    CHECK(halton(2, 3) == doctest::Approx(2.0f / 3.0f));
    CHECK(halton(3, 3) == doctest::Approx(1.0f / 9.0f));
}

TEST_CASE("temporal: jitter phases grow with the upscale") {
    CHECK(jitter_phases(1.0f) == 8);
    CHECK(jitter_phases(0.5f) == 32);
    CHECK(jitter_phases(0.75f) == 15); // 8 x 1.78, rounded up
    CHECK(jitter_phases(0.33f) == 74); // Ultra Performance: 8 x 9.18
    CHECK(jitter_phases(0.1f) == 74);  // kept to the render scale's range
    CHECK(jitter_phases(2.0f) == 8);
}

TEST_CASE("temporal: jitter stays inside the pixel, repeats, and centres on it") {
    for (std::uint32_t phases : {8u, 15u, 32u}) {
        glm::vec2 sum(0.0f);
        for (std::uint64_t f = 0; f < phases; ++f) {
            const glm::vec2 j = jitter_offset(f, phases);
            CHECK(std::abs(j.x) < 0.5f);
            CHECK(std::abs(j.y) < 0.5f);
            CHECK(j == jitter_offset(f + phases, phases));
            sum += j;
        }
        // Low-discrepancy: a cycle's mean is near the centre.
        CHECK(std::abs(sum.x / static_cast<float>(phases)) < 0.07f);
        CHECK(std::abs(sum.y / static_cast<float>(phases)) < 0.07f);
    }
    // Successive frames differ.
    CHECK(jitter_offset(0, 8) != jitter_offset(1, 8));
    CHECK(jitter_offset(5, 0) == jitter_offset(0, 1)); // no phases: one position, no division by zero
}

TEST_CASE("temporal: a jittered projection moves the image by the jitter in pixels") {
    const FlyCamera cam = camera_at({100.0f, -40.0f, 12.0f}, 0.7f, -0.2f);
    const int w = 1280, h = 720;
    const glm::mat4 proj = cam.projection(w, h, 2000.0f);
    const glm::mat4 vp = proj * cam.view();
    const glm::vec2 jitter(0.25f, -0.375f);
    const glm::mat4 jvp = jittered(proj, jitter_ndc(jitter, w, h)) * cam.view();
    for (glm::vec3 p : {glm::vec3(200.0f, 10.0f, 0.0f), glm::vec3(150.0f, 40.0f, 30.0f), glm::vec3(600.0f, 500.0f, -50.0f)}) {
        const glm::vec3 a = project(vp, p), b = project(jvp, p);
        const glm::vec2 moved = pixel(glm::vec2(b), w, h) - pixel(glm::vec2(a), w, h);
        CHECK(moved.x == doctest::Approx(jitter.x).epsilon(1e-3));
        CHECK(moved.y == doctest::Approx(jitter.y).epsilon(1e-3));
        CHECK(b.z == doctest::Approx(a.z)); // depth is untouched
    }
    CHECK(jittered(proj, glm::vec2(0.0f)) == proj);
}

TEST_CASE("temporal: reprojection takes a still point to where it was a frame ago") {
    // Far from the origin, as EQ zones are: float matrices composed naively lose the sub-pixel motion here.
    const glm::vec3 base(9500.0f, -12000.0f, 300.0f);
    const int w = 1920, h = 1080;
    const FlyCamera prev = camera_at(base, 1.1f, -0.1f);
    const FlyCamera cur = camera_at(base + glm::vec3(0.8f, 0.3f, 0.05f), 1.115f, -0.095f);
    const glm::mat4 pp = prev.projection(w, h, 3000.0f), cp = cur.projection(w, h, 3000.0f);
    const glm::mat4 r = reprojection(prev.view(), pp, cur.view(), cp);
    for (glm::vec3 off : {glm::vec3(40.0f, 120.0f, -5.0f), glm::vec3(-20.0f, 60.0f, 10.0f), glm::vec3(5.0f, 900.0f, 80.0f)}) {
        const glm::vec3 p = base + off;
        const glm::dvec4 cd = glm::dmat4(cp) * glm::dmat4(cur.view()) * glm::dvec4(glm::dvec3(p), 1.0);
        const glm::dvec4 pd = glm::dmat4(pp) * glm::dmat4(prev.view()) * glm::dvec4(glm::dvec3(p), 1.0);
        const glm::vec3 now = glm::vec3(glm::dvec3(cd) / cd.w);
        const glm::vec3 then = glm::vec3(glm::dvec3(pd) / pd.w);
        const glm::vec3 got = project(r, now);
        // Within a hundredth of a pixel.
        const glm::vec2 err = pixel(glm::vec2(got), w, h) - pixel(glm::vec2(then), w, h);
        CHECK(std::abs(err.x) < 0.01f);
        CHECK(std::abs(err.y) < 0.01f);
    }
    // A camera that did not move reprojects every point onto itself.
    const glm::mat4 same = reprojection(cur.view(), cp, cur.view(), cp);
    const glm::vec3 q = project(same, glm::vec3(0.3f, -0.6f, 0.01f));
    CHECK(q.x == doctest::Approx(0.3f).epsilon(1e-4));
    CHECK(q.y == doctest::Approx(-0.6f).epsilon(1e-4));
}

TEST_CASE("temporal: camera cuts drop the history") {
    const FlyCamera a = camera_at({1000.0f, 2000.0f, 50.0f}, 0.3f, 0.0f);
    CHECK_FALSE(camera_cut(a.view(), a.view()));
    // A frame's running or turning keeps it.
    CHECK_FALSE(camera_cut(a.view(), camera_at({1003.0f, 2001.0f, 50.0f}, 0.35f, 0.02f).view()));
    // A teleport, a snap round.
    CHECK(camera_cut(a.view(), camera_at({1100.0f, 2000.0f, 50.0f}, 0.3f, 0.0f).view()));
    CHECK(camera_cut(a.view(), camera_at({1000.0f, 2000.0f, 50.0f}, 1.3f, 0.0f).view()));
    CHECK(camera_cut(a.view(), camera_at({1000.0f, 2000.0f, 50.0f}, 0.3f, 0.9f).view()));
    // A broken matrix is a cut, not a kept history.
    glm::mat4 bad = a.view();
    bad[3][0] = std::nanf("");
    CHECK(camera_cut(a.view(), bad));
}

TEST_CASE("temporal: scene pipelines' blend states for the motion and reactive attachments") {
    VkPipelineColorBlendAttachmentState color{};
    color.colorWriteMask = 0xf;
    color.blendEnable = VK_TRUE;
    // Without a temporal upscaler: the colour alone, as it was.
    const SceneBlends plain = make_scene_blends(color, SceneOutput::Motion, false);
    CHECK(plain.count == 1);
    CHECK(plain.info().attachmentCount == 1);
    CHECK(plain.states[0].blendEnable == VK_TRUE);
    // An opaque draw writes its motion (red and green) as it is, and nothing of the reactive mask.
    const SceneBlends motion = make_scene_blends(color, SceneOutput::Motion, true);
    CHECK(motion.count == 3);
    CHECK(motion.info().pAttachments == motion.states.data());
    CHECK(motion.states[0].colorWriteMask == 0xfu);
    CHECK(motion.states[1].colorWriteMask == (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT));
    CHECK(motion.states[1].blendEnable == VK_FALSE);
    CHECK(motion.states[2].colorWriteMask == 0u);
    // A blended draw keeps the motion under it and raises the reactive mask to the largest drawn.
    const SceneBlends reactive = make_scene_blends(color, SceneOutput::Reactive, true);
    CHECK(reactive.states[1].colorWriteMask == 0u);
    CHECK(reactive.states[2].colorWriteMask == VK_COLOR_COMPONENT_R_BIT);
    CHECK(reactive.states[2].blendEnable == VK_TRUE);
    CHECK(reactive.states[2].colorBlendOp == VK_BLEND_OP_MAX);
    // The sky, the spawns' boxes: neither.
    const SceneBlends nothing = make_scene_blends(color, SceneOutput::Nothing, true);
    CHECK(nothing.count == 3);
    CHECK(nothing.states[1].colorWriteMask == 0u);
    CHECK(nothing.states[2].colorWriteMask == 0u);
}

TEST_CASE("temporal: object motion keys tell things apart") {
    using hailstone::render::ObjectRenderer;
    // A door, an object and a placed item with the same id are different things; none is "no key".
    const std::uint64_t door = ObjectRenderer::make_motion_key(1, 7);
    const std::uint64_t object = ObjectRenderer::make_motion_key(2, 7);
    const std::uint64_t placed = ObjectRenderer::make_motion_key(3, 7);
    CHECK(door != object);
    CHECK(object != placed);
    CHECK(door != placed);
    CHECK(door != 0);
    CHECK(ObjectRenderer::make_motion_key(1, 0) != 0);
    CHECK(ObjectRenderer::make_motion_key(1, 8) != door);
}

TEST_CASE("temporal: upscale quality presets are render scales") {
    CHECK(upscale_quality_percent(UpscaleQuality::NativeAa) == 100);
    CHECK(upscale_quality_percent(UpscaleQuality::Quality) == 67);           // 1.5x
    CHECK(upscale_quality_percent(UpscaleQuality::Balanced) == 59);          // 1.7x
    CHECK(upscale_quality_percent(UpscaleQuality::Performance) == 50);       // 2x
    CHECK(upscale_quality_percent(UpscaleQuality::UltraPerformance) == 33);  // 3x
    CHECK(upscale_quality_percent(UpscaleQuality::Custom) == 0);             // the slider's own
    for (int i = 0; i < 5; ++i) {
        const auto q = static_cast<UpscaleQuality>(i);
        CHECK(upscale_quality_for(upscale_quality_percent(q)) == q);
        CHECK(upscale_quality_percent(q) >= kMinRenderScalePercent);
    }
    CHECK(upscale_quality_for(75) == UpscaleQuality::Custom);
    CHECK(upscale_quality_for(66) == UpscaleQuality::Custom);
    CHECK(static_cast<float>(kMinRenderScalePercent) / 100.0f == doctest::Approx(kMinRenderScale));
}
