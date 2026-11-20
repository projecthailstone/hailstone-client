// A step a little taller than AvatarHeight: the movement sphere rides over its lip, and the floor test has to find
// its top so the player lands on it (app/player_movement.cpp, PlayerMovement::floor_probe_lift). The Bazaar arena's
// 4-unit tiers left a 3.75 player hanging with its feet inside the tier, unable to move.

#include <doctest/doctest.h>

#include <optional>
#include <vector>

#include "app/player_movement.h"
#include "core/game_dir.h"
#include "game/collision.h"
#include "game/mechanics/collision_movement.h"
#include "net/connection/game_connection.h"
#include "scratch_dir.h"

using namespace hailstone;

namespace {

// The triangle with its record normal (game::CollisionTriangle::make) facing `out`.
game::CollisionTriangle facing(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec3& out) {
    if (glm::dot(glm::cross(a - b, c - b), out) < 0.0f) std::swap(b, c);
    return game::CollisionTriangle::make(a, b, c, 0);
}

void quad(std::vector<game::CollisionTriangle>& t, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          const glm::vec3& out) {
    t.push_back(facing(a, b, c, out));
    t.push_back(facing(a, c, d, out));
}

// Ground at z 0 for x < 10; a solid block 4 high from x 10 to 200, with nothing inside it (as a zone's tier is drawn:
// its top and its faces, no floor under the top).
game::CollisionMesh step_scene() {
    std::vector<game::CollisionTriangle> t;
    const glm::vec3 up(0, 0, 1);
    quad(t, {-30, -300, 0}, {10, -300, 0}, {10, 300, 0}, {-30, 300, 0}, up);
    quad(t, {10, -300, 4}, {200, -300, 4}, {200, 300, 4}, {10, 300, 4}, up);
    quad(t, {10, -300, 0}, {10, 300, 0}, {10, 300, 4}, {10, -300, 4}, glm::vec3(-1, 0, 0));
    return game::CollisionMesh::from_triangles(std::move(t));
}

class MeshWorld final : public app::MoveWorld {
public:
    explicit MeshWorld(const game::CollisionMesh& mesh) : mesh_(mesh) {}
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
        if (s.valid()) {
            const auto triangle = mesh_.sweep_sphere_hit(s);
            out.ladder_hit = triangle >= 0 &&
                (mesh_.triangles()[static_cast<std::size_t>(triangle)].flags & game::kTriangleLadder) != 0;
        }
        out.out = game::sweep_output(s);
        out.collided = s.collided;
        return out;
    }
    const game::CollisionMesh& mesh_;
};

} // namespace

TEST_CASE("Player movement: a step the sphere rides over is landed on, not hung from") {
    test::ScratchDir scratch("step_landing");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    const game::CollisionMesh mesh = step_scene();
    const MeshWorld world(mesh);
    // A human-sized player (an Iksar at size 6): AvatarHeight 3.75, the stand-in radius 1.78, the sphere's centre 1.9
    // above the spawn's z, so its bottom is 0.12 above it, and 0.13 under the step's top while walking on the ground.
    constexpr float kAvatarHeight = 3.75f;
    // Head-on and at angles to the riser (the arena's tiers are met at about 63 degrees from their face's normal).
    for (float degrees : {0.0f, 30.0f, 50.0f, 63.4f, 75.0f}) {
        CAPTURE(degrees);
        const float yaw = glm::radians(degrees);
        app::PlayerMovement m;
        m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{kAvatarHeight, 128}));
        m.set_race(128);
        m.reset(glm::vec3(0.0f, 0.0f, 0.0f), yaw);
        app::MovementKeys keys;
        std::uint32_t now = 10000;
        app::PlayerMovement::Frame frame;
        for (int i = 0; i < 180; ++i) { // 3 s at 60 frames a second
            keys.forward = i >= 3;
            now += 16;
            frame = m.update(keys, yaw, 0.0f, 16.0f, now, conn, nullptr, kAvatarHeight, &world);
        }
        CAPTURE(frame.feet.x);
        CAPTURE(frame.feet.z);
        // On the step's top, past its edge, with the floor under it found.
        CHECK(frame.feet.x > 15.0f);
        CHECK(frame.feet.z == doctest::Approx(4.0f).epsilon(0.01));
        REQUIRE(m.floor_height());
        CHECK(*m.floor_height() == doctest::Approx(4.0f).epsilon(0.01));
    }
}

TEST_CASE("Remote movement: a real ladder sweep supplies the climbing contact state") {
    std::vector<game::CollisionTriangle> triangles;
    quad(triangles, {10, -10, 0}, {10, 10, 0}, {10, 10, 20}, {10, -10, 20}, {-1, 0, 0});
    for (auto& triangle : triangles) triangle.flags |= game::kTriangleObject | game::kTriangleLadder;
    const auto mesh = game::CollisionMesh::from_triangles(std::move(triangles));
    const MeshWorld world(mesh);
    game::Spawn spawn;
    spawn.race = 1;
    spawn.heights.avatar_height = 3.75f;
    // The sphere centre is lifted from the spawn z, as with the local movement's actual sweep.
    CHECK(app::remote_collision_type(spawn, {5, 0, 4}, {12, 0, 4}, world) == 6);
    CHECK(app::remote_collision_type(spawn, {5, 30, 4}, {12, 30, 4}, world) == 0);
    CHECK(app::remote_collision_type(spawn, {12, 0, 4}, {15, 0, 4}, world) == 0); // leaving the back face
}
