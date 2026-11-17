#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include "assets/model.h"
#include "render/material_state.h"
#include "render/zone_renderer.h"

using namespace hailstone;

// The zone shaders' alpha coverage, compiled as C++ with GLM's float operations (shaders/sample_coverage.glsl).
namespace sample_coverage_test {
using glm::vec2;
using glm::clamp;
using glm::dot;
using glm::floor;
using glm::fract;
#include "../shaders/sample_coverage.glsl"
} // namespace sample_coverage_test

// The depth pre-pass draws the opaque pass's materials and takes its alpha from the texture, or 1
// (shaders/zone_depth.frag). The effects whose alpha zone.frag computes another way must stay out of that pass.
TEST_CASE("depth pre-pass: the effects with an alpha of their own are not in the opaque pass") {
    using assets::EqgShader;
    for (EqgShader shader : {EqgShader::Water, EqgShader::LavaH, EqgShader::WaterFall, EqgShader::WldDetail}) {
        assets::Material m;
        m.eqg_shader = shader;
        CHECK(render::material_state(m).pass != 0);
    }
}

TEST_CASE("depth pre-pass: the effects it draws are in the opaque pass") {
    using assets::EqgShader;
    for (EqgShader shader : {EqgShader::None, EqgShader::Chroma, EqgShader::Terrain, EqgShader::Lava, EqgShader::Lava2}) {
        assets::Material m;
        m.eqg_shader = shader;
        CHECK(render::material_state(m).pass == 0);
    }
}

namespace {

std::string read_shader(const char* name) {
    std::ifstream in(std::string(HAILSTONE_SOURCE_DIR "/shaders/") + name, std::ios::binary);
    REQUIRE(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The calls in a shader's code (not its comments) to a helper that decides a discard: the *_discard helpers
// (lighting.glsl reveal_discard), the alpha test (coverage.glsl alpha_test) and a level of detail's sample mask
// (coverage.glsl lod_fade_samples).
std::set<std::string> discard_helpers(const std::string& source) {
    const std::string code = std::regex_replace(source, std::regex(R"(//[^\n]*)"), "");
    std::set<std::string> found;
    const std::regex call(R"(\b(\w+_discard|alpha_test|lod_fade_samples)\s*\()");
    for (auto it = std::sregex_iterator(code.begin(), code.end(), call); it != std::sregex_iterator(); ++it)
        found.insert((*it)[1].str());
    return found;
}

std::string strip_comments(const std::string& source) {
    return std::regex_replace(source, std::regex(R"(//[^\n]*)"), "");
}

// Every `gl_SampleMask[0] = ...;` statement in a shader's code, whitespace collapsed.
std::vector<std::string> sample_mask_writes(const std::string& source) {
    const std::string code = strip_comments(source);
    std::vector<std::string> found;
    const std::regex write(R"(gl_SampleMask\[0\]\s*=\s*([^;]*);)");
    for (auto it = std::sregex_iterator(code.begin(), code.end(), write); it != std::sregex_iterator(); ++it)
        found.push_back(std::regex_replace((*it)[1].str(), std::regex(R"(\s+)"), " "));
    return found;
}

} // namespace

// The colour pass over the pre-pass tests depth equal and writes none: a pixel zone.frag discards that the depth
// shader keeps would hide what is behind it (the isometric camera's cut walls: a hole of clear colour).
TEST_CASE("depth pre-pass: the depth shader discards where zone.frag does") {
    const auto colour = discard_helpers(read_shader("zone.frag"));
    const auto depth = discard_helpers(read_shader("zone_depth.frag"));
    CHECK(colour.count("reveal_discard") == 1);
    // A level of detail's fade: the same samples in both (coverage.glsl lod_fade_samples).
    CHECK(colour.count("lod_fade_samples") == 1);
    // A neighbour's part of the land: the same side of the blend in both (coverage.glsl land_blend_discard).
    CHECK(colour.count("land_blend_discard") == 1);
    for (const auto& helper : colour) {
        INFO("zone.frag calls " << helper << ", zone_depth.frag does not");
        CHECK(depth.count(helper) == 1);
    }
}

// Under MSAA the colour pass over the pre-pass tests depth equal: the two must cover the same samples of an
// alpha-tested edge or a level of detail's fade, so both write one mask, the same, after the alpha test.
TEST_CASE("depth pre-pass: zone.frag and zone_depth.frag write the same sample mask") {
    for (const char* name : {"zone.frag", "zone_depth.frag"}) {
        INFO(name);
        const std::string code = strip_comments(read_shader(name));
        const auto writes = sample_mask_writes(code);
        REQUIRE(writes.size() == 1);
        CHECK(writes[0] == "lod_samples & alpha_coverage_samples(out_alpha, gl_FragCoord.xy)");
        const auto test = code.find("alpha_test(");
        REQUIRE(test != std::string::npos);
        CHECK(test < code.find("gl_SampleMask[0]"));
    }
}

TEST_CASE("sample coverage: an alpha covers its share of the samples") {
    using sample_coverage_test::coverage_sample_mask;
    for (int count : {2, 4, 8}) {
        INFO("samples " << count);
        const auto all = static_cast<unsigned>((1 << count) - 1);
        for (float alpha : {0.0f, 0.1f, 0.25f, 0.5f, 0.6f, 0.75f, 0.9f, 1.0f}) {
            INFO("alpha " << alpha);
            const float want = alpha * static_cast<float>(count);
            double sum = 0.0;
            int pixels = 0;
            for (int y = 0; y < 64; ++y) {
                for (int x = 0; x < 64; ++x) {
                    const glm::vec2 frag(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                    const auto mask = static_cast<unsigned>(coverage_sample_mask(alpha, frag, count));
                    REQUIRE((mask & ~all) == 0u);
                    const int bits = std::popcount(mask);
                    // Rounded by the pixel's noise: the share below or above, never further.
                    CHECK(static_cast<float>(bits) >= std::floor(want) - 1e-4f);
                    CHECK(static_cast<float>(bits) <= std::ceil(want) + 1e-4f);
                    if (alpha == 0.0f) CHECK(mask == 0u);
                    if (alpha == 1.0f) CHECK(mask == all);
                    sum += bits;
                    ++pixels;
                }
            }
            // Over many pixels the samples average to the alpha.
            CHECK(std::abs(sum / pixels - static_cast<double>(want)) < 0.15);
        }
    }
    // Out-of-range alpha clamps to none or all.
    CHECK(coverage_sample_mask(-0.5f, glm::vec2(3.5f, 7.5f), 4) == 0);
    CHECK(coverage_sample_mask(1.5f, glm::vec2(3.5f, 7.5f), 4) == 15);
}

// A faded-out lawn blade has zero height (shaders/lawn.vert blade_at): its rise must not divide by it, or 0/0 sends
// a NaN to gl_Position and the previous frame's position.
TEST_CASE("lawn: a zero-height blade does not divide by its height") {
    const std::string code = strip_comments(read_shader("lawn.vert"));
    CHECK(code.find("(height * height)") == std::string::npos);
    const std::regex guarded(R"(if\s*\(\s*height\s*>\s*0\.0\s*\)\s*bend_fraction\s*=\s*reach\s*/\s*height\s*;)");
    CHECK(std::regex_search(code, guarded));
    // No other division by the height.
    const std::regex divide(R"(/\s*height\b)");
    CHECK(std::distance(std::sregex_iterator(code.begin(), code.end(), divide), std::sregex_iterator()) == 1);
}

// display.depth_prepass is switched in the game (the Options window, ZoneRenderer::set_depth_prepass) from one frame
// to the next: a zone builds the pre-pass's pipelines whether it is on or not, and draws it only when it is.
TEST_CASE("depth pre-pass: drawn when switched on, its pipelines built, in the modern lighting") {
    using render::ZoneRenderer;
    CHECK(ZoneRenderer::prepass_draws(true, true, true));
    CHECK_FALSE(ZoneRenderer::prepass_draws(false, true, true));  // switched off
    CHECK_FALSE(ZoneRenderer::prepass_draws(true, false, true));  // a neighbour zone, or no zone_depth.frag.spv
    CHECK_FALSE(ZoneRenderer::prepass_draws(true, true, false));  // the classic lighting has nothing to save
}

TEST_CASE("depth pre-pass: both UI packs' Options windows offer the switch") {
    for (const char* pack : {"modern", "gamepad"}) {
        std::ifstream in(std::string(HAILSTONE_SOURCE_DIR "/ui/") + pack + "/options.lua", std::ios::binary);
        REQUIRE(in);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        INFO(pack);
        CHECK(text.find("\"depth_prepass\"") != std::string::npos);
    }
}
