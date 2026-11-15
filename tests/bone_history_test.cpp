// render/bone_history.h: the bones each spawn was drawn with a frame ago, kept on the CPU for the upscaler's motion.
#include <doctest/doctest.h>

#include <vector>

#include "render/bone_history.h"

using namespace hailstone::render;

namespace {

std::vector<glm::mat4> pose(float x, std::size_t count) {
    std::vector<glm::mat4> out(count, glm::mat4(1.0f));
    for (std::size_t i = 0; i < count; ++i) out[i][3][0] = x + static_cast<float>(i);
    return out;
}

} // namespace

TEST_CASE("bone history: a spawn's bones come back the next frame") {
    BoneHistory h;
    h.begin_frame(1);
    auto a = pose(1.0f, 3);
    CHECK(h.keep(7, a.data(), a.size()) == nullptr); // new: none a frame ago

    h.begin_frame(2);
    auto b = pose(2.0f, 3);
    const auto* before = h.keep(7, b.data(), b.size());
    REQUIRE(before);
    CHECK(*before == a);

    h.begin_frame(3);
    before = h.keep(7, a.data(), a.size());
    REQUIRE(before);
    CHECK(*before == b);
}

TEST_CASE("bone history: a second pass in the same frame keeps the last frame's") {
    BoneHistory h;
    h.begin_frame(1);
    auto a = pose(1.0f, 2);
    h.keep(7, a.data(), a.size());
    h.begin_frame(2);
    auto b = pose(2.0f, 2);
    h.keep(7, b.data(), b.size());
    h.begin_frame(2); // the same frame again
    const auto* before = h.keep(7, b.data(), b.size());
    REQUIRE(before);
    CHECK(*before == a);
}

TEST_CASE("bone history: a changed shape or a missed frame has no motion") {
    BoneHistory h;
    h.begin_frame(1);
    auto a = pose(1.0f, 2);
    h.keep(7, a.data(), a.size());
    h.keep(8, a.data(), a.size());
    h.begin_frame(2);
    auto c = pose(3.0f, 4);
    CHECK(h.keep(7, c.data(), c.size()) == nullptr); // a different number of bones
    // 8 is not drawn in frame 2, so it is dropped when frame 3 begins.
    h.begin_frame(3);
    CHECK(h.size() == 1);
    CHECK(h.keep(8, a.data(), a.size()) == nullptr);
    REQUIRE(h.keep(7, c.data(), c.size()));
}

TEST_CASE("bone history: settled frames reuse the vectors") {
    BoneHistory h;
    auto a = pose(1.0f, 40);
    h.begin_frame(1);
    h.keep(7, a.data(), a.size());
    h.begin_frame(2);
    const auto* first = h.keep(7, a.data(), a.size());
    REQUIRE(first);
    const glm::mat4* storage = first->data();
    h.begin_frame(3);
    h.keep(7, a.data(), a.size());
    h.begin_frame(4);
    const auto* again = h.keep(7, a.data(), a.size());
    REQUIRE(again);
    CHECK(again->data() == storage); // the same buffer, two frames on: nothing was reallocated
}
