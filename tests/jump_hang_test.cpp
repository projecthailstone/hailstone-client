// A standing jump on flat ground (app/player_movement.cpp, the collision hold in the air): the player comes down
// whatever the frame rate. Above about 126 frames a second the original's still rule held a jump at its top.

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "app/player_movement.h"
#include "core/game_dir.h"
#include "game/collision.h"
#include "game/mechanics/collision_movement.h"
#include "net/connection/game_connection.h"
#include "scratch_dir.h"

using namespace hailstone;

namespace {

game::CollisionMesh flat_scene() {
    std::vector<game::CollisionTriangle> t;
    glm::vec3 a{-300, -300, 0}, b{300, -300, 0}, c{300, 300, 0}, d{-300, 300, 0};
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

} // namespace

TEST_CASE("Player movement: a standing jump comes down at any frame rate") {
    test::ScratchDir scratch("jump_hang");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    const game::CollisionMesh mesh = flat_scene();
    const FlatWorld world(mesh);
    conn.player_status_mut().endurance = 1000;
    conn.player_status_mut().max_endurance = 1000;
    constexpr float kAvatarHeight = 3.75f;
    // 20 to 500 frames a second; the jump animation over at the top (the renderer's answer) or not drawn at all.
    for (int frame_ms : {50, 16, 10, 7, 4, 2}) {
        for (bool drawn : {false, true}) {
            CAPTURE(frame_ms);
            CAPTURE(drawn);
            conn.player_status_mut().stand_state = net::kStandStanding;
            app::PlayerMovement m;
            m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{kAvatarHeight, 1}));
            m.set_race(1);
            m.reset(glm::vec3(0.0f), 0.0f);
            if (drawn) m.set_jump_animation(false);
            app::MovementKeys keys;
            std::uint32_t now = 10000, up = 0, down = 0;
            float top = 0.0f;
            for (int t = 0; t < 3000; t += frame_ms) {
                keys.jump = t >= 100 && t < 100 + frame_ms;
                now += static_cast<std::uint32_t>(frame_ms);
                auto f = m.update(keys, 0.0f, 0.0f, static_cast<float>(frame_ms), now, conn, nullptr, kAvatarHeight,
                                  &world);
                // The app's part: the standing jump's crouch is the player's stand state.
                if (f.stand_state) conn.player_status_mut().stand_state = static_cast<std::uint8_t>(*f.stand_state);
                if (f.feet.z > 0.01f && !up) up = now;
                if (up && !down && f.feet.z <= 0.01f) down = now;
                top = std::max(top, f.feet.z);
            }
            CHECK(top > 3.0f);
            REQUIRE(down != 0);
            // About half a second up (450 at 20 frames a second, 512 at 60); the original's per-frame friction
            // shortens it at high frame rates. Held at the top, it never came down above about 126 a second.
            CHECK(down - up < 600u);
        }
    }
}
