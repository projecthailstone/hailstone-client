// game/boats.h and the boat parts of app/player_movement.cpp: a boat's drawn pose between EQEmu's updates, the frame a
// rider's position goes out in, the local player carried on a moving and turning deck (and walking on it, boarding
// from a dock and stepping off), and other spawns riding a deck between their updates.
#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <vector>

#include "app/player_movement.h"
#include "core/game_dir.h"
#include "game/boats.h"
#include "game/coords.h"
#include "game/mechanics/vehicle.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "scratch_dir.h"

using namespace hailstone;

namespace {

constexpr std::uint32_t kBoat = 7;
constexpr float kAvatarHeight = 3.75f;

game::Spawn& add_boat(game::WorldState& w, glm::vec3 at, float heading) {
    game::Spawn& s = w.spawns[kBoat];
    s.id = kBoat;
    s.name = "Captains_Skiff00";
    s.race = 73; // LAUNCH: a vehicle race
    s.is_npc = true;
    s.position = at;
    s.heading = heading;
    return s;
}

// A deck 20 x 12 (the boat's frame, centred on it) 5 above the boat's z, a dock at deck height for x < `dock_edge`
// (the world's), the sea floor far below. The deck reports the boat as the floor test's vehicle.
class Harbour final : public app::MoveWorld {
public:
    Harbour(const game::WorldState& w, float dock_edge) : world_(w), dock_edge_(dock_edge) {}
    bool collision_loaded() const override { return true; }
    std::optional<std::pair<glm::vec3, glm::vec3>> world_box() const override { return std::nullopt; }
    Sweep sweep(const glm::vec3&, const glm::vec3& to, float) const override {
        Sweep s;
        s.out.position = to;
        return s;
    }
    Sweep world_sweep(const glm::vec3& from, const glm::vec3& to, float r) const override { return sweep(from, to, r); }
    bool in_world(const glm::vec3&) const override { return true; }
    std::optional<float> floor_below(const glm::vec3&, float) const override { return kSeaFloor; }
    float floor_sweep(const glm::vec3& from, const glm::vec3& to, float r, std::uint32_t* vehicle = nullptr,
                      std::vector<FloorSwitchHit>* = nullptr) const override {
        const game::BoatPose pose = game::drawn_pose(world_.spawns.at(kBoat));
        const float deck = pose.position.z + 5.0f;
        const glm::vec3 local = game::boat_to_local(pose, from);
        if (std::fabs(local.x) <= 10.0f && std::fabs(local.y) <= 6.0f && from.z - r >= deck && to.z - r <= deck) {
            if (vehicle) *vehicle = kBoat;
            return deck + r;
        }
        if (from.x < dock_edge_ && from.z - r >= deck && to.z - r <= deck) return deck + r;
        if (from.z - r >= kSeaFloor && to.z - r <= kSeaFloor) return kSeaFloor + r;
        return to.z;
    }
    std::optional<float> ceiling_above(const glm::vec3&) const override { return std::nullopt; }
    static constexpr float kSeaFloor = -100.0f;

private:
    const game::WorldState& world_;
    float dock_edge_;
};

float wrap_diff(float a, float b) {
    float d = std::fmod(a - b + 768.0f, 512.0f) - 256.0f;
    return d;
}

} // namespace

TEST_CASE("Boats: world and the boat's frame") {
    const game::BoatPose pose{{100.0f, -40.0f, -12.0f}, 128.0f}; // a quarter turn: x along world +y
    const glm::vec3 p(103.0f, -30.0f, -7.0f);
    const glm::vec3 local = game::boat_to_local(pose, p);
    CHECK(local.x == doctest::Approx(10.0f).epsilon(0.001));
    CHECK(local.y == doctest::Approx(-3.0f).epsilon(0.001));
    CHECK(local.z == doctest::Approx(5.0f));
    const glm::vec3 back = game::boat_to_world(pose, local);
    CHECK(back.x == doctest::Approx(p.x).epsilon(0.0001));
    CHECK(back.y == doctest::Approx(p.y).epsilon(0.0001));
}

TEST_CASE("Boats: a rider's position, sent in the drawn boat's frame, is where EQEmu puts it back") {
    // GameConnection::send_position: Vehicle_ToLocal on the drawn pose, then world_to_server. EQEmu's
    // Handle_OP_ClientUpdate: theta = boat heading in degrees, x = bx + (cx cos + cy sin), y = by + (-cx sin + cy cos),
    // z = bz + cz, heading + boat heading.
    for (float heading : {0.0f, 37.5f, 128.0f, 300.25f, 483.2f}) {
        const glm::vec3 boat(879.0f, 3116.0f, -12.0f), rider(884.5f, 3109.25f, 8.1f);
        game::mechanics::VehicleBody b{rider.x, rider.y, rider.z, 0, 0, 0, 100.0f, 0};
        game::mechanics::vehicle_to_local({boat.x, boat.y, boat.z, heading, 0.0f}, b);
        const glm::vec3 sent = game::world_to_server({b.x, b.y, b.z});
        const glm::vec3 boat_server = game::world_to_server(boat);
        const double theta = std::fmod(heading * 360.0 / 512.0, 360.0) * 3.14159265358979 / 180.0;
        const double x = boat_server.x + (sent.x * std::cos(theta) - -sent.y * std::sin(theta));
        const double y = boat_server.y + (-sent.x * std::sin(theta) + sent.y * std::cos(theta));
        const glm::vec3 want = game::world_to_server(rider);
        CHECK(x == doctest::Approx(want.x).epsilon(0.0001));
        CHECK(y == doctest::Approx(want.y).epsilon(0.0001));
        CHECK(sent.z + boat_server.z == doctest::Approx(want.z));
        CHECK(std::fmod(b.heading + heading + 512.0f, 512.0f) == doctest::Approx(100.0f).epsilon(0.001));
    }
}

TEST_CASE("Boats: an update's correction fades in; the drawn deck neither jumps nor trails") {
    game::WorldState w;
    game::Spawn& s = add_boat(w, {0.0f, 0.0f, -12.0f}, 0.0f);
    s.speed_run = 0.45f; // EQEmu's anim 18: 10.4 units a second along the heading
    s.update_time = 0.0;
    double now = 0.0;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) w.smooth(dt, now += dt);
    // At a steady speed the drawn pose is the stepped one: nothing to ease toward, no lag.
    CHECK(glm::distance(s.display_position, s.position) < 0.001f);
    CHECK(s.position.x > 15.0f); // 10.4 units a second, from a standstill
    // An update 1.5 units ahead and 6 heading units round: the drawn pose stays put this frame ...
    const glm::vec3 drawn = s.display_position;
    const float drawn_heading = s.display_heading;
    const glm::vec3 updated = s.position + glm::vec3(1.5f, 0.0f, 0.0f);
    game::vehicle_note_update(s, updated, 6.0f);
    s.position = updated;
    s.heading = 6.0f;
    s.update_time = now;
    w.smooth(dt, now += dt);
    CHECK(glm::distance(s.display_position, drawn) < 0.25f); // one frame of travel, no 1.5 jump
    CHECK(std::fabs(wrap_diff(s.display_heading, drawn_heading)) < 0.2f);
    // ... and has caught up within a few seconds, moving forward all the while.
    float last_x = s.display_position.x;
    for (int i = 0; i < 240; ++i) {
        w.smooth(dt, now += dt);
        CHECK(s.display_position.x >= last_x);
        last_x = s.display_position.x;
    }
    CHECK(glm::length(s.display_error) < 0.05f);
    CHECK(std::fabs(wrap_diff(s.display_heading, s.heading)) < 0.2f);
    // A teleport (the grid's jump back to its start) is not faded.
    const glm::vec3 far = s.position + glm::vec3(0.0f, 300.0f, 0.0f);
    game::vehicle_note_update(s, far, s.heading);
    s.position = far;
    w.smooth(dt, now += dt);
    CHECK(glm::distance(s.display_position, s.position) < 0.5f);
}

TEST_CASE("Boats: the player stays on a moving, turning deck, walks on it, boards from a dock and steps off") {
    test::ScratchDir scratch("boats");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    game::WorldState& w = conn.world_for_test();
    game::Spawn& boat = add_boat(w, {0.0f, 0.0f, -12.0f}, 0.0f);
    boat.display_position = boat.position;
    boat.display_valid = true;
    const Harbour harbour(w, -20.0f);
    app::PlayerMovement m;
    const float deck = boat.position.z + 5.0f;
    m.reset(glm::vec3(3.0f, 2.0f, deck + 0.5f), 0.0f);
    app::MovementKeys keys;
    std::uint32_t now = 5000;
    auto frame = m.update(keys, 0.0f, 0.0f, 50.0f, now, conn, nullptr, kAvatarHeight, &harbour);
    float yaw = frame.yaw;
    auto step = [&](int frames, float speed, float turn) {
        for (int i = 0; i < frames; ++i) {
            // The boat moves and turns where it is drawn (WorldState::smooth_vehicles before the player moves).
            const float h = game::wrap_heading(boat.display_heading + turn);
            const float yaw_b = h * 3.14159265f / 256.0f;
            boat.display_position += glm::vec3(std::cos(yaw_b), std::sin(yaw_b), 0.0f) * speed;
            boat.display_heading = h;
            boat.position = boat.display_position;
            boat.heading = h;
            now += 50;
            frame = m.update(keys, yaw, 0.0f, 50.0f, now, conn, nullptr, kAvatarHeight, &harbour);
            yaw = frame.yaw;
        }
    };
    step(10, 0.0f, 0.0f);
    REQUIRE(m.standing_on() == kBoat);
    const glm::vec3 start = game::boat_to_local(game::drawn_pose(boat), frame.feet);
    const float start_yaw = game::wrap_heading(game::yaw_to_heading(yaw) - boat.display_heading);

    SUBCASE("carried while it sails and turns") {
        for (int k = 0; k < 20; ++k) {
            step(10, 0.6f, k % 4 == 0 ? 3.0f : 0.0f); // 12 units a second, a turn now and then
            CHECK(m.standing_on() == kBoat);
            const glm::vec3 local = game::boat_to_local(game::drawn_pose(boat), frame.feet);
            CHECK(glm::distance(glm::vec2(local), glm::vec2(start)) < 0.05f);
            CHECK(local.z == doctest::Approx(start.z).epsilon(0.01));
            // Facing turns with the deck.
            const float face = game::wrap_heading(game::yaw_to_heading(yaw) - boat.display_heading);
            CHECK(std::fabs(wrap_diff(face, start_yaw)) < 0.1f);
        }
        CHECK(glm::length(glm::vec2(boat.display_position)) > 100.0f);
    }
    SUBCASE("walks on the deck while it sails") {
        keys.forward = true;
        step(4, 0.6f, 1.0f);
        keys.forward = false;
        step(20, 0.6f, 1.0f);
        CHECK(m.standing_on() == kBoat);
        const glm::vec3 local = game::boat_to_local(game::drawn_pose(boat), frame.feet);
        CHECK(glm::distance(glm::vec2(local), glm::vec2(start)) > 1.0f);  // walked
        CHECK(std::fabs(local.x) <= 10.0f);                                 // still aboard
        CHECK(local.z == doctest::Approx(start.z).epsilon(0.01));
        const glm::vec3 held = local;
        step(40, 0.6f, 1.0f);
        CHECK(glm::distance(game::boat_to_local(game::drawn_pose(boat), frame.feet), held) < 0.05f);
    }
    SUBCASE("steps off the deck into the sea and is no longer carried") {
        keys.forward = true;
        step(60, 0.0f, 0.0f);
        keys.forward = false;
        step(60, 0.6f, 0.0f);
        CHECK(m.standing_on() == 0);
        CHECK(frame.feet.z < deck - 1.0f);
    }
    SUBCASE("rails: walking toward the side of a moving deck stays aboard (our own design)") {
        keys.forward = true;
        step(120, 0.6f, 0.0f); // six seconds of walking ahead: far more than the 10 units to the bow
        keys.forward = false;
        CHECK(m.standing_on() == kBoat);
        const glm::vec3 local = game::boat_to_local(game::drawn_pose(boat), frame.feet);
        CHECK(local.x <= 10.5f);
        CHECK(local.x > 7.0f); // it did walk to the edge
        CHECK(frame.feet.z == doctest::Approx(deck).epsilon(0.05));
        // Once the boat lies still, the same walk takes the player over the side.
        keys.forward = true;
        step(60, 0.0f, 0.0f);
        keys.forward = false;
        step(60, 0.0f, 0.0f);
        CHECK(m.standing_on() == 0);
        CHECK(frame.feet.z < deck - 1.0f);
    }
    SUBCASE("boards from the dock") {
        // The boat lies alongside the dock (its stern 2 units from the dock's edge); the player walks along +x.
        boat.display_position = boat.position = glm::vec3(-8.0f, 0.0f, -12.0f);
        m.place(glm::vec3(-30.0f, 0.0f, deck + 0.2f));
        step(10, 0.0f, 0.0f);
        CHECK(m.standing_on() == 0);
        CHECK(frame.feet.z == doctest::Approx(deck).epsilon(0.05));
        keys.forward = true;
        for (int i = 0; i < 200 && m.standing_on() != kBoat; ++i) step(1, 0.0f, 0.0f);
        keys.forward = false;
        step(20, 0.0f, 0.0f);
        CHECK(m.standing_on() == kBoat);
        CHECK(frame.feet.z == doctest::Approx(deck).epsilon(0.05));
        const glm::vec3 aboard = game::boat_to_local(game::drawn_pose(boat), frame.feet);
        step(40, 0.5f, 0.0f); // and sails off with it
        CHECK(glm::distance(game::boat_to_local(game::drawn_pose(boat), frame.feet), aboard) < 0.05f);
        CHECK(frame.feet.x > 5.0f);
    }
}

TEST_CASE("Boats: another spawn on a deck rides it between its updates") {
    game::WorldState w;
    w.player_id = 1;
    game::Spawn& boat = add_boat(w, {0.0f, 0.0f, -12.0f}, 0.0f);
    boat.speed_run = 0.45f;
    game::Spawn& rider = w.spawns[2];
    rider.id = 2;
    rider.name = "Somebody";
    rider.race = 1;
    rider.position = glm::vec3(4.0f, 1.0f, -7.0f + kAvatarHeight);
    rider.heading = 128.0f;
    double now = 10.0;
    boat.update_time = rider.update_time = now;
    // The deck test: within 10 of the boat's middle.
    auto on_deck = [&](const glm::vec3& p) -> std::uint32_t {
        const game::BoatPose pose = game::drawn_pose(w.spawns.at(kBoat));
        return glm::length(glm::vec2(p - pose.position)) < 10.0f ? kBoat : 0;
    };
    const float dt = 1.0f / 60.0f;
    w.smooth(dt, now += dt, {}, {}, on_deck);
    CHECK(rider.on_vehicle == kBoat);
    // Its place on the deck (the boat has stepped one frame since the update: a fraction of a unit).
    const glm::vec3 aboard = game::boat_to_local(game::drawn_pose(boat), rider.display_position);
    CHECK(glm::distance(aboard, glm::vec3(4.0f, 1.0f, rider.position.z - boat.position.z)) < 0.3f);
    for (int i = 0; i < 300; ++i) w.smooth(dt, now += dt, {}, {}, on_deck);
    // Five seconds without an update from the rider: still where it stood on the deck, which has sailed on.
    CHECK(boat.position.x > 40.0f);
    const glm::vec3 local = game::boat_to_local(game::drawn_pose(boat), rider.display_position);
    CHECK(glm::distance(local, aboard) < 0.01f);
    CHECK(rider.display_position.z == doctest::Approx(-7.0f + kAvatarHeight).epsilon(0.01));
    // An update that puts it in the water lets go of the boat.
    rider.position = boat.position + glm::vec3(0.0f, 40.0f, -10.0f);
    rider.update_time = now;
    w.smooth(dt, now += dt, {}, {}, on_deck);
    CHECK(rider.on_vehicle == 0);
}

TEST_CASE("Boats: the helm sails a small boat ahead, turns it to the left and lets it drift to a stop") {
    game::Helm helm;
    game::BoatPose pose{{0.0f, 0.0f, -20.0f}, 0.0f};
    const float dt = 0.05f;
    for (int i = 0; i < 60; ++i) pose = game::steer_boat(helm, pose, true, false, false, false, dt);
    CHECK(helm.speed == doctest::Approx(game::kHelmTopSpeed));
    CHECK(pose.position.x > 20.0f);
    CHECK(std::fabs(pose.position.y) < 0.001f);
    CHECK(pose.position.z == -20.0f);
    // Left: the heading goes up (yaw counterclockwise), and the boat bends toward +y.
    for (int i = 0; i < 20; ++i) pose = game::steer_boat(helm, pose, true, false, true, false, dt);
    CHECK(pose.heading == doctest::Approx(game::kHelmTurnRate).epsilon(0.01));
    CHECK(pose.position.y > 0.5f);
    const glm::vec3 at = pose.position;
    for (int i = 0; i < 200; ++i) pose = game::steer_boat(helm, pose, false, false, false, false, dt);
    CHECK(helm.speed == 0.0f);
    const glm::vec3 stopped = pose.position;
    CHECK(glm::distance(stopped, at) > 5.0f);
    pose = game::steer_boat(helm, pose, false, false, false, false, dt);
    CHECK(pose.position == stopped);
    // Astern only crawls.
    for (int i = 0; i < 100; ++i) pose = game::steer_boat(helm, pose, false, true, false, false, dt);
    CHECK(helm.speed == doctest::Approx(-game::kHelmAsternSpeed));
}

namespace {

// A flat deck `w` x `h` at height `z` in the boat's frame (two triangles), as world triangles at `pose`.
game::VehicleHull flat_hull(std::uint32_t id, const game::BoatPose& pose, float w, float h, float z) {
    const glm::vec3 a = game::boat_to_world(pose, {-w / 2, -h / 2, z}), b = game::boat_to_world(pose, {w / 2, -h / 2, z}),
                    c = game::boat_to_world(pose, {w / 2, h / 2, z}), d = game::boat_to_world(pose, {-w / 2, h / 2, z});
    game::VehicleHull v;
    v.id = id;
    // Wound so the normal faces up (CollisionTriangle::make: (v0 - v1) x (v2 - v1)).
    v.triangles = {game::CollisionTriangle::make(a, c, b, 0), game::CollisionTriangle::make(a, d, c, 0)};
    v.center = pose.position + glm::vec3(0, 0, z);
    v.radius = glm::length(glm::vec2(w, h)) / 2;
    return v;
}

// A box-shaped hull's corner points in the boat's frame (x -10..10, y -4..4, z 0..5).
std::vector<glm::vec3> box_points() {
    std::vector<glm::vec3> out;
    for (float x : {-10.0f, 10.0f})
        for (float y : {-4.0f, 4.0f})
            for (float z : {0.0f, 5.0f}) out.emplace_back(x, y, z);
    return out;
}

} // namespace

TEST_CASE("Boats: overlapping decks go to the nearest one, and to the one stood on when they are level") {
    const game::BoatPose here{{0.0f, 0.0f, 0.0f}, 0.0f}, beside{{6.0f, 0.0f, 0.0f}, 64.0f};
    const glm::vec3 p(3.0f, 1.0f, 20.0f);
    auto drop = [&](const std::vector<game::VehicleHull>& hulls, std::uint32_t prefer, std::uint32_t skip = 0) {
        game::SphereSweep s = game::SphereSweep::make(p, p - glm::vec3(0, 0, 40.0f), 0.5f);
        s.floor_only = true;
        return game::sweep_hulls(s, hulls, 0.5f, prefer, skip);
    };
    SUBCASE("level decks: the one stood on, in either order") {
        const std::vector<game::VehicleHull> ab{flat_hull(1, here, 20, 12, 5), flat_hull(2, beside, 20, 12, 5)};
        const std::vector<game::VehicleHull> ba{ab[1], ab[0]};
        CHECK(drop(ab, 1) == 1);
        CHECK(drop(ba, 1) == 1);
        CHECK(drop(ab, 2) == 2);
        CHECK(drop(ba, 2) == 2);
        CHECK(drop(ab, 0) != 0);
    }
    SUBCASE("a higher deck is the one under the feet, whichever is stood on") {
        const std::vector<game::VehicleHull> ab{flat_hull(1, here, 20, 12, 5), flat_hull(2, beside, 20, 12, 8)};
        const std::vector<game::VehicleHull> ba{ab[1], ab[0]};
        CHECK(drop(ab, 1) == 2);
        CHECK(drop(ba, 1) == 2);
        CHECK(drop(ab, 0) == 2);
        CHECK(drop(ab, 2, 2) == 1); // the helm's own boat is skipped
    }
}

TEST_CASE("Boats: the hull's probes sit just inside its outline") {
    const auto probes = game::hull_probes(box_points(), 1.0f);
    // The box's four ends and four corners answer the 16 directions.
    CHECK(probes.size() >= 4);
    CHECK(probes.size() <= static_cast<std::size_t>(game::kHullProbeDirections));
    float max_x = -1e9f, min_x = 1e9f, max_y = -1e9f, min_y = 1e9f;
    for (const auto& q : probes) {
        CHECK(std::fabs(q.x) <= 10.0f);
        CHECK(std::fabs(q.y) <= 4.0f);
        max_x = std::max(max_x, q.x), min_x = std::min(min_x, q.x);
        max_y = std::max(max_y, q.y), min_y = std::min(min_y, q.y);
    }
    CHECK(max_x > 8.5f);
    CHECK(max_x < 10.0f);
    CHECK(min_x < -8.5f);
    CHECK(min_x > -10.0f);
    CHECK(max_y > 2.5f);
    CHECK(min_y < -2.5f);
    CHECK(game::hull_probes({}).empty());
}

TEST_CASE("Boats: the helm stops short of a dock and another boat, and may still turn away (our own design)") {
    const auto probes = game::hull_probes(box_points());
    SUBCASE("a dock across its bow") {
        // The dock: everything at x >= 40 (its face), as a probe's sweep would find it.
        const game::HullSweepFn dock = [](const glm::vec3&, const glm::vec3& b, float r) { return b.x + r >= 40.0f; };
        game::Helm helm;
        game::BoatPose pose{{0.0f, 0.0f, -20.0f}, 0.0f};
        const float dt = 0.05f;
        auto clear = [&](const game::BoatPose& from, const game::BoatPose& to) {
            return game::hull_clear(from, to, probes, dock);
        };
        for (int i = 0; i < 200; ++i)
            pose = game::helm_resolve(helm, pose, game::steer_boat(helm, pose, true, false, false, false, dt), clear);
        // Its bow (x 10) stopped within a step of the dock's face, short of it.
        CHECK(pose.position.x + 10.0f < 40.0f);
        CHECK(pose.position.x + 10.0f > 40.0f - 1.5f);
        CHECK(helm.speed < 1.0f);
        // Hard to port while backing off: astern first (the swing would put a corner into the dock), then the turn.
        const float x = pose.position.x;
        for (int i = 0; i < 40; ++i)
            pose = game::helm_resolve(helm, pose, game::steer_boat(helm, pose, false, true, true, false, dt), clear);
        CHECK(pose.heading > 0.0f);
        CHECK(pose.position.x < x);
    }
    SUBCASE("another boat's hull") {
        // The other boat's hull side: a wall across the way at x 34.
        const std::vector<game::VehicleHull> walls = [] {
            game::VehicleHull v;
            v.id = 9;
            const glm::vec3 a(34.0f, -10.0f, -30.0f), b(34.0f, 10.0f, -30.0f), c(34.0f, 10.0f, -5.0f), d(34.0f, -10.0f, -5.0f);
            v.triangles = {game::CollisionTriangle::make(a, b, c, 0), game::CollisionTriangle::make(a, c, d, 0)};
            v.center = glm::vec3(34.0f, 0.0f, -17.5f);
            v.radius = 20.0f;
            return std::vector<game::VehicleHull>{v};
        }();
        const game::HullSweepFn hulls = [&](const glm::vec3& a, const glm::vec3& b, float r) {
            game::SphereSweep s = game::SphereSweep::make(a, b, r);
            game::sweep_hulls(s, walls, r, 0, 1); // its own hull (1) is not in the way
            return s.collided && s.t > 0.0f;
        };
        game::Helm helm;
        game::BoatPose pose{{0.0f, 0.0f, -20.0f}, 0.0f};
        auto clear = [&](const game::BoatPose& from, const game::BoatPose& to) {
            return game::hull_clear(from, to, probes, hulls);
        };
        for (int i = 0; i < 200; ++i)
            pose = game::helm_resolve(helm, pose, game::steer_boat(helm, pose, true, false, false, false, 0.05f), clear);
        CHECK(pose.position.x + 10.0f < 34.0f);
        CHECK(pose.position.x + 10.0f > 32.5f);
    }
}

