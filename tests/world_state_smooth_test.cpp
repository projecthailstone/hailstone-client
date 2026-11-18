// WorldState::smooth: the drawn place follows the spawn's and recovers from a value that is not a number.
#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <limits>

#include "core/worker_pool.h"
#include "game/world_state.h"

using namespace hailstone::game;

namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

Spawn& add(WorldState& w, std::uint32_t id, glm::vec3 at) {
    Spawn& s = w.spawns[id];
    s.id = id;
    s.race = 1;
    s.is_npc = true;
    s.position = at;
    return s;
}
} // namespace

TEST_CASE("smooth: a drawn place that is not a number snaps to the spawn's next good place") {
    WorldState w;
    Spawn& s = add(w, 5, {10.0f, 20.0f, 0.0f});
    w.smooth(0.016f, 1.0);
    REQUIRE(s.display_valid);
    s.display_position = glm::vec3(kNaN);
    s.display_heading = kNaN;
    w.smooth(0.016f, 1.0);
    CHECK(s.display_position == glm::vec3(10.0f, 20.0f, 0.0f));
    CHECK(std::isfinite(s.display_heading));
}

TEST_CASE("smooth: physics clocks advance only with completed fresh remote and boat steps") {
    WorldState w;
    w.player_id = 1;
    w.controlled_id = 2;
    auto& local = add(w, 1, {0, 0, 0});
    auto& controlled = add(w, 2, {0, 0, 0});
    auto& remote = add(w, 3, {0, 0, 0});
    auto& boat = add(w, 4, {0, 0, 0});
    boat.race = 73; // The same launch fixture as boats_test.cpp.
    auto& rider = add(w, 5, {0, 0, 0});
    rider.on_vehicle = 4;
    w.physics_steps = std::vector<float>{16, 17};
    w.physics_step_time_ms = 201;
    w.smooth(0.033f, 1.0);
    CHECK_FALSE(local.physics_step_ms);
    CHECK_FALSE(controlled.physics_step_ms);
    CHECK(remote.physics_step_ms == 201);
    CHECK(boat.physics_step_ms == 201);
    CHECK(rider.physics_step_ms == 201);

    w.physics_steps = std::vector<float>{};
    w.physics_step_time_ms = 218;
    w.smooth(0.017f, 1.017);
    CHECK(remote.physics_step_ms == 201);
    CHECK(boat.physics_step_ms == 201);
    CHECK(rider.physics_step_ms == 201);
    w.physics_steps = std::vector<float>{17};
    w.physics_step_time_ms = 235;
    w.smooth(0.017f, 20.0); // Their last packet is older than the native 12-second advance window.
    CHECK(remote.physics_step_ms == 201);
    CHECK(boat.physics_step_ms == 201);
    CHECK(rider.physics_step_ms == 201);
    remote.update_time = 20;
    w.physics_step_time_ms = 252;
    w.smooth(0.017f, 20.017);
    CHECK(remote.physics_step_ms == 252);
}

TEST_CASE("smooth: a spawn place that is not a number keeps the last drawn place") {
    WorldState w;
    Spawn& s = add(w, 5, {10.0f, 20.0f, 0.0f});
    w.smooth(0.016f, 1.0);
    s.position = {kNaN, 20.0f, 0.0f};
    w.smooth(0.016f, 1.0);
    CHECK(s.display_position == glm::vec3(10.0f, 20.0f, 0.0f));
    s.position = {12.0f, 20.0f, 0.0f}; // a good update: smoothing goes on from there
    w.smooth(0.016f, 1.0);
    CHECK(s.display_position.x > 10.0f);
    CHECK(s.display_position.x < 12.0f);
}

TEST_CASE("smooth: the local player is drawn where it is, not eased") {
    WorldState w;
    w.player_id = 7;
    Spawn& me = add(w, 7, {10.0f, 20.0f, 0.0f});
    me.is_npc = false;
    me.heading = 100.0f;
    Spawn& other = add(w, 5, {10.0f, 20.0f, 0.0f});
    w.smooth(0.016f, 1.0);
    me.position = other.position = {12.0f, 20.0f, 0.0f};
    me.heading = other.heading = 140.0f;
    w.smooth(0.016f, 1.0);
    CHECK(me.display_position == glm::vec3(12.0f, 20.0f, 0.0f));
    CHECK(me.display_heading == 140.0f);
    CHECK(other.display_position.x < 12.0f); // others still ease
}

TEST_CASE("smooth: set_local_drawn places the player and its mount, and smooth keeps it for that frame") {
    WorldState w;
    w.player_id = 7;
    w.controlled_id = 9;
    Spawn& me = add(w, 7, {12.0f, 20.0f, 0.0f});
    Spawn& mount = add(w, 9, {12.0f, 20.0f, 0.0f});
    w.smooth(0.016f, 1.0);
    // Between two steps: the camera's place is short of the last step's.
    w.set_local_drawn({11.5f, 20.0f, 0.0f}, 30.0f);
    CHECK(me.display_position == glm::vec3(11.5f, 20.0f, 0.0f));
    CHECK(mount.display_heading == 30.0f);
    w.smooth(0.016f, 1.0);
    CHECK(me.display_position == glm::vec3(11.5f, 20.0f, 0.0f));
    CHECK(mount.display_position == glm::vec3(11.5f, 20.0f, 0.0f));
    // A frame without the call (update_player did not run): drawn at the position.
    w.smooth(0.016f, 1.0);
    CHECK(me.display_position == glm::vec3(12.0f, 20.0f, 0.0f));
    // A place that is not a number is not taken.
    w.set_local_drawn({kNaN, 20.0f, 0.0f}, 30.0f);
    CHECK(me.display_position == glm::vec3(12.0f, 20.0f, 0.0f));
}

TEST_CASE("smooth: only spawns within remote_probe_range of the player have the contact probe") {
    WorldState w;
    w.player_id = 1;
    add(w, 1, {0.0f, 0.0f, 0.0f});
    Spawn& near = add(w, 5, {100.0f, 0.0f, 0.0f});
    Spawn& far = add(w, 6, {600.0f, 0.0f, 0.0f});
    near.speed_run = far.speed_run = 1.0f;
    far.colliding_type = 6; // a contact from when it was near
    std::vector<std::uint32_t> probed;
    WorldState::RemoteCollisionFn collision = [&](const Spawn& actor, const glm::vec3&, const glm::vec3&) {
        probed.push_back(actor.id);
        return 6;
    };
    w.smooth(0.016f, 1.0, {}, {}, {}, {}, collision);
    CHECK(probed == std::vector<std::uint32_t>{5});
    CHECK(near.colliding_type == 6);
    CHECK(far.colliding_type == 0); // moved without a probe: no contact
    // The range follows the setting (the client's actor clip); 0 is no limit.
    probed.clear();
    w.remote_probe_range = 50.0f;
    w.smooth(0.016f, 1.0, {}, {}, {}, {}, collision);
    CHECK(probed.empty());
    w.remote_probe_range = 0.0f;
    w.smooth(0.016f, 1.0, {}, {}, {}, {}, collision);
    CHECK(probed.size() == 2);
}

TEST_CASE("smooth: remote climbing contact follows movement steps and clears on the next free move") {
    WorldState w;
    Spawn& moving = add(w, 5, {1.0f, 2.0f, 3.0f});
    moving.speed_run = 1.0f;
    w.physics_steps = std::vector<float>{16.0f, 17.0f};
    int probes = 0;
    int result = 6;
    WorldState::RemoteCollisionFn collision = [&](const Spawn& actor, const glm::vec3& from, const glm::vec3& to) {
        CHECK(actor.id == 5);
        CHECK(to.x > from.x);
        ++probes;
        return result;
    };
    w.smooth(0.033f, 1.0, {}, {}, {}, {}, collision);
    CHECK(probes == 2);
    CHECK(moving.colliding_type == 6);

    result = 0;
    w.smooth(0.033f, 1.033, {}, {}, {}, {}, collision);
    CHECK(probes == 4);
    CHECK(moving.colliding_type == 0);

    moving.colliding_type = 6;
    moving.speed_run = 0.0f;
    moving.velocity = glm::vec3(0.0f);
    w.smooth(0.033f, 1.066, {}, {}, {}, {}, collision);
    CHECK(probes == 4);
    CHECK(moving.colliding_type == 6); // The standing-still shortcut retains contact.

    moving.speed_run = 1.0f;
    moving.properties.push_back(0x64); // NoCollide: no world probe.
    w.smooth(0.033f, 1.099, {}, {}, {}, {}, collision);
    CHECK(probes == 4);
    CHECK(moving.colliding_type == 0);
}

TEST_CASE("smooth: controlled players, mounted riders and stale spawns do not acquire remote contacts") {
    WorldState w;
    w.player_id = 1;
    w.controlled_id = 2;
    for (std::uint32_t id : {1u, 2u, 3u, 4u}) {
        auto& spawn = add(w, id, {1, 2, 3});
        spawn.speed_run = 1.0f;
        spawn.update_time = 1.0;
    }
    w.spawns.at(3).mount_id = 2;
    w.spawns.at(4).update_time = -20.0;
    int probes = 0;
    w.smooth(0.016f, 1.0, {}, {}, {}, {},
             [&](const Spawn&, const glm::vec3&, const glm::vec3&) { ++probes; return 6; });
    CHECK(probes == 0);
    for (std::uint32_t id : {1u, 2u, 3u, 4u}) CHECK(w.spawns.at(id).colliding_type == 0);
}

TEST_CASE("smooth: spawns stepped on the worker threads come out as they do stepped inline") {
    // A busy zone twice: smoothed inline (no pool), and with a pool of three workers registered (as the client does).
    auto make = [] {
        WorldState w;
        w.player_id = 1;
        w.ground_npcs = true;
        w.physics_steps = std::vector<float>{16.0f, 17.0f};
        add(w, 1, {0.0f, 0.0f, 0.0f});
        for (std::uint32_t id = 10; id < 410; ++id) {
            Spawn& s = add(w, id, {static_cast<float>(id % 40) * 7.0f, static_cast<float>(id / 40) * 7.0f,
                                   10.0f + static_cast<float>(id % 3)});
            s.speed_run = 1.0f + static_cast<float>(id % 5) * 0.25f;
            s.heading = static_cast<float>(id % 512);
            s.velocity = glm::vec3(0.5f, -0.25f, 0.0f);
            s.update_time = 1.0;
        }
        w.spawns.at(20).mount_id = 21; // a rider: drawn where its mount is, after the parallel pass
        return w;
    };
    // Functions of their arguments alone, as the client's are (they read the zone, which nothing changes meanwhile).
    WorldState::FloorFn floor = [](const glm::vec3& p) -> std::optional<float> {
        return std::sin(p.x * 0.1f) + std::cos(p.y * 0.1f);
    };
    WorldState::RegionFn region = [](const glm::vec3& p) -> std::uint8_t { return p.x > 200.0f ? 1 : 0; };
    std::atomic<int> probes{0};
    WorldState::RemoteCollisionFn collision = [&](const Spawn& s, const glm::vec3&, const glm::vec3& to) {
        probes.fetch_add(1);
        return s.id % 7 == 0 && to.x > 50.0f ? 6 : 0;
    };
    auto run = [&](WorldState& w) {
        for (int frame = 0; frame < 5; ++frame)
            w.smooth(0.033f, 1.0 + 0.033 * frame, floor, region, {}, {}, collision);
    };
    WorldState inline_world = make();
    run(inline_world);
    const int inline_probes = probes.exchange(0);
    WorldState pooled_world = make();
    {
        hailstone::core::WorkerPool pool(3);
        hailstone::core::set_workers(&pool);
        run(pooled_world);
        hailstone::core::set_workers(nullptr);
    }
    CHECK(inline_probes > 0);
    CHECK(probes.load() == inline_probes);
    REQUIRE(pooled_world.spawns.size() == inline_world.spawns.size());
    for (const auto& [id, s] : inline_world.spawns) {
        CAPTURE(id);
        const Spawn& p = pooled_world.spawns.at(id);
        CHECK(p.position == s.position);
        CHECK(p.velocity == s.velocity);
        CHECK(p.heading == s.heading);
        CHECK(p.display_position == s.display_position);
        CHECK(p.display_heading == s.display_heading);
        CHECK(p.colliding_type == s.colliding_type);
        CHECK(p.body_wet == s.body_wet);
        CHECK(p.feet_wet == s.feet_wet);
        CHECK(p.ground_drop == s.ground_drop);
    }
    CHECK(pooled_world.spawns.at(20).display_position == pooled_world.spawns.at(21).display_position);
}

TEST_CASE("smooth: a spawn that stays in one place for a second is stalled, whatever its speeds say") {
    WorldState w;
    w.player_id = 1;
    Spawn& local = add(w, 1, {0, 0, 0});
    Spawn& s = add(w, 5, {0, 0, 0});
    s.update_time = 0.0;
    s.speed_run = 0.5f; // a walk: stepped (and moving) for 12 s after the update
    s.velocity = {0.3f, 0.0f, 0.0f};
    auto at = [](int frame) { return frame * 0.05; };
    int frame = 0;
    for (; at(frame) <= 12.0; ++frame) {
        w.smooth(0.05f, at(frame));
        REQUIRE_FALSE(s.stalled);
    }
    CHECK(glm::length(s.position) > 10.0f);
    // No longer stepped: it stays put with its speeds as they were, and stands once a second has gone by.
    for (; at(frame) < 12.9; ++frame) w.smooth(0.05f, at(frame));
    CHECK_FALSE(s.stalled);
    for (; at(frame) < 13.2; ++frame) w.smooth(0.05f, at(frame));
    CHECK(s.stalled);
    CHECK(s.speed_run == 0.5f);
    CHECK(s.velocity != glm::vec3(0.0f));
    CHECK_FALSE(local.stalled); // the local player never is
    // An update that moves it walks it again at once.
    s.position += glm::vec3(5.0f, 0.0f, 0.0f);
    s.update_time = at(frame);
    w.smooth(0.05f, at(frame));
    CHECK_FALSE(s.stalled);
}
