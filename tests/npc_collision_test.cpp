// NPC collision (game/npc_collision.h): which spawns stop the player, the bodies' walls, the sweep against them, the
// player walking into an NPC (app/player_movement.cpp), and the server's switch (hailstone.cfg [movement]).

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "app/player_movement.h"
#include "core/game_dir.h"
#include "game/npc_collision.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "plugin/policy.h"
#include "scratch_dir.h"

using namespace hailstone;

namespace {

constexpr float kAvatarHeight = 3.75f; // a human's
constexpr float kPlayerRadius = 1.78f; // the player's stand-in sweep radius for a human

game::Spawn npc(std::uint32_t id) {
    game::Spawn s;
    s.id = id;
    s.is_npc = true;
    s.race = 1;
    s.heights.avatar_height = kAvatarHeight;
    return s;
}

// A flat floor at z 0 and the NPC bodies, nothing else; the movement sweep meets only the bodies.
class Field final : public app::MoveWorld {
public:
    explicit Field(std::vector<game::NpcBody> bodies) : bodies_(std::move(bodies)) {}
    bool collision_loaded() const override { return true; }
    std::optional<std::pair<glm::vec3, glm::vec3>> world_box() const override { return std::nullopt; }
    Sweep sweep(const glm::vec3& from, const glm::vec3& to, float r) const override {
        game::SphereSweep s = game::SphereSweep::make(from, to, r);
        Sweep out;
        if (s.valid()) game::sweep_npcs(s, bodies_);
        out.out = game::sweep_output(s);
        out.collided = s.collided;
        return out;
    }
    Sweep world_sweep(const glm::vec3&, const glm::vec3& to, float) const override {
        Sweep s;
        s.out.position = to;
        return s;
    }
    bool in_world(const glm::vec3&) const override { return true; }
    std::optional<float> floor_below(const glm::vec3& p, float step_up) const override {
        if (p.z + step_up >= 0.0f) return 0.0f;
        return std::nullopt;
    }
    float floor_sweep(const glm::vec3& from, const glm::vec3& to, float r, std::uint32_t* = nullptr,
                      std::vector<FloorSwitchHit>* = nullptr) const override {
        if (from.z - r >= 0.0f && to.z - r <= 0.0f) return r;
        return to.z;
    }
    std::optional<float> ceiling_above(const glm::vec3&) const override { return std::nullopt; }

private:
    std::vector<game::NpcBody> bodies_;
};

} // namespace

TEST_CASE("NPC collision: only live NPCs that are no one's pet (swarm pets included), mercenary or mount block") {
    game::Spawn s = npc(10);
    CHECK(game::npc_blocks_player(s, 1));
    CHECK_FALSE(game::npc_blocks_player(s, 10)); // the controlled spawn never blocks itself

    game::Spawn player = s;
    player.is_npc = false;
    CHECK_FALSE(game::npc_blocks_player(player, 1));

    game::Spawn pet = s;
    pet.pet_owner_id = 2;
    CHECK_FALSE(game::npc_blocks_player(pet, 1));

    // Swarm pets: EQEmu sends no petOwnerId, only targetable_with_hotkey off and "<owner>'s Pet".
    game::Spawn swarm = s;
    swarm.cyclable = false;
    CHECK(game::is_swarm_pet(swarm));
    CHECK_FALSE(game::npc_blocks_player(swarm, 1));
    game::Spawn named = s;
    named.last_name = "Soandso's Pet";
    CHECK(game::is_swarm_pet(named));
    CHECK_FALSE(game::npc_blocks_player(named, 1));
    game::Spawn merchant = s;
    merchant.last_name = "Merchant";
    CHECK_FALSE(game::is_swarm_pet(merchant));
    CHECK(game::npc_blocks_player(merchant, 1));
    merchant.last_name = "'s Pet"; // no owner name before it
    CHECK(game::npc_blocks_player(merchant, 1));

    game::Spawn merc = s;
    merc.mercenary = true;
    CHECK_FALSE(game::npc_blocks_player(merc, 1));

    game::Spawn corpse = s;
    corpse.is_corpse = true;
    CHECK_FALSE(game::npc_blocks_player(corpse, 1));

    game::Spawn mount = s;
    mount.rider_id = 2;
    CHECK_FALSE(game::npc_blocks_player(mount, 1));

    game::Spawn boat = s;
    boat.race = 141; // BOAT: a vehicle race, whose hull collides on its own
    CHECK_FALSE(game::npc_blocks_player(boat, 1));
}

TEST_CASE("NPC collision: the body stands on the NPC's feet and its walls face outward") {
    const game::NpcBody b = game::npc_body(npc(10), glm::vec3(5.0f, 6.0f, 7.0f + kAvatarHeight));
    CHECK(b.feet.x == doctest::Approx(5.0f));
    CHECK(b.feet.z == doctest::Approx(7.0f));
    CHECK(b.radius == doctest::Approx(kAvatarHeight * 0.5f * 0.35f).epsilon(0.001)); // the original's NPC volume
    CHECK(b.height == doctest::Approx(game::kNpcMinWallHeight));
    const auto walls = game::npc_walls(b);
    REQUIRE(walls.size() == 2 * game::kNpcWallSides);
    for (const auto& t : walls) {
        const glm::vec3 mid = (t.v0 + t.v1 + t.v2) / 3.0f;
        CHECK(glm::dot(t.normal, mid - b.feet) > 0.0f);
        CHECK(t.normal.z == doctest::Approx(0.0f));
    }
    // A big NPC's walls are as high as twice its AvatarHeight.
    game::Spawn giant = npc(11);
    giant.heights.avatar_height = 20.0f;
    CHECK(game::npc_body(giant, glm::vec3(0.0f)).height == doctest::Approx(40.0f));
}

TEST_CASE("NPC collision: the sweep stops at a body and slides along it, and leaves one it starts inside") {
    const game::NpcBody b = game::npc_body(npc(10), glm::vec3(10.0f, 0.0f, kAvatarHeight));
    const std::vector<game::NpcBody> bodies{b};
    const float centre_z = kAvatarHeight;

    // Straight at it: stopped short, a body's and the sweep's radius from its axis.
    game::SphereSweep s = game::SphereSweep::make(glm::vec3(0, 0, centre_z), glm::vec3(20, 0, centre_z),
                                                  kPlayerRadius);
    CHECK(game::sweep_npcs(s, bodies) == 10);
    const game::SweepOutput out = game::sweep_output(s);
    CHECK(out.position.x == doctest::Approx(10.0f - b.radius - kPlayerRadius).epsilon(0.02));

    // At an angle: stopped, with a slide along the wall.
    s = game::SphereSweep::make(glm::vec3(0, 0.5f, centre_z), glm::vec3(20, 2.5f, centre_z), kPlayerRadius);
    CHECK(game::sweep_npcs(s, bodies) == 10);
    CHECK(game::sweep_output(s).skitter_dist > 0.0f);

    // Passing by: not stopped.
    s = game::SphereSweep::make(glm::vec3(0, 6, centre_z), glm::vec3(20, 6, centre_z), kPlayerRadius);
    CHECK(game::sweep_npcs(s, bodies) == 0);
    CHECK_FALSE(s.collided);

    // Above its walls (on a ledge over it): not stopped.
    s = game::SphereSweep::make(glm::vec3(0, 0, 20), glm::vec3(20, 0, 20), kPlayerRadius);
    CHECK(game::sweep_npcs(s, bodies) == 0);

    // The NPC walked onto the player: the player walks out whichever way, through it too.
    for (const glm::vec3 to : {glm::vec3(10, 10, centre_z), glm::vec3(10, -10, centre_z), glm::vec3(20, 0, centre_z)}) {
        s = game::SphereSweep::make(glm::vec3(9.5f, 0, centre_z), to, kPlayerRadius);
        CHECK(game::sweep_npcs(s, bodies) == 0);
        CHECK_FALSE(s.collided);
    }

    // Resting against it where the last sweep stopped: still held.
    s = game::SphereSweep::make(out.position, glm::vec3(20, 0, centre_z), kPlayerRadius);
    CHECK(game::sweep_npcs(s, bodies) == 10);
    CHECK(game::sweep_output(s).position.x < out.position.x + 0.1f);
}

TEST_CASE("NPC collision: the player walking into an NPC stops at it") {
    test::ScratchDir scratch("npc_collision");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    auto walk = [&](const app::MoveWorld& world) {
        app::PlayerMovement m;
        m.reset(glm::vec3(0.0f), 0.0f);
        app::MovementKeys keys;
        std::uint32_t now = 5000;
        auto frame = m.update(keys, 0.0f, 0.0f, 100.0f, now, conn, nullptr, kAvatarHeight, &world);
        keys.forward = true;
        for (int i = 0; i < 100; ++i) {
            now += 50;
            frame = m.update(keys, 0.0f, 0.0f, 50.0f, now, conn, nullptr, kAvatarHeight, &world);
        }
        return frame.feet;
    };
    // Which way forward is at yaw 0, and that the walk goes past where the NPC will stand.
    const glm::vec3 free_end = walk(Field({}));
    const float free_run = glm::length(glm::vec2(free_end));
    REQUIRE(free_run > 30.0f);
    const glm::vec2 ahead = glm::vec2(free_end) / free_run;

    const glm::vec2 at = ahead * 15.0f;
    const game::NpcBody b = game::npc_body(npc(10), glm::vec3(at, kAvatarHeight));
    const glm::vec3 held = walk(Field({b}));
    const float run = glm::dot(glm::vec2(held), ahead);
    CHECK(run < 15.0f - b.radius);
    CHECK(run > 15.0f - b.radius - kPlayerRadius - 1.0f);
    CHECK(held.z == doctest::Approx(0.0f).epsilon(0.01)); // on the floor, not on top of the NPC
}

TEST_CASE("NPC collision: hailstone.cfg [movement] npc_collision") {
    CHECK(plugin::Rules{}.npc_collision);                  // no file: on
    CHECK(plugin::parse_server_cfg("").npc_collision);     // a file that does not say: on
    std::vector<std::string> warnings;
    CHECK_FALSE(plugin::parse_server_cfg("[movement]\nnpc_collision = off\n", &warnings).npc_collision);
    CHECK(warnings.empty());
    CHECK(plugin::parse_server_cfg("[Movement]\nnpc_collision = on ; comment\n").npc_collision);
    warnings.clear();
    CHECK(plugin::parse_server_cfg("[movement]\nnpc_collision = maybe\n", &warnings).npc_collision);
    CHECK(warnings.size() == 1);
}
