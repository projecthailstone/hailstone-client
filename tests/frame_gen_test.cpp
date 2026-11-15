// Frame generation's decisions (render/frame_gen_logic.h).
#include <doctest/doctest.h>

#include "render/frame_gen_logic.h"

using namespace hailstone::render;

TEST_CASE("frame generation: a frame is made only between two real frames in a row of one scene") {
    FrameGenState s;
    s.enabled = true;
    s.temporal_frame = true;
    s.history_kept = true;
    s.previous_ready = true;
    CHECK(frame_gen_ready(s));
    FrameGenState off = s;
    off.enabled = false;
    CHECK_FALSE(frame_gen_ready(off));
    FrameGenState no_motion = s; // no temporal upscaler this frame (no 3D scene: the login screen)
    no_motion.temporal_frame = false;
    CHECK_FALSE(frame_gen_ready(no_motion));
    FrameGenState cut = s; // a teleport, a zone, a resize: the history dropped
    cut.history_kept = false;
    CHECK_FALSE(frame_gen_ready(cut));
    FrameGenState first = s; // the first frame since the pictures were made: nothing to start from
    first.previous_ready = false;
    CHECK_FALSE(frame_gen_ready(first));
    FrameGenState snap = s; // the chase camera pushed in by a tree: the history goes on, no frame is made
    snap.camera_steady = false;
    CHECK_FALSE(frame_gen_ready(snap));
    // Where none is made the real frame is shown twice, keeping the pace; with frame generation off, neither.
    CHECK_FALSE(frame_gen_repeat(s));
    CHECK(frame_gen_repeat(snap));
    CHECK(frame_gen_repeat(cut));
    CHECK(frame_gen_repeat(first));
    CHECK_FALSE(frame_gen_repeat(off));
    CHECK(kFrameGenTime == doctest::Approx(0.5f));
}

TEST_CASE("frame generation: one more swapchain image, and the second may be waited for") {
    CHECK(swapchain_image_count(2, 0, false) == 3);
    CHECK(swapchain_image_count(2, 0, true) == 4);
    CHECK(swapchain_image_count(3, 8, true) == 5);
    CHECK(swapchain_image_count(2, 3, true) == 3); // the surface's limit
    CHECK(frame_gen_fits(swapchain_image_count(2, 0, true), 2));
    CHECK(frame_gen_fits(swapchain_image_count(3, 0, true), 3));
    CHECK_FALSE(frame_gen_fits(swapchain_image_count(2, 3, true), 2)); // capped: the second only when free
    CHECK_FALSE(frame_gen_fits(swapchain_image_count(2, 0, false), 2));
}
