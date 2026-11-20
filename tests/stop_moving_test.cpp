// A spawn that feigns, sits or is stunned while it moves stops where it is. PlayerZoneClient::ChangePosition
// @0x5a0bc0's StopMoving clears SpeedRun (+0x7c) and the turn rate (+0x8c) for sitting, looting and lying down;
// CharacterZoneClient::StunMe @0x4432c0 clears the local player's SpeedRun when a stun starts. Without them another
// spawn ran on with its last update's speed for up to 12 s, and the local player slid on while it could not move.

#include <doctest/doctest.h>

#include <vector>

#include <glm/glm.hpp>

#include <memory>
#include <string>

#include "app/player_movement.h"
#include "core/game_dir.h"
#include "game/collision.h"
#include "game/mechanics/collision_movement.h"
#include "game/spells.h"
#include "mock_server/harness.h"
#include "net/connection/game_connection.h"
#include "net/messages/message.h"
#include "net/messages/notice.h"
#include "net/messages/spell.h"
#include "net/messages/zone.h"
#include "scratch_dir.h"

using namespace hailstone;
using namespace hailstone::net;

namespace {

messages::GameMessage appearance(std::uint32_t id, std::uint32_t state) {
    return {messages::GameOpcode::SpawnAppearance,
            SpawnAppearance{static_cast<std::uint16_t>(id), kAppearanceAnimation, state}};
}

messages::GameMessage stun(std::uint32_t ms, bool lean = false) {
    return {messages::GameOpcode::Stun, StunNotice{ms, lean}};
}

// A spells_us.txt line: one effect in slot 1 (the rest 254), a long duration.
std::string spell_line(int id, int spa) {
    std::vector<std::string> fl(237, "0");
    fl[0] = std::to_string(id);
    fl[1] = "Spell" + std::to_string(id);
    for (std::size_t k = 0; k < 12; ++k) {
        fl[86 + k] = "254";
        fl[70 + k] = "100";
    }
    fl[86] = std::to_string(spa);
    fl[20] = "1";
    fl[16] = "1";
    fl[83] = "1";
    std::string out;
    for (std::size_t i = 0; i < fl.size(); ++i) out += (i ? "^" : "") + fl[i];
    return out + "\n";
}

void give_buff(GameConnection& conn, std::uint32_t slot, std::uint32_t spell) {
    messages::BuffChange bp;
    bp.entity_id = conn.world().player_id;
    bp.slot = slot;
    bp.buff.effect_type = 2;
    bp.buff.level = 60;
    bp.buff.spell_id = spell;
    bp.buff.duration = 100;
    bp.buff.caster_id = conn.world().player_id;
    conn.receive_zone_message_for_test({messages::GameOpcode::Buff, bp});
}

game::CollisionMesh flat_scene() {
    std::vector<game::CollisionTriangle> t;
    glm::vec3 a{-500, -500, 0}, b{500, -500, 0}, c{500, 500, 0}, d{-500, 500, 0};
    auto tri = [&](glm::vec3 p, glm::vec3 q, glm::vec3 r) {
        if (glm::dot(glm::cross(p - q, r - q), glm::vec3(0, 0, 1)) < 0.0f) std::swap(q, r);
        t.push_back(game::CollisionTriangle::make(p, q, r, 0));
    };
    tri(a, b, c);
    tri(a, c, d);
    return game::CollisionMesh::from_triangles(std::move(t));
}

class FlatWorld final : public app::MoveWorld {
public:
    explicit FlatWorld(const game::CollisionMesh& mesh) : mesh_(mesh) {}
    bool collision_loaded() const override { return true; }
    std::optional<std::pair<glm::vec3, glm::vec3>> world_box() const override { return mesh_.bounds(); }
    Sweep sweep(const glm::vec3& from, const glm::vec3& to, float radius) const override {
        return run(from, to, radius, false);
    }
    Sweep world_sweep(const glm::vec3& from, const glm::vec3& to, float radius) const override {
        return run(from, to, radius, true);
    }
    bool in_world(const glm::vec3&) const override { return true; }
    std::optional<float> floor_below(const glm::vec3& p, float step_up) const override {
        return mesh_.floor_below(p, step_up);
    }
    float floor_sweep(const glm::vec3& from, const glm::vec3& to, float radius, std::uint32_t* = nullptr,
                      std::vector<FloorSwitchHit>* = nullptr) const override {
        return run(from, to, radius, true).out.position.z;
    }
    std::optional<float> ceiling_above(const glm::vec3& p) const override { return mesh_.ceiling_above(p); }

private:
    Sweep run(const glm::vec3& from, const glm::vec3& to, float radius, bool floor_only) const {
        game::SphereSweep s = game::SphereSweep::make(from, to, radius);
        s.floor_only = floor_only;
        Sweep out;
        if (s.valid()) mesh_.sweep_sphere(s);
        out.out = game::sweep_output(s);
        out.collided = s.collided;
        return out;
    }
    const game::CollisionMesh& mesh_;
};

// How far the local player goes in `ms` with forward held, from `now` on (advanced).
float run_forward(app::PlayerMovement& m, GameConnection& conn, const FlatWorld& world, std::uint32_t& now, int ms) {
    constexpr float kAvatarHeight = 3.75f;
    app::MovementKeys keys;
    keys.forward = true;
    glm::vec3 from = m.controller().body.feet, to = from;
    for (int t = 0; t < ms; t += 16) {
        now += 16;
        to = m.update(keys, 0.0f, 0.0f, 16.0f, now, conn, nullptr, kAvatarHeight, &world).feet;
    }
    return glm::distance(glm::vec2(from), glm::vec2(to));
}

} // namespace

TEST_CASE("Stop moving: another spawn that lies down, sits or loots loses its speed and turn rate") {
    test::Rig rig;
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    auto& w = conn.world_for_test();
    const game::Spawn* me = w.player();
    REQUIRE(me);
    for (std::uint32_t state : {kStandLying, kStandSitting, kStandLooting, kStandStanding, kStandCrouching}) {
        CAPTURE(state);
        game::Spawn s;
        s.id = 4001;
        s.name = "Runner";
        s.position = me->position + glm::vec3(20.0f, 0.0f, 0.0f);
        s.velocity = glm::vec3(0.5f, 0.0f, 0.0f);
        s.speed_run = 0.7f;
        s.speed_heading = 2.0f;
        s.update_time = 10.0;
        w.spawns[s.id] = s;
        conn.receive_zone_message_for_test(appearance(s.id, state));
        const game::Spawn& r = w.spawns.at(s.id);
        CHECK(r.stand_state == state);
        const bool stops = state == kStandLying || state == kStandSitting || state == kStandLooting;
        CHECK((r.speed_run == 0.0f) == stops);
        CHECK((r.speed_heading == 0.0f) == stops);

        // Stepped on between updates (AdvanceSpawn, under 12 s since the last one): only the velocity it had, which
        // friction takes away, against a run at its speed.
        const glm::vec3 start = r.position;
        for (int i = 0; i < 60; ++i) w.smooth(0.05f, 10.0 + 0.05 * (i + 1));
        const float moved = glm::distance(start, w.spawns.at(s.id).position);
        if (stops) CHECK(moved < 3.0f);
        else CHECK(moved > 20.0f);
    }
}

TEST_CASE("Stop moving: the local player's stops count a stun that starts and sitting, looting or lying down") {
    test::Rig rig;
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    const std::uint32_t speed = conn.speed_stops(), turn = conn.turn_stops();
    conn.receive_zone_message_for_test(stun(3000));
    CHECK(conn.speed_stops() == speed + 1);
    CHECK(conn.turn_stops() == turn); // StunMe leaves the turn rate
    // One that comes while a stun runs does not stop the player again (StunMe returns first).
    conn.receive_zone_message_for_test(stun(3000));
    CHECK(conn.speed_stops() == speed + 1);

    REQUIRE(conn.set_stand_state(kStandSitting));
    CHECK(conn.speed_stops() == speed + 2);
    CHECK(conn.turn_stops() == turn + 1);
    REQUIRE(conn.set_stand_state(kStandStanding));
    CHECK(conn.speed_stops() == speed + 2);
    // The server lying the player down (its own SpawnAppearance), then again in the same state: once.
    conn.receive_zone_message_for_test(appearance(conn.world().player_id, kStandLying));
    CHECK(conn.speed_stops() == speed + 3);
    CHECK(conn.turn_stops() == turn + 2);
    conn.receive_zone_message_for_test(appearance(conn.world().player_id, kStandLying));
    CHECK(conn.speed_stops() == speed + 3);
}

TEST_CASE("Stun: StunMe's rules for the local player") {
    test::Rig rig;
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    auto table = std::make_shared<game::SpellTable>();
    REQUIRE(table->parse(spell_line(501, 31)) == 1); // mesmerize
    conn.set_spell_table_for_test(table);

    // A stun runs its own time: one that comes meanwhile, or EQEmu's UnStun (a duration of 0), changes nothing.
    std::int64_t t = GameConnection::now_ms();
    conn.receive_zone_message_for_test(stun(3000));
    REQUIRE(conn.stunned());
    conn.receive_zone_message_for_test(stun(0));
    conn.receive_zone_message_for_test(stun(10000));
    conn.passage_of_time_for_test(t + 2000);
    CHECK(conn.stunned());
    conn.passage_of_time_for_test(GameConnection::now_ms() + 3000);
    CHECK_FALSE(conn.stunned());

    // A duration below 1000 ms, 0 too, is a 1000 ms stun for a player.
    t = GameConnection::now_ms();
    conn.receive_zone_message_for_test(stun(0));
    REQUIRE(conn.stunned());
    conn.passage_of_time_for_test(t + 500);
    CHECK(conn.stunned());
    conn.passage_of_time_for_test(GameConnection::now_ms() + 1000);
    CHECK_FALSE(conn.stunned());

    // The lean byte: the turn rate 32 (the movement code's), counted once a stun starts.
    const std::uint32_t leans = conn.stun_leans();
    conn.receive_zone_message_for_test(stun(2000, true));
    CHECK(conn.stun_leans() == leans + 1);
    conn.passage_of_time_for_test(GameConnection::now_ms() + 2000);
    REQUIRE_FALSE(conn.stunned());

    // Mesmerized when the timer passes: the stun goes on until the mez ends it.
    give_buff(conn, 0, 501);
    conn.receive_zone_message_for_test(stun(1000));
    conn.passage_of_time_for_test(GameConnection::now_ms() + 1000);
    CHECK(conn.stunned()); // the timer is gone: EndStun comes with the mez's removal (ProcessAffects)
}

TEST_CASE("Stun: stun immunity (SPA 40) keeps the player from being stunned") {
    test::Rig rig;
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    auto table = std::make_shared<game::SpellTable>();
    REQUIRE(table->parse(spell_line(500, 40)) == 1);
    conn.set_spell_table_for_test(table);
    give_buff(conn, 0, 500);
    REQUIRE(conn.spell_state().buffs[0].spell == 500);
    const std::uint32_t stops = conn.speed_stops();
    conn.receive_zone_message_for_test(stun(3000));
    CHECK_FALSE(conn.stunned());
    CHECK(conn.speed_stops() == stops);
}

TEST_CASE("Stop moving: the local player stops when stunned or lying down with forward held") {
    test::ScratchDir scratch("stop_moving");
    GameDir game(scratch.path());
    const game::CollisionMesh mesh = flat_scene();
    const FlatWorld world(mesh);
    for (bool feign : {false, true}) {
        CAPTURE(feign);
        GameConnection conn(game, {});
        conn.player_status_mut().stand_state = kStandStanding;
        // StunMe needs the player's spawn.
        game::Spawn me;
        me.id = 7;
        conn.world_for_test().spawns[me.id] = me;
        conn.world_for_test().player_id = me.id;
        app::PlayerMovement m;
        m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{3.75f, 1}));
        m.set_race(1);
        m.reset(glm::vec3(0.0f), 0.0f);
        std::uint32_t now = 10000;
        // Up to speed first.
        CHECK(run_forward(m, conn, world, now, 1000) > 10.0f);
        if (feign) conn.receive_zone_message_for_test(appearance(me.id, kStandLying));
        else conn.receive_zone_message_for_test(stun(5000));
        // Friction takes the velocity it had in a moment; the speed it ran at no longer pushes it.
        run_forward(m, conn, world, now, 500);
        CHECK(run_forward(m, conn, world, now, 1500) < 0.5f);
    }
}

TEST_CASE("Stun: a stun with the lean byte turns the player until it ends") {
    test::ScratchDir scratch("stun_lean");
    GameDir game(scratch.path());
    const game::CollisionMesh mesh = flat_scene();
    const FlatWorld world(mesh);
    GameConnection conn(game, {});
    conn.player_status_mut().stand_state = kStandStanding;
    game::Spawn me;
    me.id = 7;
    conn.world_for_test().spawns[me.id] = me;
    conn.world_for_test().player_id = me.id;
    app::PlayerMovement m;
    m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{3.75f, 1}));
    m.set_race(1);
    m.reset(glm::vec3(0.0f), 0.0f);
    std::uint32_t now = 10000;
    float yaw = 0.0f;
    auto frames = [&](int ms) {
        for (int t = 0; t < ms; t += 16) {
            now += 16;
            yaw = m.update(app::MovementKeys{}, yaw, 0.0f, 16.0f, now, conn, nullptr, 3.75f, &world).yaw;
        }
    };
    frames(200);
    CHECK(yaw == doctest::Approx(0.0f));
    conn.receive_zone_message_for_test(stun(5000, true));
    REQUIRE(conn.stunned());
    frames(500);
    // 32 heading units a tick (50 ms): ten ticks turn it 320 of 512.
    CHECK(std::fabs(yaw) > 1.0f);
}
