#include <doctest/doctest.h>

#include "game/position_tracker.h"

using hailstone::game::PositionTracker;

namespace {

PositionTracker::Gate playing() { return {true, true, true, true}; }


} // namespace

TEST_CASE("PositionTracker: nothing before it is enabled or outside the game") {
    PositionTracker t;
    CHECK_FALSE(t.record({1, 2, 3}, 4, 100, playing()));
    CHECK(t.empty());
    t.enable();
    PositionTracker::Gate g = playing();
    g.controlled = false;
    t.record({1, 2, 3}, 2, 100, g);
    CHECK(t.empty());
    // Out of the main loop moving samples are dropped, the others kept.
    g = playing();
    g.main_loop = false;
    t.record({1, 2, 3}, 1, 100, g);
    CHECK(t.empty());
    t.record({1, 2, 3}, 2, 100, g);
    CHECK(t.size() == 1);
}

TEST_CASE("PositionTracker: moving samples at the same spot within a second are dropped") {
    PositionTracker t;
    t.enable();
    t.record({1, 2, 3}, 1, 1000, playing());
    t.record({1, 2, 3}, 1, 1999, playing());
    CHECK(t.size() == 1);
    t.record({1, 2, 3}, 1, 2000, playing());
    CHECK(t.size() == 2);
    t.record({1, 2, 4}, 1, 2001, playing());
    CHECK(t.size() == 3);
    CHECK(t.sequence() == 3);
    // Any sample moves the time on: the same spot a moment after an update is dropped.
    t.record({9, 9, 9}, 2, 2500, playing());
    t.record({1, 2, 4}, 1, 3400, playing());
    CHECK(t.size() == 4);
}

TEST_CASE("PositionTracker: teleport, zone line and failed teleport samples go at once") {
    PositionTracker t;
    t.enable();
    CHECK(t.record({0, 0, 0}, 3, 10, playing()));
    CHECK(t.record({0, 0, 0}, 4, 10, playing()));
    CHECK(t.record({0, 0, 0}, 5, 10, playing()));
    CHECK(t.record({0, 0, 0}, 8, 10, playing()));
    CHECK_FALSE(t.record({0, 0, 0}, 2, 10, playing()));
    CHECK_FALSE(t.record({0, 0, 0}, 7, 10, playing()));
    CHECK_FALSE(t.record({0, 0, 0}, 9, 10, playing()));
}

TEST_CASE("PositionTracker: the entries, oldest first, and only the older ones unless forced") {
    PositionTracker t;
    t.enable();
    t.record({1.5f, -2.0f, 3.25f}, 1, 0x12345, playing());
    t.record({4, 5, 6}, 2, 0x20000, playing());
    // 0x20000 - 0x12345 = 56507 ms: both are older than 2 s at 0x20000 + 2001; only the first at 0x20000 + 2000.
    auto entries = t.take(false, 0x20000 + 2000);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].sample.position[0] == 1.5f);
    CHECK(entries[0].sample.position[1] == -2.0f);
    CHECK(entries[0].sample.position[2] == 3.25f);
    CHECK(entries[0].sample.reason == 1);
    CHECK(entries[0].sample.time == 0x12345u);
    CHECK(entries[0].ordinal == 0);
    CHECK(t.size() == 1);
    CHECK(t.take(false, 0x20000 + 2000).empty());
    entries = t.take(true, 0x20000);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].sample.reason == 2);
    CHECK(entries[0].ordinal == 1);
    CHECK(t.empty());
    // A flush forgets the samples and sets the sample count back to the entry count.
    t.record({0, 0, 0}, 2, 1, playing());
    t.record({0, 0, 0}, 2, 2, playing());
    CHECK(t.sequence() == 4);
    t.flush();
    CHECK(t.empty());
    CHECK(t.sequence() == 2);
}
