#include <doctest/doctest.h>

#include "app/perf_ui.h"
#include "core/frame_sections.h"

using namespace hailstone::app;

TEST_CASE("perf: a hitch is 2.5 times the typical frame, 20 ms at least") {
    CHECK_FALSE(is_hitch(60.0f, 0.0f)); // no typical time yet
    CHECK_FALSE(is_hitch(19.0f, 4.0f));
    CHECK(is_hitch(20.0f, 4.0f));
    CHECK_FALSE(is_hitch(40.0f, 16.7f));
    CHECK(is_hitch(42.0f, 16.7f));
}

TEST_CASE("perf: a hitch's log line lists its phases longest first") {
    PhaseTimes phases{};
    phases[static_cast<std::size_t>(FramePhase::Update)] = 50.0f;
    phases[static_cast<std::size_t>(FramePhase::Record)] = 3.0f;
    phases[static_cast<std::size_t>(FramePhase::Ui)] = 0.5f; // under 1 ms: left out
    CHECK(hitch_text(60.0f, 8.0f, phases) == "hitch 60.0 ms (typical 8.0): Update 50.0 other 6.5 Record 3.0");
}

TEST_CASE("perf: the sections of a hitch follow its phases") {
    PhaseTimes phases{};
    phases[static_cast<std::size_t>(FramePhase::Ui)] = 50.0f;
    CHECK(hitch_text(52.0f, 8.0f, phases, {{"draw_ui", 2.0f}, {"Lua UI frame", 47.5f}, {"perf window", 0.4f}}) ==
          "hitch 52.0 ms (typical 8.0): UI 50.0 other 2.0; in it: Lua UI frame 47.5, draw_ui 2.0");
}

TEST_CASE("perf: section averages are per frame over the last frames, longest first") {
    static const char* const a = "a";
    static const char* const b = "b";
    SectionAverages avg(3);
    CHECK(avg.rows().empty());
    avg.add({{a, 9.0f}});           // dropped: only the last 3 frames count
    avg.add({{a, 1.0f}, {b, 6.0f}});
    avg.add({{a, 2.0f}});           // b missing here: 0 ms for this frame
    avg.add({{a, 3.0f}, {b, 3.0f}});
    CHECK(avg.frames() == 3);
    auto rows = avg.rows();
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].name == b);
    CHECK(rows[0].avg_ms == doctest::Approx(3.0f));
    CHECK(rows[0].max_ms == doctest::Approx(6.0f));
    CHECK(rows[1].name == a);
    CHECK(rows[1].avg_ms == doctest::Approx(2.0f));
    CHECK(rows[1].max_ms == doctest::Approx(3.0f));
}

TEST_CASE("perf: section averages carry the bytes each section allocated a frame") {
    static const char* const lua = "Lua window A";
    static const char* const draw = "draw_ui";
    SectionAverages avg(2);
    avg.add({{lua, 1.0f}, {draw, 1.0f}}, {{lua, 3000}});
    avg.add({{lua, 1.0f}}, {{lua, 1000}});
    auto rows = avg.rows();
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].name == lua);
    CHECK(rows[0].has_bytes);
    CHECK(rows[0].avg_bytes == doctest::Approx(2000.0f));
    CHECK(rows[1].name == draw);
    CHECK_FALSE(rows[1].has_bytes); // no bytes counted for it: not the same as 0
}

TEST_CASE("perf: frame sections add up by name and are emptied when taken") {
    namespace fs = hailstone::core::frame_sections;
    fs::take();
    fs::add("a", 1.0f);
    fs::add("b", 2.0f);
    fs::add("a", 3.0f);
    auto taken = fs::take();
    REQUIRE(taken.size() == 2);
    CHECK(taken[0].second == doctest::Approx(4.0f));
    CHECK(taken[1].second == doctest::Approx(2.0f));
    CHECK(fs::take().empty());

    fs::take_bytes();
    fs::add_bytes("a", 100);
    fs::add_bytes("a", 50);
    auto bytes = fs::take_bytes();
    REQUIRE(bytes.size() == 1);
    CHECK(bytes[0].second == 150);
    CHECK(fs::take_bytes().empty());
}

TEST_CASE("perf: frame generation's rates count every image displayed against the real frames") {
    // Four real frames of 20 ms: three had an in-between frame.
    const FrameRates r = frame_rates({20.0f, 20.0f, 20.0f, 20.0f}, {2, 2, 1, 2});
    CHECK(r.rendered_fps == doctest::Approx(50.0f));
    CHECK(r.displayed_fps == doctest::Approx(87.5f));
    CHECK(r.generated_share == doctest::Approx(0.75f));
    // Without frame generation both rates are the same.
    const FrameRates off = frame_rates({10.0f, 10.0f}, {1, 1});
    CHECK(off.displayed_fps == doctest::Approx(off.rendered_fps));
    CHECK(off.generated_share == doctest::Approx(0.0f));
    // Nothing yet, or histories that do not match: no rates.
    CHECK(frame_rates({}, {}).rendered_fps == 0.0f);
    CHECK(frame_rates({10.0f}, {}).displayed_fps == 0.0f);
}
