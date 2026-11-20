// game::SpellEffectTimers (eqgame's SpellEffectTimers_Add / _Update / _Remove) and the effect packets reaching the
// app's queue (GameConnection::take_effect_packets), against the mock EQEmu.

#include <doctest/doctest.h>

#include "game/spell_effect_timers.h"
#include "mock_server/harness.h"
#include "net/messages/spell.h"

using namespace hailstone;
using namespace hailstone::test;
using game::SpellEffectTimers;

TEST_CASE("spell effect timers: the cast stage at once, the projectile and impact stages as their starts pass") {
    SpellEffectTimers t;
    t.add(7, 9, 468, 500, 200, 3000, true, 1000);
    auto plays = t.take();
    REQUIRE(plays.size() == 1);
    CHECK(plays[0].stage == 0);
    CHECK(plays[0].duration_ms == 500); // the cast stage lasts until the projectile stage
    CHECK(plays[0].caster == 7);
    CHECK(plays[0].target == 9);
    CHECK(plays[0].effect == 468);
    CHECK(plays[0].full);

    t.update(1499); // elapsed 499: not past 500
    CHECK(t.take().empty());
    t.update(1501);
    plays = t.take();
    REQUIRE(plays.size() == 1);
    CHECK(plays[0].stage == 1);
    CHECK(plays[0].duration_ms == 200);
    t.update(1701); // elapsed 701 > 700
    plays = t.take();
    REQUIRE(plays.size() == 1);
    CHECK(plays[0].stage == 2);
    CHECK(plays[0].duration_ms == 3000);
    CHECK(t.empty()); // past the impact stage's start: dropped
}

TEST_CASE("spell effect timers: EQEmu's nimbus {500, 0, 3000} plays stages 1 and 2 together") {
    SpellEffectTimers t;
    t.add(7, 7, 468, 500, 0, 3000, true, 0);
    t.take();
    t.update(600);
    auto plays = t.take();
    REQUIRE(plays.size() == 2);
    CHECK(plays[0].stage == 1);
    CHECK(plays[0].duration_ms == 0);
    CHECK(plays[1].stage == 2);
    CHECK(plays[1].duration_ms == 3000);
    CHECK(t.empty());
}

TEST_CASE("spell effect timers: an update landing exactly on a stage's start skips it, as the original's does") {
    SpellEffectTimers t;
    t.add(1, 1, 5, 500, 500, 100, false, 0);
    t.take();
    t.update(500); // elapsed 500: neither before (500 > 500 fails) nor after (500 < 500 fails)
    t.update(600);
    CHECK(t.take().empty());
    t.update(1001);
    auto plays = t.take();
    REQUIRE(plays.size() == 1);
    CHECK(plays[0].stage == 2);
}

TEST_CASE("spell effect timers: a stage starting at 0 never plays (the compares are strict)") {
    SpellEffectTimers t;
    t.add(1, 1, 5, 0, 0, 100, false, 0);
    REQUIRE(t.take().size() == 1); // the cast stage only
    t.update(50);
    CHECK(t.take().empty());
    CHECK(t.empty());
}

TEST_CASE("spell effect timers: an entry whose stage 2 starts near 2^32 is dropped once elapsed tops out") {
    // A deliberate change: the original's 32-bit elapsed wraps back under such a start and keeps the entry for good.
    SpellEffectTimers t;
    t.add(1, 1, 5, 10, 0xfffffff5u, 100, false, 0); // stage 2 at 0xffffffff, which no elapsed passes
    t.take();
    t.update(0x80000000ull);
    CHECK_FALSE(t.empty());
    t.update(0x100000010ull); // the 32-bit sum would wrap to 0x10
    CHECK(t.empty());
}

TEST_CASE("spell effect timers: remove drops every entry with the caster, target and effect") {
    SpellEffectTimers t;
    t.add(1, 2, 5, 1000, 0, 0, false, 0);
    t.add(1, 2, 5, 1000, 0, 0, false, 0);
    t.add(1, 3, 5, 1000, 0, 0, false, 0);
    CHECK_FALSE(t.remove(1, 2, 6));
    CHECK(t.remove(1, 2, 5));
    CHECK_FALSE(t.remove(1, 2, 5));
    t.take();
    t.update(2000);
    auto plays = t.take();
    REQUIRE(plays.size() == 2); // the 1 -> 3 entry's projectile and impact stages (both start at 1000)
    CHECK(plays[0].target == 3);
    CHECK(plays[1].target == 3);
}

TEST_CASE("effect packets: OP_SpellEffect and the nimbus packets reach the queue in order") {
    Rig rig;
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    conn.take_effect_packets();
    conn.receive_zone_message_for_test({net::messages::GameOpcode::SpellEffect, net::messages::SpellVisualEffect{468, 12, 12, 500, 0, 3000, true, true}});
    conn.receive_zone_message_for_test({net::messages::GameOpcode::AddNimbusEffect, net::messages::NimbusVisualEffect{12, 468}});
    conn.receive_zone_message_for_test({net::messages::GameOpcode::RemoveNimbusEffect, net::messages::NimbusVisualEffect{12, 468}});
    conn.receive_zone_message_for_test({net::messages::GameOpcode::SpellEffect, net::messages::SpellVisualEffect{468, 12, 13, 0, 0, 0, false, false}});
    auto q = conn.take_effect_packets();
    using Kind = net::EffectPacketCue::Kind;
    REQUIRE(q.size() == 4);
    CHECK(q[0].kind == Kind::Add);
    CHECK(q[0].effect == 468);
    CHECK(q[0].stage1_ms == 500);
    CHECK(q[0].stage2_length_ms == 3000);
    CHECK(q[0].full);
    CHECK(q[1].kind == Kind::AddNimbus);
    CHECK(q[1].caster == 12);
    CHECK(q[1].target == 12);
    CHECK(q[2].kind == Kind::RemoveNimbus);
    CHECK(q[3].kind == Kind::Remove);
    CHECK(q[3].target == 13);
    CHECK(conn.take_effect_packets().empty());
}
