#include "render/temporal.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <glm/gtc/matrix_transform.hpp>

namespace hailstone::render {

Upscaler parse_upscaler(std::string_view name) {
    if (name == "taa") return Upscaler::Taa;
    if (name == "fsr3") return Upscaler::Fsr3;
    return Upscaler::Fsr1;
}

std::string_view upscaler_name(Upscaler u) {
    return u == Upscaler::Fsr3 ? "fsr3" : u == Upscaler::Taa ? "taa" : "fsr1";
}

UpscaleQuality upscale_quality_for(int percent) {
    for (std::size_t i = 0; i < std::size(kUpscaleQualityPercent); ++i)
        if (kUpscaleQualityPercent[i] == percent) return static_cast<UpscaleQuality>(i);
    return UpscaleQuality::Custom;
}

int upscale_quality_percent(UpscaleQuality q) {
    const auto i = static_cast<std::size_t>(q);
    return i < std::size(kUpscaleQualityPercent) ? kUpscaleQualityPercent[i] : 0;
}

float halton(std::uint32_t index, std::uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

std::uint32_t jitter_phases(float scale) {
    scale = std::clamp(scale, kMinRenderScale, 1.0f);
    const float ratio = 1.0f / scale;
    return static_cast<std::uint32_t>(std::ceil(8.0f * ratio * ratio - 1e-4f));
}

glm::vec2 jitter_offset(std::uint64_t frame, std::uint32_t phases) {
    phases = std::max(phases, 1u);
    // The sequence's first point (index 0) is 0, 0: start at 1 so a cycle has no sample on the pixel's centre twice.
    const auto index = static_cast<std::uint32_t>(frame % phases) + 1;
    return {halton(index, 2) - 0.5f, halton(index, 3) - 0.5f};
}

glm::vec2 jitter_ndc(glm::vec2 jitter_px, std::uint32_t width, std::uint32_t height) {
    return {2.0f * jitter_px.x / static_cast<float>(std::max(width, 1u)),
            -2.0f * jitter_px.y / static_cast<float>(std::max(height, 1u))};
}

glm::mat4 jittered(const glm::mat4& proj, glm::vec2 ndc) {
    // An NDC offset is a clip-space offset scaled by w: a translation applied after the projection.
    return glm::translate(glm::mat4(1.0f), glm::vec3(ndc, 0.0f)) * proj;
}

glm::mat4 reprojection(const glm::mat4& prev_view, const glm::mat4& prev_proj, const glm::mat4& view,
                       const glm::mat4& proj) {
    // prev_proj * prev_view * inverse(view) * inverse(proj): the two views' product is the camera's motion over the
    // frame, small numbers even far from the origin once taken in double.
    const glm::dmat4 motion = glm::dmat4(prev_view) * glm::inverse(glm::dmat4(view));
    return glm::mat4(glm::dmat4(prev_proj) * motion * glm::inverse(glm::dmat4(proj)));
}

bool camera_cut(const glm::mat4& prev_view, const glm::mat4& view, float max_step, float max_turn) {
    const glm::dmat4 a = glm::inverse(glm::dmat4(prev_view));
    const glm::dmat4 b = glm::inverse(glm::dmat4(view));
    const glm::dvec3 step = glm::dvec3(b[3]) - glm::dvec3(a[3]);
    if (!(glm::length(step) <= static_cast<double>(max_step))) return true; // NaN counts as a cut
    // The view direction is -Z in view space.
    const glm::dvec3 fa = -glm::normalize(glm::dvec3(a[2]));
    const glm::dvec3 fb = -glm::normalize(glm::dvec3(b[2]));
    const double turn = std::acos(std::clamp(glm::dot(fa, fb), -1.0, 1.0));
    return !(turn <= static_cast<double>(max_turn));
}

} // namespace hailstone::render
