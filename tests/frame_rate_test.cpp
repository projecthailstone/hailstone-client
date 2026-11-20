// The game clock (core/step_clock.h) with the player's movement (app/player_movement.cpp): a fall, a running jump and
// a held turn come out the same at any drawn frame rate, as the original's rules gave at 60 frames a second. Stepped
// once a drawn frame instead (game.fixed_step false), the original's per-frame friction makes them differ.
//
// What this covers: the clock and the movement rules on it. The app's loop (app/client_app_player.cpp) runs the same
// steps; it is not driven here.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "app/player_movement.h"
#include "core/game_dir.h"
#include "core/step_clock.h"
#include "game/collision.h"
#include "game/spells.h"
#include "game/world_state.h"
#include "game/mechanics/collision_movement.h"
#include "net/connection/game_connection.h"
#include "scratch_dir.h"

using namespace hailstone;

namespace {

game::CollisionMesh flat_floor() {
    std::vector<game::CollisionTriangle> t;
    glm::vec3 a{-3000, -3000, 0}, b{3000, -3000, 0}, c{3000, 3000, 0}, d{-3000, 3000, 0};
    auto tri = [&](glm::vec3 p, glm::vec3 q, glm::vec3 r) {
        if (glm::dot(glm::cross(p - q, r - q), glm::vec3(0, 0, 1)) < 0.0f) std::swap(q, r);
        t.push_back(game::CollisionTriangle::make(p, q, r, 0));
    };
    tri(a, b, c);
    tri(a, c, d);
    return game::CollisionMesh::from_triangles(std::move(t));
}

class FloorWorld final : public app::MoveWorld {
public:
    explicit FloorWorld(const game::CollisionMesh& mesh) : mesh_(mesh) {}
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

constexpr float kAvatarHeight = 3.75f;

struct Outcome {
    bool landed = false;
    float landing_speed = 0.0f;
    std::optional<std::int32_t> fall_damage;
    std::uint64_t landed_ms = 0; // since the start
    glm::vec3 feet{0.0f};
    float top = 0.0f;
    float yaw = 0.0f;
};

enum class Scenario { Fall, RunningJump, Turn };

// Drawn frames `frame_us` apart for 4 s; on the game clock (fixed) or once a frame. The keys follow the game's time:
// what is held at a step's time.
Outcome play(Scenario what, std::uint64_t frame_us, bool fixed) {
    test::ScratchDir scratch("frame_rate");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    conn.player_status_mut().endurance = 1000;
    conn.player_status_mut().max_endurance = 1000;
    conn.player_status_mut().stand_state = net::kStandStanding;
    const game::CollisionMesh mesh = flat_floor();
    const FloorWorld world(mesh);
    app::PlayerMovement m;
    m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{kAvatarHeight, 1}));
    m.set_race(1);
    m.reset(glm::vec3(0.0f, 0.0f, what == Scenario::Fall ? 120.0f : 0.0f), 0.0f);
    core::StepClock clock;
    clock.set_fixed(fixed);
    Outcome out;
    float yaw = 0.0f;
    const std::uint64_t start_ms = 10'000;
    bool jumped = false;
    for (std::uint64_t t = start_ms * 1000; t <= (start_ms + 4000) * 1000; t += frame_us) {
        for (const auto& st : clock.advance(t)) {
            const std::uint64_t at = st.ms - start_ms;
            app::MovementKeys keys;
            if (what == Scenario::RunningJump) {
                keys.forward = true;
                keys.jump = !jumped && at >= 1000;
            }
            if (what == Scenario::Turn) keys.turn_right = at < 500;
            auto f = m.update(keys, yaw, 0.0f, static_cast<float>(frame_us) / 1000.0f,
                              static_cast<std::uint32_t>(st.ms), conn, nullptr, kAvatarHeight, &world, nullptr,
                              st.elapsed_ms);
            if (f.jumped) jumped = true;
            if (f.stand_state) conn.player_status_mut().stand_state = static_cast<std::uint8_t>(*f.stand_state);
            yaw = f.yaw;
            out.top = std::max(out.top, f.feet.z);
            if (f.landed && !out.landed && (what != Scenario::RunningJump || jumped)) {
                out.landed = true;
                out.landing_speed = f.landing_speed;
                out.fall_damage = f.fall_damage;
                out.landed_ms = at;
                if (what == Scenario::RunningJump) out.feet = f.feet; // where the jump came down
            }
        }
    }
    if (what != Scenario::RunningJump) out.feet = glm::vec3(0.0f);
    out.yaw = yaw;
    return out;
}

// 15, 60, 144, 240 and 1000 frames a second, and an uneven 7 ms / 29 ms one is below.
constexpr std::uint64_t kRates[] = {66'667, 16'667, 6'944, 4'167, 1'000};

} // namespace

TEST_CASE("Frame rate: on the game clock a fall lands the same at any drawn frame rate") {
    const Outcome base = play(Scenario::Fall, 16'667, true);
    REQUIRE(base.landed);
    CHECK(base.landing_speed < -2.5f); // a real fall: the landing sound's speed and more
    for (std::uint64_t f : kRates) {
        CAPTURE(f);
        const Outcome o = play(Scenario::Fall, f, true);
        CHECK(o.landed);
        CHECK(o.landing_speed == base.landing_speed);
        CHECK(o.fall_damage == base.fall_damage);
        // The step it lands on is the same; the drawn frame that shows it is at most one frame later.
        CHECK(o.landed_ms == base.landed_ms);
    }
    // Once a drawn frame (the original's code as written), the air's friction per frame makes a fall at 1000 frames
    // a second much slower: it does not land as hard.
    const Outcome per_frame = play(Scenario::Fall, 1'000, false);
    CHECK(per_frame.landing_speed > base.landing_speed);
}

TEST_CASE("Frame rate: on the game clock a running jump goes as far and as high at any drawn frame rate") {
    const Outcome base = play(Scenario::RunningJump, 16'667, true);
    REQUIRE(base.landed);
    CHECK(base.top > 3.0f);
    CHECK(glm::length(base.feet) > 10.0f); // it ran on and came down ahead
    for (std::uint64_t f : kRates) {
        CAPTURE(f);
        const Outcome o = play(Scenario::RunningJump, f, true);
        CHECK(o.landed);
        CHECK(o.top == base.top);
        CHECK(o.feet == base.feet);
    }
    const Outcome per_frame = play(Scenario::RunningJump, 1'000, false);
    CHECK(per_frame.top != base.top);
}

TEST_CASE("Frame rate: on the game clock a held turn turns as far at any drawn frame rate") {
    const Outcome base = play(Scenario::Turn, 16'667, true);
    CHECK(base.yaw != 0.0f);
    for (std::uint64_t f : kRates) {
        CAPTURE(f);
        CHECK(play(Scenario::Turn, f, true).yaw == base.yaw);
    }
}

namespace {

// An NPC running from a standstill with deltas but no speed (it coasts on the server's velocity), then one running
// along its heading: where it is after 2 s of drawn frames `frame_us` apart, its physics on the game clock (fixed)
// or once a frame.
glm::vec3 run_npc(std::uint64_t frame_us, bool fixed, bool coasting) {
    game::WorldState w;
    game::Spawn& s = w.spawns[7];
    s.id = 7;
    s.is_npc = true;
    s.position = glm::vec3(0.0f);
    s.heading = 0.0f;
    if (coasting) s.velocity = glm::vec3(1.5f, 0.0f, 0.0f); // units a tick, as an update's deltas
    else s.speed_run = 0.7f;
    s.update_time = 10.0;
    core::StepClock clock;
    clock.set_fixed(fixed);
    std::uint64_t last = 10'000'000;
    // The last drawn frame at 12 s exactly, so every rate has run the same steps.
    for (std::uint64_t t = 10'000'000; t <= 12'000'000; t = t + frame_us > 12'000'000 && t < 12'000'000 ? 12'000'000 : t + frame_us) {
        auto steps = clock.advance(t);
        if (fixed) {
            w.physics_steps.emplace();
            for (const auto& st : steps) w.physics_steps->push_back(static_cast<float>(st.elapsed_ms));
        }
        const float dt = static_cast<float>(t - last) / 1e6f;
        last = t;
        w.smooth(dt, static_cast<double>(t) / 1e6);
    }
    return s.position;
}

} // namespace

TEST_CASE("Frame rate: on the game clock another spawn moves as far at any drawn frame rate") {
    for (bool coasting : {false, true}) {
        CAPTURE(coasting);
        const glm::vec3 base = run_npc(16'667, true, coasting);
        CHECK(base.x > 0.1f);
        for (std::uint64_t f : kRates) {
            CAPTURE(f);
            CHECK(run_npc(f, true, coasting) == base);
        }
    }
    // Once a frame, the friction per frame lets a coasting spawn's deltas carry it much farther at 15 frames a second
    // than at 240 (the original's code as written).
    CHECK(run_npc(66'667, false, true).x > run_npc(4'167, false, true).x * 1.5f);
}

TEST_CASE("Frame rate: on the game clock scribing a spell takes as long at any drawn frame rate") {
    // 3 of 92 ticks a frame: 31 frames, about half a second at 60 frames a second.
    auto scribe = [](std::uint64_t frame_us, bool fixed) {
        core::StepClock clock;
        clock.set_fixed(fixed);
        game::Scribing sc;
        std::uint32_t last = 0;
        for (std::uint64_t t = 5'000'000;; t += frame_us) {
            if (fixed) {
                for (const auto& st : clock.advance(t))
                    if (game::scribe_countdown(sc, last, static_cast<std::uint32_t>(st.ms))) return st.ms - 5000;
            } else if (game::scribe_countdown(sc, last, static_cast<std::uint32_t>(t / 1000))) {
                return t / 1000 - 5000;
            }
        }
    };
    const std::uint64_t base = scribe(16'667, true);
    CHECK(base > 450);
    CHECK(base < 550);
    for (std::uint64_t f : kRates) {
        CAPTURE(f);
        CHECK(scribe(f, true) == base);
    }
    CHECK(scribe(4'167, false) < base / 2); // once a frame at 240: four times as fast
}
