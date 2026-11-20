// The zone's own ladders (game/zone_ladders.h): LADDER* placements are actors of type 6, so walking into one climbs
// it (CollidingType 6 from the sweep's hit callback) and a click mounts it.

#include <doctest/doctest.h>

#include <optional>
#include <thread>
#include <vector>

#include "app/player_movement.h"
#include "app/zone_move_world.h"
#include "assets/zone.h"
#include "core/game_dir.h"
#include "game/collision.h"
#include "game/mechanics/collision_movement.h"
#include "game/zone_ladders.h"
#include "game_files.h"
#include "net/connection/game_connection.h"
#include "plugin/policy.h"
#include "scratch_dir.h"

using namespace hailstone;

namespace {

// An axis-aligned box as a mesh (12 triangles).
assets::Mesh box_mesh(glm::vec3 lo, glm::vec3 hi) {
    assets::Mesh m;
    for (int i = 0; i < 8; ++i) {
        assets::Vertex v;
        v.position = {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
        m.vertices.push_back(v);
    }
    const std::uint32_t faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    for (const auto& f : faces) m.indices.insert(m.indices.end(), {f[0], f[1], f[2], f[0], f[2], f[3]});
    m.materials.push_back(assets::Material{});
    m.parts.push_back({0, static_cast<std::uint32_t>(m.indices.size()), 0, true});
    return m;
}

// The triangle with its record normal (game::CollisionTriangle::make) facing `out`.
game::CollisionTriangle facing(glm::vec3 a, glm::vec3 b, glm::vec3 c, const glm::vec3& out, std::uint8_t flags) {
    if (glm::dot(glm::cross(a - b, c - b), out) < 0.0f) std::swap(b, c);
    return game::CollisionTriangle::make(a, b, c, flags);
}

void quad(std::vector<game::CollisionTriangle>& t, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          const glm::vec3& out, std::uint8_t flags = 0) {
    t.push_back(facing(a, b, c, out, flags));
    t.push_back(facing(a, c, d, out, flags));
}

enum class Wall { Plain, Ladder, PassableLadder };

// Ground at z 0; a wall 60 high at x 10 facing -x, itself a ladder (Ladder), or plain with a passable ladder 0.7 in
// front of it (PassableLadder, as Cabilis's LADDERCB101 against a canal wall).
game::CollisionMesh wall_scene(Wall wall) {
    std::vector<game::CollisionTriangle> t;
    quad(t, {-30, -300, 0}, {300, -300, 0}, {300, 300, 0}, {-30, 300, 0}, {0, 0, 1});
    const std::uint8_t flags = wall == Wall::Ladder ? game::kTriangleObject | game::kTriangleLadder : game::kTriangleObject;
    quad(t, {10, -5, 0}, {10, 5, 0}, {10, 5, 60}, {10, -5, 60}, {-1, 0, 0}, flags);
    if (wall == Wall::PassableLadder)
        quad(t, {9.3f, -2, 0}, {9.3f, 2, 0}, {9.3f, 2, 60}, {9.3f, -2, 60}, {-1, 0, 0},
             game::kTriangleObject | game::kTriangleLadder | game::kTrianglePassable);
    return game::CollisionMesh::from_triangles(std::move(t));
}

// The zone mesh as ZoneMoveWorld uses it (app/zone_move_world.h): the sweep's final triangle tells a ladder hit, and
// with `passable_ladders` (hailstone.cfg [movement]) so does a passable ladder met short of the stop.
class MeshWorld final : public app::MoveWorld {
public:
    explicit MeshWorld(const game::CollisionMesh& mesh, bool passable_ladders = false)
        : mesh_(mesh), passable_ladders_(passable_ladders) {}
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
            const std::int32_t tri = mesh_.sweep_sphere_hit(s);
            out.ladder_hit =
                tri >= 0 && (mesh_.triangles()[static_cast<std::size_t>(tri)].flags & game::kTriangleLadder);
            if (passable_ladders_ && !floor_only && !out.ladder_hit) out.ladder_hit = mesh_.touches_passable_ladder(s);
        }
        out.out = game::sweep_output(s);
        out.collided = s.collided;
        return out;
    }
    const game::CollisionMesh& mesh_;
    bool passable_ladders_;
};

struct Walk {
    float top_z = 0.0f;
    std::int32_t colliding_type = 0;
};

// Walks at the wall (+x) for 2.4 s looking up 45 degrees (short of the top, which a climb reaches in about 3 s).
Walk walk_at_wall(Wall wall, bool passable_ladders = false) {
    test::ScratchDir scratch("zone_ladders");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    const game::CollisionMesh mesh = wall_scene(wall);
    const MeshWorld world(mesh, passable_ladders);
    constexpr float kAvatarHeight = 3.75f;
    app::PlayerMovement m;
    m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{kAvatarHeight, 1}));
    m.set_race(1);
    m.reset(glm::vec3(0.0f, 0.0f, 0.0f), 0.0f);
    app::MovementKeys keys;
    std::uint32_t now = 10000;
    Walk w;
    const float pitch = glm::radians(45.0f);
    for (int i = 0; i < 150; ++i) {
        keys.forward = i >= 3;
        now += 16;
        app::PlayerMovement::Frame f = m.update(keys, 0.0f, pitch, 16.0f, now, conn, nullptr, kAvatarHeight, &world);
        w.top_z = std::max(w.top_z, f.feet.z);
        w.colliding_type = m.controller().body.colliding_type;
    }
    return w;
}

// Adds an axis-aligned box (12 triangles) to a mesh's one part.
void add_box(assets::Mesh& m, glm::vec3 lo, glm::vec3 hi) {
    const auto base = static_cast<std::uint32_t>(m.vertices.size());
    for (int i = 0; i < 8; ++i) {
        assets::Vertex v;
        v.position = {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
        m.vertices.push_back(v);
    }
    const std::uint32_t faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    // Each triangle facing out of the box (CollisionTriangle::make's normal, (a - b) x (c - b)), front_ccw clear.
    const glm::vec3 middle = (lo + hi) * 0.5f;
    auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        const glm::vec3 pa = m.vertices[base + a].position, pb = m.vertices[base + b].position,
                        pc = m.vertices[base + c].position;
        if (glm::dot(glm::cross(pa - pb, pc - pb), (pa + pb + pc) / 3.0f - middle) < 0.0f) std::swap(b, c);
        m.indices.insert(m.indices.end(), {base + a, base + b, base + c});
    };
    for (const auto& f : faces) {
        tri(f[0], f[1], f[2]);
        tri(f[0], f[2], f[3]);
    }
    if (m.materials.empty()) m.materials.push_back(assets::Material{});
    if (m.parts.empty()) m.parts.push_back({0, 0, 0, true});
    m.parts[0].index_count = static_cast<std::uint32_t>(m.indices.size());
}

constexpr float kRungLadderHeight = 42.0f;

// A ladder of rails and rungs as LADDER14 .. LADDER60 are built (0.72 deep, 3.5 wide, a rung every 3.5), 42 high, at
// x 10 facing -x, on ground at z 0 with a wall 2.3 behind it: room for a small sphere that goes between the rungs.
assets::Zone rung_ladder_zone() {
    assets::Zone zone;
    assets::Mesh ground;
    add_box(ground, {-60.0f, -60.0f, -2.0f}, {60.0f, 60.0f, 0.0f});
    add_box(ground, {13.0f, -60.0f, 0.0f}, {15.0f, 60.0f, 80.0f});
    zone.terrain.push_back(std::move(ground));
    assets::Mesh ladder;
    add_box(ladder, {0.02f, -1.75f, -1.0f}, {0.74f, -1.45f, kRungLadderHeight});
    add_box(ladder, {0.02f, 1.45f, -1.0f}, {0.74f, 1.75f, kRungLadderHeight});
    for (float z = 1.75f; z < kRungLadderHeight; z += 3.5f) add_box(ladder, {0.02f, -1.45f, z}, {0.74f, 1.45f, z + 0.3f});
    zone.objects["LADDER42_ACTORDEF"] = std::move(ladder);
    assets::Placement p;
    p.actor = "LADDER42_ACTORDEF";
    p.transform = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    zone.placements.push_back(p);
    return zone;
}

// AvatarHeight as SetHeights gives it from racedata.txt's default heights (Height x 0.625): gnome 3, halfling 3.5,
// dwarf 4, human 6, ogre 9.
struct Race {
    const char* name;
    std::uint32_t race;
    float avatar_height;
};
constexpr Race kRaces[] = {{"gnome", 12, 1.875f}, {"halfling", 11, 2.1875f}, {"dwarf", 8, 2.5f},
                           {"human", 1, 3.75f},   {"ogre", 10, 5.625f}};

// How far above `from` the feet get walking along `yaw` for 8 s, looking up 45 degrees.
float climb(const app::MoveWorld& world, const Race& r, glm::vec3 from = {0.0f, 0.0f, 0.0f}, float yaw = 0.0f,
            const assets::ZoneRegions* regions = nullptr) {
    test::ScratchDir scratch("zone_ladders_climb");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    app::PlayerMovement m;
    m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{r.avatar_height, r.race}));
    m.set_race(r.race);
    m.reset(from, yaw);
    app::MovementKeys keys;
    std::uint32_t now = 10000;
    float top = from.z;
    for (int i = 0; i < 500; ++i) {
        keys.forward = i >= 3;
        now += 16;
        app::PlayerMovement::Frame f =
            m.update(keys, yaw, glm::radians(45.0f), 16.0f, now, conn, regions, r.avatar_height, &world);
        top = std::max(top, f.feet.z);
    }
    return top - from.z;
}

} // namespace

TEST_CASE("zone ladders: the actor tag starts with LADDER, in any case") {
    CHECK(game::is_ladder_actor("LADDER14_ACTORDEF"));
    CHECK(game::is_ladder_actor("ladderCB101_ACTORDEF"));
    CHECK(game::is_ladder_actor("LADDER"));
    CHECK_FALSE(game::is_ladder_actor("LADDE"));
    CHECK_FALSE(game::is_ladder_actor("XLADDER_ACTORDEF"));
    CHECK_FALSE(game::is_ladder_actor("IT63_ACTORDEF"));
}

TEST_CASE("zone ladders: a LADDER placement is a ladder actor and its triangles are marked") {
    assets::Zone zone;
    zone.objects["LADDER14_ACTORDEF"] = box_mesh({0.0f, -1.7f, -1.0f}, {0.7f, 1.7f, 14.6f});
    zone.objects["BARREL_ACTORDEF"] = box_mesh({-1, -1, 0}, {1, 1, 3});
    const glm::vec3 at(100.0f, 200.0f, 5.0f);
    constexpr float kUnit = 2.0f * glm::pi<float>() / 512.0f;
    assets::Placement ladder;
    ladder.actor = "LADDER14_ACTORDEF";
    // As wld::placement_transform: heading 128 about Z, a small lean about Y, scale 2.
    ladder.transform = glm::translate(glm::mat4(1.0f), at);
    ladder.transform = glm::rotate(ladder.transform, 128.0f * kUnit, glm::vec3(0, 0, 1));
    ladder.transform = glm::rotate(ladder.transform, -8.0f * kUnit, glm::vec3(0, 1, 0));
    ladder.transform = glm::scale(ladder.transform, glm::vec3(2.0f));
    assets::Placement barrel;
    barrel.actor = "BARREL_ACTORDEF";
    barrel.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0));
    zone.placements = {barrel, ladder};

    auto ladders = game::zone_ladders(zone);
    REQUIRE(ladders.size() == 1);
    const auto& l = ladders[0].ladder;
    CHECK(l.type == 6);
    CHECK(l.x == doctest::Approx(at.x));
    CHECK(l.y == doctest::Approx(at.y));
    CHECK(l.z == doctest::Approx(at.z));
    CHECK(l.heading == doctest::Approx(128.0f).epsilon(1e-4));
    CHECK(l.height == doctest::Approx(14.6f * 2.0f));
    CHECK(ladders[0].bound > 0.0f);

    const game::CollisionMesh mesh = game::CollisionMesh::build(zone);
    std::size_t marked = 0, objects = 0;
    for (const auto& t : mesh.triangles()) {
        if (t.flags & game::kTriangleObject) ++objects;
        if (t.flags & game::kTriangleLadder) ++marked;
    }
    CHECK(objects == 36);
    CHECK(marked == 24); // the ladder's and its slab's, not the barrel's
}

TEST_CASE("zone ladders: walking into a ladder climbs it; into a wall does not") {
    const Walk wall = walk_at_wall(Wall::Plain);
    CHECK(wall.colliding_type == 0);
    CHECK(wall.top_z < 1.0f);
    const Walk ladder = walk_at_wall(Wall::Ladder);
    CHECK(ladder.colliding_type == 6);
    CHECK(ladder.top_z > 30.0f);
}

TEST_CASE("zone ladders: a ladder whose polygons are all passable is kept, clickable, and never blocks") {
    // Cabilis's LADDERCB101: every polygon has the WLD passable flag (a part that is not solid).
    assets::Zone zone;
    assets::Mesh mesh = box_mesh({0.0f, -1.0f, -12.0f}, {0.7f, 1.0f, 12.0f});
    mesh.parts[0].solid = false;
    zone.objects["LADDERCB101_ACTORDEF"] = mesh;
    assets::Placement p;
    p.actor = "LADDERCB101_ACTORDEF";
    p.transform = glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 60.0f, -13.0f));
    zone.placements = {p};

    const auto ladders = game::zone_ladders(zone);
    REQUIRE(ladders.size() == 1);
    CHECK(ladders[0].ladder.height == doctest::Approx(12.0f)); // the top of all its triangles
    REQUIRE(ladders[0].shape.records.size() == 12);
    for (const auto& r : ladders[0].shape.records) CHECK((r.flags & game::kTrianglePassable) != 0); // the pick's
    // passableBlocking pass hits them

    const game::CollisionMesh collision = game::CollisionMesh::build(zone);
    std::size_t passable_ladder = 0;
    for (const auto& t : collision.triangles())
        if ((t.flags & game::kTriangleLadder) && (t.flags & game::kTrianglePassable)) ++passable_ladder;
    CHECK(passable_ladder == 12);
}

TEST_CASE("zone ladders: walking into a passable ladder climbs it only with [movement] passable_ladders") {
    // As in the original (nothing gives its triangles the DLL's bit 2): the player walks through it to the wall.
    const Walk faithful = walk_at_wall(Wall::PassableLadder, false);
    CHECK(faithful.colliding_type == 0);
    CHECK(faithful.top_z < 1.0f);
    // Hailstone's switch: touching it short of the stop, or overlapping it where the move starts, climbs.
    const Walk climbed = walk_at_wall(Wall::PassableLadder, true);
    CHECK(climbed.colliding_type == 6);
    CHECK(climbed.top_z > 30.0f);
    // A plain wall or a solid ladder is the same either way.
    CHECK(walk_at_wall(Wall::Plain, true).top_z < 1.0f);
    CHECK(walk_at_wall(Wall::Ladder, true).top_z > 30.0f);
}

TEST_CASE("zone ladders: other spawns' contact probes climb a passable ladder under the player's rules") {
    const game::CollisionMesh mesh = wall_scene(Wall::PassableLadder);
    game::Spawn spawn;
    spawn.race = 1;
    spawn.heights.avatar_height = 3.75f;
    std::vector<app::WorldObjects::DoorHit> doors;
    auto probe = [&](const std::optional<plugin::Rules>& rules) {
        app::ZoneMoveWorld world(&mesh, nullptr, nullptr);
        world.apply_remote_rules(rules);
        world.set_contact_probe(&doors);
        // A spawn walking toward the wall, into the passable ladder in front of it.
        return app::remote_collision_type(spawn, {0, 0, 3.75f}, {9.5f, 0, 3.75f}, world);
    };
    // No hailstone.cfg: on, as for the player.
    CHECK(probe(std::nullopt) == 6);
    plugin::Rules on;
    on.configured = true;
    CHECK(probe(on) == 6);
    // The server's [movement] passable_ladders = off: walked through, as in the original.
    plugin::Rules off = on;
    off.passable_ladders = false;
    CHECK(probe(off) == 0);
    // The world built without the rules (as remote probes were): no climb.
    app::ZoneMoveWorld bare(&mesh, nullptr, nullptr);
    bare.set_contact_probe(&doors);
    CHECK(app::remote_collision_type(spawn, {0, 0, 3.75f}, {9.5f, 0, 3.75f}, bare) == 0);
}

TEST_CASE("zone ladders: other spawns' contact probes do not take the player's /nav pass-door list") {
    std::vector<game::Door> doors(2);
    doors[0].id = 3;
    doors[0].state = 1; // open
    doors[1].id = 4;    // shut
    // The player's world while /nav passes open doors: the open one is passed.
    app::ZoneMoveWorld player(nullptr, nullptr, nullptr);
    player.apply_rules(std::nullopt, doors, true);
    CHECK(player.passable_doors() == std::vector<std::uint8_t>{3});
    // Another spawn's probe meets every door, whatever the player's /nav does, and keeps the passable ladders.
    app::ZoneMoveWorld remote(nullptr, nullptr, nullptr);
    remote.set_passable_doors({3});
    remote.apply_remote_rules(std::nullopt);
    CHECK(remote.passable_doors().empty());
    const game::CollisionMesh mesh = wall_scene(Wall::PassableLadder);
    app::ZoneMoveWorld climb(&mesh, nullptr, nullptr);
    climb.apply_remote_rules(std::nullopt);
    std::vector<app::WorldObjects::DoorHit> hits;
    climb.set_contact_probe(&hits);
    game::Spawn spawn;
    spawn.race = 1;
    spawn.heights.avatar_height = 3.75f;
    CHECK(app::remote_collision_type(spawn, {0, 0, 3.75f}, {9.5f, 0, 3.75f}, climb) == 6);
}

TEST_CASE("zone ladders: the contact probe answers as the full sweep does for a solid ladder and a wall") {
    game::Spawn spawn;
    spawn.race = 1;
    spawn.heights.avatar_height = 3.75f;
    std::vector<app::WorldObjects::DoorHit> doors;
    for (Wall wall : {Wall::Plain, Wall::Ladder, Wall::PassableLadder}) {
        CAPTURE(static_cast<int>(wall));
        const game::CollisionMesh mesh = wall_scene(wall);
        app::ZoneMoveWorld full(&mesh, nullptr, nullptr);
        full.apply_rules(std::nullopt, {}, false);
        app::ZoneMoveWorld contact(&mesh, nullptr, nullptr);
        contact.apply_rules(std::nullopt, {}, false);
        contact.set_contact_probe(&doors);
        for (float y : {0.0f, 4.0f, 20.0f}) {
            CAPTURE(y);
            CHECK(app::remote_collision_type(spawn, {0, y, 3.75f}, {12, y, 3.75f}, full) ==
                  app::remote_collision_type(spawn, {0, y, 3.75f}, {12, y, 3.75f}, contact));
        }
    }
}

TEST_CASE("zone ladders: the per-thread contact probe answers as the buffered one, from several threads at once") {
    game::Spawn spawn;
    spawn.race = 1;
    spawn.heights.avatar_height = 3.75f;
    for (Wall wall : {Wall::Plain, Wall::Ladder, Wall::PassableLadder}) {
        CAPTURE(static_cast<int>(wall));
        const game::CollisionMesh mesh = wall_scene(wall);
        std::vector<app::WorldObjects::DoorHit> doors;
        app::ZoneMoveWorld buffered(&mesh, nullptr, nullptr);
        buffered.apply_remote_rules(std::nullopt);
        buffered.set_contact_probe(&doors);
        app::ZoneMoveWorld shared(&mesh, nullptr, nullptr);
        shared.apply_remote_rules(std::nullopt);
        shared.set_contact_probe_per_thread();
        const float ys[] = {0.0f, 4.0f, 20.0f};
        std::int32_t expected[3] = {};
        for (int i = 0; i < 3; ++i)
            expected[i] = app::remote_collision_type(spawn, {0, ys[i], 3.75f}, {12, ys[i], 3.75f}, buffered);
        // One world, probed by four threads together, as WorldState::smooth's workers probe it.
        std::int32_t got[4][3] = {};
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&, t] {
                for (int pass = 0; pass < 50; ++pass)
                    for (int i = 0; i < 3; ++i)
                        got[t][i] = app::remote_collision_type(spawn, {0, ys[i], 3.75f}, {12, ys[i], 3.75f}, shared);
            });
        for (auto& th : threads) th.join();
        for (int t = 0; t < 4; ++t)
            for (int i = 0; i < 3; ++i) CHECK(got[t][i] == expected[i]);
    }
}

TEST_CASE("zone ladders: hailstone.cfg [movement] passable_ladders") {
    CHECK(plugin::Rules{}.passable_ladders);              // no file: on
    CHECK(plugin::parse_server_cfg("").passable_ladders); // a file that does not say: on
    std::vector<std::string> warnings;
    CHECK_FALSE(plugin::parse_server_cfg("[movement]\npassable_ladders = off\n", &warnings).passable_ladders);
    CHECK(warnings.empty());
    CHECK(plugin::parse_server_cfg("[movement]\npassable_ladders = on\n").passable_ladders);
}

TEST_CASE("zone ladders: Upper Guk's ladders" * doctest::skip(!hailstone::test::game_dir())) {
    const assets::Zone zone = assets::load_zone(*hailstone::test::game_dir(), "guktop");
    const auto ladders = game::zone_ladders(zone);
    REQUIRE(!ladders.empty());
    for (const auto& l : ladders) {
        CAPTURE(l.ladder.x);
        CAPTURE(l.ladder.y);
        // LADDER14 / 28 / 42 at their placements' scale: about as tall as the name says, stood upright.
        CHECK(l.ladder.height > 10.0f);
        CHECK(l.ladder.height < 100.0f);
        CHECK(l.ladder.heading >= 0.0f);
        CHECK(l.ladder.heading < 512.0f);
    }
    const game::CollisionMesh mesh = game::CollisionMesh::build(zone);
    std::size_t marked = 0;
    for (const auto& t : mesh.triangles())
        if (t.flags & game::kTriangleLadder) ++marked;
    CHECK(marked > 0);
    MESSAGE("guktop: ", ladders.size(), " ladders, ", marked, " ladder triangles");
}

TEST_CASE("zone ladders: Cabilis East's canal ladders" * doctest::skip(!hailstone::test::game_dir())) {
    const assets::Zone zone = assets::load_zone(*hailstone::test::game_dir(), "cabeast");
    const auto ladders = game::zone_ladders(zone);
    // 2 LADDER14 (solid bamboo) and 13 LADDERCB101 (every polygon passable): all of them clickable.
    std::size_t placed = 0;
    for (const auto& p : zone.placements)
        if (game::is_ladder_actor(p.actor)) ++placed;
    CHECK(placed == 15);
    CHECK(ladders.size() == placed);

    // The canal ladder at (850, -98), heading 256: swimming at it from the canal, looking up.
    const game::ZoneLadder* canal = nullptr;
    for (const auto& l : ladders)
        if (glm::distance(glm::vec2(l.center), glm::vec2(850.0f, -98.1f)) < 1.0f) canal = &l;
    REQUIRE(canal != nullptr);
    const game::CollisionMesh mesh = game::CollisionMesh::build(zone);
    test::ScratchDir scratch("zone_ladders_cabeast");
    GameDir game(scratch.path());
    net::GameConnection conn(game, {});
    auto swim = [&](bool passable_ladders) {
        const MeshWorld world(mesh, passable_ladders);
        constexpr float kAvatarHeight = 3.75f;
        app::PlayerMovement m;
        m.set_collision_radius(game::mechanics::sweep_radius(game::mechanics::SweepRadiusSpawn{kAvatarHeight, 1}));
        m.set_race(1);
        const float yaw = glm::half_pi<float>(); // +y, at the ladder
        m.reset(canal->center + glm::vec3(0.0f, -12.0f, 3.0f), yaw);
        app::MovementKeys keys;
        std::uint32_t now = 10000;
        float top = -1e9f;
        for (int i = 0; i < 400; ++i) {
            keys.forward = i >= 3;
            now += 16;
            auto f = m.update(keys, yaw, glm::radians(45.0f), 16.0f, now, conn, &zone.regions, kAvatarHeight, &world);
            top = std::max(top, f.feet.z);
        }
        return top;
    };
    CHECK(swim(false) < -9.0f); // the original: no way out of the canal here
    CHECK(swim(true) > -1.0f);  // up to the street (z 0)
}

TEST_CASE("zone ladders: a solid ladder's slab is the box of its solid polygons, placed as the ladder") {
    assets::Mesh mesh;
    add_box(mesh, {0.0f, -1.7f, -1.0f}, {0.7f, 1.7f, 14.6f});
    constexpr float kUnit = 2.0f * glm::pi<float>() / 512.0f;
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 200.0f, 5.0f));
    transform = glm::rotate(transform, 128.0f * kUnit, glm::vec3(0, 0, 1));
    transform = glm::rotate(transform, -8.0f * kUnit, glm::vec3(0, 1, 0)); // leaning, as Upper Guk's
    const auto slab = game::ladder_slab(mesh, transform);
    REQUIRE(slab.size() == 12);
    const glm::vec3 middle(transform * glm::vec4(0.35f, 0.0f, 6.8f, 1.0f));
    for (const auto& t : slab) {
        CHECK(t.flags == (game::kTriangleObject | game::kTriangleLadder | game::kTriangleLadderSlab));
        CHECK(glm::dot(t.normal, (t.v0 + t.v1 + t.v2) / 3.0f - middle) > 0.0f); // facing out
    }
    // Every corner is a corner of the placed box.
    const glm::vec3 top(transform * glm::vec4(0.7f, 1.7f, 14.6f, 1.0f));
    bool found = false;
    for (const auto& t : slab)
        for (const glm::vec3& v : {t.v0, t.v1, t.v2}) found = found || glm::distance(v, top) < 1e-3f;
    CHECK(found);

    // A ladder whose polygons are all passable (LADDERCB101) has none.
    mesh.parts[0].solid = false;
    CHECK(game::ladder_slab(mesh, transform).empty());
}

TEST_CASE("zone ladders: the slab is the movement's alone, and only with solid ladders") {
    const assets::Zone zone = rung_ladder_zone();
    game::CollisionMesh mesh = game::CollisionMesh::build(zone);
    CHECK_FALSE(mesh.solid_ladders()); // off unless asked: the meshes of the neighbour zones, weather and plates
    // Between two rungs (z 5.25 .. 8.75), in front of the ladder and behind it.
    const glm::vec3 front(9.0f, 0.0f, 7.0f), back(12.0f, 0.0f, 7.0f);
    for (bool solid : {false, true}) {
        CAPTURE(solid);
        mesh.set_solid_ladders(solid);
        CHECK(mesh.line_of_sight(front, back)); // seen through the rungs either way
        CHECK(mesh.surfaces_above({10.4f, 0.0f, 7.0f}).front().z == doctest::Approx(8.75f));
        // A sphere moved through the gap: stopped by the slab only.
        game::SphereSweep s = game::SphereSweep::make(front, back, 0.5f);
        mesh.sweep_sphere(s);
        CHECK(s.collided == solid);
        // The movement's floor and ceiling inside the ladder: the rungs, or the slab's top and nothing above it.
        const glm::vec3 inside(10.4f, 0.0f, 7.0f);
        if (solid) {
            CHECK(mesh.floor_below(inside, 0.0f) == doctest::Approx(0.0f)); // the ground: the slab is not below
            CHECK(mesh.floor_below({10.4f, 0.0f, 50.0f}, 0.0f) == doctest::Approx(kRungLadderHeight));
            CHECK(mesh.ceiling_above(inside) == doctest::Approx(kRungLadderHeight));
        } else {
            CHECK(mesh.floor_below(inside, 0.0f) == doctest::Approx(5.55f));
            CHECK(mesh.ceiling_above(inside) == doctest::Approx(8.75f));
        }
    }
    std::size_t slab = 0;
    for (const auto& t : mesh.triangles())
        if (t.flags & game::kTriangleLadderSlab) ++slab;
    CHECK(slab == 12);
}

TEST_CASE("zone ladders: with solid ladders every race climbs a ladder of rungs") {
    const assets::Zone zone = rung_ladder_zone();
    game::CollisionMesh mesh = game::CollisionMesh::build(zone);
    const MeshWorld world(mesh);
    for (const Race& r : kRaces) {
        CAPTURE(std::string(r.name));
        mesh.set_solid_ladders(false);
        const float rungs = climb(world, r);
        mesh.set_solid_ladders(true);
        const float slab = climb(world, r);
        MESSAGE(std::string(r.name), ": rungs ", rungs, ", slab ", slab);
        CHECK(slab > kRungLadderHeight - 6.0f);
        // The rungs alone (the original's collision with the stand-in radius): a gnome's sphere goes between them.
        if (r.race == 12) CHECK(rungs < 10.0f);
    }
}

TEST_CASE("zone ladders: every race climbs Upper and Lower Guk's tall ladders" * doctest::skip(!hailstone::test::game_dir())) {
    // Upper Guk's LADDER28 at (747, 258), 56 high, leaning back; Lower Guk's LADDER42 at (1128, -833), 84 high, in a
    // shaft. Without solid ladders a gnome, halfling or dwarf got about a tenth of the way up either.
    struct Tall {
        const char* zone;
        glm::vec2 at;
    };
    for (const Tall& t : {Tall{"guktop", {746.8f, 258.0f}}, Tall{"gukbottom", {1128.3f, -833.4f}}}) {
        CAPTURE(std::string(t.zone));
        const assets::Zone zone = assets::load_zone(*hailstone::test::game_dir(), t.zone);
        const auto ladders = game::zone_ladders(zone);
        const game::ZoneLadder* ladder = nullptr;
        for (const auto& l : ladders)
            if (glm::distance(glm::vec2(l.center), t.at) < 1.0f) ladder = &l;
        REQUIRE(ladder != nullptr);
        game::CollisionMesh mesh = game::CollisionMesh::build(zone);
        mesh.set_solid_ladders(true);
        const MeshWorld world(mesh);
        // Straight at its face (the model's +x is into the ladder), from the nearest floor in front at its foot.
        glm::vec3 into(ladder->transform[0]);
        into.z = 0.0f;
        into = glm::normalize(into);
        std::optional<glm::vec3> start;
        for (float back : {3.0f, 5.0f, 8.0f}) {
            const glm::vec3 p = glm::vec3(ladder->center.x, ladder->center.y, 0.0f) - into * back;
            const auto floor = mesh.floor_below({p.x, p.y, ladder->center.z + 3.0f}, 0.0f);
            if (floor && *floor > ladder->center.z - 3.0f) {
                start = glm::vec3(p.x, p.y, *floor);
                break;
            }
        }
        REQUIRE(start);
        const glm::vec3 from = *start;
        for (const Race& r : kRaces) {
            CAPTURE(std::string(r.name));
            CHECK(climb(world, r, from, std::atan2(into.y, into.x), &zone.regions) > 0.9f * ladder->ladder.height);
        }
    }
}

TEST_CASE("zone ladders: hailstone.cfg [movement] solid_ladders") {
    CHECK(plugin::Rules{}.solid_ladders);
    CHECK(plugin::parse_server_cfg("").solid_ladders);
    std::vector<std::string> warnings;
    CHECK_FALSE(plugin::parse_server_cfg("[movement]\nsolid_ladders = off\n", &warnings).solid_ladders);
    CHECK(warnings.empty());
    CHECK(plugin::parse_server_cfg("[movement]\nsolid_ladders = on\n").solid_ladders);
}
