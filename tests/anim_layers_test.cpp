#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "assets/character.h"
#include "core/worker_pool.h"
#include "game/anim_layers.h"
#include "game/world_state.h"
#include "render/actor_layers.h"
#include "render/actor_renderer.h"

using namespace hailstone;
using namespace hailstone::game;
using hailstone::render::ActorLayers;
using hailstone::render::LayerRequest;

TEST_CASE("bone groups: the classic skeleton's callback") {
    CHECK(classic_bone_group("HUMCH_TRACK") == kUpperBones);
    CHECK(classic_bone_group("HUMNE_TRACK") == kUpperBones);
    CHECK(classic_bone_group("HUMHE_TRACK") == kUpperBones);
    CHECK(classic_bone_group("HUMHEAD_POINT_TRACK") == kUpperBones);
    CHECK(classic_bone_group("HUMBI_L_TRACK") == kUpperBones);
    CHECK(classic_bone_group("HUMFO_R_TRACK") == kUpperBones);
    CHECK(classic_bone_group("HUMFI_R2_TRACK") == kUpperBones);
    CHECK(classic_bone_group("HUMFA_DAG") == kUpperBones);
    CHECK(classic_bone_group("HUMHI_DAG") == kUpperBones);
    CHECK(classic_bone_group("HUMCHCHEST_DAG") == kLowerBones);
    CHECK(classic_bone_group("HUM_TRACK") == kLowerBones);
    CHECK(classic_bone_group("HUMPE_TRACK") == kLowerBones);
    CHECK(classic_bone_group("HUMTH_R_TRACK") == kLowerBones);
    CHECK(classic_bone_group("HUMR_POINT_TRACK") == kLowerBones); // not named: the lower group
    CHECK(classic_bone_group("HUMSHIELD_POINT_TRACK") == kLowerBones);
    CHECK(classic_bone_group("HU") == kLowerBones);
}

TEST_CASE("bone groups: the new-style skeleton's callback") {
    CHECK(new_style_bone_group("CHEST_CHEST01") == kLowerBones);
    CHECK(new_style_bone_group("CHEST_CHEST02") == kUpperBones);
    CHECK(new_style_bone_group("CHEST03_TORCH01") == kUpperBones);
    CHECK(new_style_bone_group("NECK_NECK") == kUpperBones);
    CHECK(new_style_bone_group("HEAD_HAIR01") == kUpperBones);
    CHECK(new_style_bone_group("ARML_HAND") == kUpperBones);
    CHECK(new_style_bone_group("ROOT_BONE") == kLowerBones);
    CHECK(new_style_bone_group("PELV") == kLowerBones);
    CHECK(new_style_bone_group("LEGL_THIGH") == kLowerBones);
}

TEST_CASE("classic layering tables") {
    CHECK_FALSE(classic_action_over_legs(1)); // C01 kick
    CHECK(classic_action_over_legs(2));
    CHECK(classic_action_over_legs(5)); // C05
    CHECK(classic_action_over_legs(9));
    CHECK_FALSE(classic_action_over_legs(10));
    CHECK_FALSE(classic_action_over_legs(11));
    CHECK(classic_action_over_legs(0x2b)); // casting
    CHECK_FALSE(classic_action_over_legs(0x2d));
    CHECK(classic_legs_under_action(0x11));
    CHECK(classic_legs_under_action(0x12));
    CHECK(classic_legs_under_action(0x20));
    CHECK(classic_legs_under_action(0x4b));
    CHECK_FALSE(classic_legs_under_action(0x4d));
    CHECK(classic_legs_under_action(0x4e));
    CHECK_FALSE(classic_interruptible(5));
    CHECK(classic_interruptible(0x12));
    CHECK(classic_interruptible(0x20));
    CHECK_FALSE(classic_interruptible(0x24));

    CHECK(classic_action_upper_only(5, false, true, 0.7f));
    CHECK_FALSE(classic_action_upper_only(5, false, true, 0.0f));  // standing: the whole body
    CHECK_FALSE(classic_action_upper_only(5, false, false, 0.7f)); // not humanoid
    CHECK_FALSE(classic_action_upper_only(1, false, true, 0.7f));  // a kick
    CHECK(classic_action_upper_only(1, true, false, 0.0f));        // a rider: the legs keep the rider animation
    CHECK(classic_legs_take_motion(5, 0x12, true));
    CHECK_FALSE(classic_legs_take_motion(5, 0x20, true)); // standing waits for the action
    CHECK_FALSE(classic_legs_take_motion(1, 0x12, true));
    CHECK_FALSE(classic_legs_take_motion(5, 0x12, false));
}

TEST_CASE("new-style layer modes (0x58ac90)") {
    LayerModeInput in;
    in.wanted = 0x6a; // NRUN
    in.playing = 0x6a;
    CHECK(new_style_layer_mode(in) == 3); // the same sequence and column: continue
    in.playing = 0xfe;
    CHECK(new_style_layer_mode(in) == 2);
    in.playing = 0x50; // SLPR: an action (hold 0)
    CHECK(new_style_layer_mode(in) == 1);
    in.completed = true;
    CHECK(new_style_layer_mode(in) == 2);
    in.completed = false;
    in.forced = true;
    CHECK(new_style_layer_mode(in) == 2);
    in.forced = false;
    in.playing = 0x68; // WALK, looping
    CHECK(new_style_layer_mode(in) == 2);
    in.wanted = 0x6e; // STND (priority 25) under a walk (100): a movement sequence gives way
    CHECK(new_style_layer_mode(in) == 2);
    in.playing = 0x6f; // IDLE (50, not movement) keeps playing against STND
    CHECK(new_style_layer_mode(in) == 1);
    in.playing = 0x79; // sitting down is not cut by a stand
    CHECK(new_style_layer_mode(in) == 1);
    in.playing = 0x92; // CRMP: hold 1, but a living PC holds it as 3
    in.stand_state = 0x73;
    CHECK(new_style_layer_mode(in) == 0);
    in.group = false;
    CHECK(new_style_layer_mode(in) == 3);
}

namespace {
assets::AnimationClip clip(std::uint32_t frames) {
    assets::AnimationClip c;
    c.frame_count = frames;
    c.frame_ms = 100;
    return c;
}
} // namespace

TEST_CASE("classic: an attack while running plays over the legs") {
    auto run = clip(10), attack = clip(8), stand = clip(10);
    ActorLayers l;
    LayerRequest req;
    req.base = &run;
    req.base_anim = 0x12;
    req.humanoid = true;
    req.speed_run = 0.7f;
    for (int i = 0; i < 3; ++i) l.step(req, 0.1f);
    CHECK(l.group[kLowerBones].clip == &run);
    REQUIRE(l.start_action(&attack, 5, 1.0f, req));
    CHECK_FALSE(l.action_on_legs);
    l.step(req, 0.1f);
    CHECK(l.group[kUpperBones].clip == &attack);
    CHECK(l.group[kLowerBones].clip == &run);
    CHECK(l.split());
    // Stopping under the attack: the legs keep their clip until it ends (P01 waits).
    req.base = &stand;
    req.base_anim = 0x20;
    req.speed_run = 0.0f;
    l.step(req, 0.1f);
    CHECK(l.group[kLowerBones].clip == &run);
    for (int i = 0; i < 8; ++i) l.step(req, 0.1f);
    CHECK(l.action == nullptr);
    CHECK(l.group[kUpperBones].clip == &stand);
    CHECK(l.group[kLowerBones].clip == &stand);
}

TEST_CASE("classic: running under a whole-body attack takes the legs (humanoids)") {
    auto run = clip(10), attack = clip(8), stand = clip(10);
    ActorLayers l;
    LayerRequest req;
    req.base = &stand;
    req.base_anim = 0x20;
    req.humanoid = true;
    l.step(req, 0.1f);
    REQUIRE(l.start_action(&attack, 5, 1.0f, req));
    CHECK(l.action_on_legs);
    l.step(req, 0.1f);
    CHECK(l.group[kLowerBones].clip == &attack);
    req.base = &run;
    req.base_anim = 0x12;
    req.speed_run = 0.7f;
    l.step(req, 0.1f);
    CHECK(l.group[kLowerBones].clip == &run);
    CHECK(l.group[kUpperBones].clip == &attack);
    // Not a humanoid: the action holds the whole body until it ends.
    ActorLayers m;
    req.humanoid = false;
    req.base = &stand;
    req.base_anim = 0x20;
    req.speed_run = 0.0f;
    m.step(req, 0.1f);
    REQUIRE(m.start_action(&attack, 5, 1.0f, req));
    req.base = &run;
    req.base_anim = 0x12;
    req.speed_run = 0.7f;
    m.step(req, 0.1f);
    CHECK(m.group[kLowerBones].clip == &attack);
    for (int i = 0; i < 8; ++i) m.step(req, 0.1f);
    CHECK(m.group[kLowerBones].clip == &run);
    CHECK(m.group[kUpperBones].clip == &run);
    // The upper body comes back in step with the legs (SyncToBoneGroup).
    CHECK(m.group[kUpperBones].time == doctest::Approx(m.group[kLowerBones].time));
}

TEST_CASE("classic: a rider acts with the upper body, the legs keep the rider animation") {
    auto rider = clip(10), cast = clip(12);
    ActorLayers l;
    LayerRequest req;
    req.base = &rider;
    req.base_anim = 0x4a;
    req.mounted = true;
    l.step(req, 0.1f);
    REQUIRE(l.start_action(&cast, 0x2b, 1.0f, req));
    l.step(req, 0.1f);
    CHECK(l.group[kLowerBones].clip == &rider);
    CHECK(l.group[kUpperBones].clip == &cast);
}

TEST_CASE("new-style: an attack over a walk; a stand cuts it") {
    auto walk = clip(10), slash = clip(8), stand = clip(10), kick = clip(8);
    ActorLayers l;
    LayerRequest req;
    req.new_style = true;
    req.base = &walk;
    req.base_anim = 0x68;
    req.blend_ms = 100.0f;
    l.step(req, 0.1f);
    REQUIRE(l.start_action(&slash, 0x50, 1.0f, req)); // SLPR: layer kind 1
    CHECK_FALSE(l.action_on_legs);
    l.step(req, 0.1f);
    CHECK(l.group[kUpperBones].clip == &slash);
    CHECK(l.group[kLowerBones].clip == &walk);
    req.base = &stand;
    req.base_anim = 0x6e;
    l.step(req, 0.1f);
    CHECK(l.action == nullptr);
    CHECK(l.group[kUpperBones].clip == &stand);
    CHECK(l.group[kLowerBones].clip == &stand);
    // A kick (kind 0) is a whole-body sequence.
    REQUIRE(l.start_action(&kick, 0x5f, 1.0f, req));
    CHECK(l.action_on_legs);
    // A rider plays only kind 1 sequences, and not over the unfinished kick (its mode is 1: queued, never played).
    req.mounted = true;
    CHECK_FALSE(l.start_action(&kick, 0x5f, 1.0f, req));
    CHECK_FALSE(l.start_action(&slash, 0x50, 1.0f, req));
    for (int i = 0; i < 9; ++i) l.step(req, 0.1f);
    CHECK(l.start_action(&slash, 0x50, 1.0f, req));
    CHECK_FALSE(l.action_on_legs);
    // The same sequence asked again keeps playing (mode 3), even once it is through.
    l.step(req, 0.1f);
    const float t = l.group[kUpperBones].time;
    CHECK_FALSE(l.start_action(&slash, 0x50, 1.0f, req));
    CHECK(l.group[kUpperBones].time == doctest::Approx(t));
}

TEST_CASE("New-style layers distinguish the same sequence in another style column") {
    auto stand = clip(10), one_hand = clip(2), mounted = clip(2);
    ActorLayers l;
    LayerRequest req;
    req.new_style = true;
    req.base = &stand;
    req.base_anim = 0x6e;
    req.base_column = 5;
    req.mounted = true;
    l.step(req, 0.1f);
    CHECK(l.group[kLowerBones].column == 5);
    req.action_column = 1;
    REQUIRE(l.start_action(&one_hand, 0x50, 1.0f, req));
    CHECK(l.group[kUpperBones].column == 1);
    CHECK(l.group[kLowerBones].clip == &stand);
    l.step(req, 0.2f);
    CHECK_FALSE(l.start_action(&one_hand, 0x50, 1.0f, req)); // same sequence/style keeps its phase
    req.action_column = 5;
    CHECK(l.start_action(&mounted, 0x50, 1.0f, req)); // finished sequence, new style can start
    CHECK(l.group[kUpperBones].clip == &mounted);
    CHECK(l.group[kUpperBones].column == 5);
    CHECK(l.group[kLowerBones].clip == &stand);
    CHECK(l.group[kLowerBones].column == 5);
}

TEST_CASE("classic: an action asked during an unfinished one follows it (+0x10c4)") {
    using hailstone::game::classic_may_interrupt;
    CHECK_FALSE(classic_may_interrupt(5, 5));   // never over itself
    CHECK_FALSE(classic_may_interrupt(2, 5));   // C05 is not interruptible
    CHECK(classic_may_interrupt(2, 0x20));      // P01 is
    CHECK(classic_may_interrupt(0x10, 5));      // D05 always
    CHECK_FALSE(classic_may_interrupt(0x20, 0x1a)); // a stand over a blend animation
    CHECK(classic_may_interrupt(0x1a, 5));
    auto stand = clip(10), slash = clip(8), bash = clip(6), run = clip(10);
    ActorLayers l;
    LayerRequest req;
    req.base = &stand;
    req.base_anim = 0x20;
    req.humanoid = true;
    l.step(req, 0.1f);
    REQUIRE(l.start_action(&slash, 5, 1.0f, req));
    l.step(req, 0.3f);
    CHECK_FALSE(l.start_action(&bash, 2, 1.0f, req)); // waits
    CHECK(l.follow_up == 2);
    CHECK(l.group[kUpperBones].clip == &slash);
    l.step(req, 0.6f);
    l.step(req, 0.1f); // the slash is through: the stand request plays the follow-up
    CHECK(l.group[kUpperBones].clip == &bash);
    CHECK(l.follow_up == 0x20);
    for (int i = 0; i < 7; ++i) l.step(req, 0.1f);
    CHECK(l.group[kUpperBones].clip == &stand);
    // A movement request during the action drops the follow-up.
    REQUIRE(l.start_action(&slash, 5, 1.0f, req));
    l.step(req, 0.1f);
    CHECK_FALSE(l.start_action(&bash, 2, 1.0f, req));
    req.base = &run;
    req.base_anim = 0x12;
    req.speed_run = 0.7f;
    l.step(req, 0.1f);
    CHECK(l.follow_up == 0x20);
    // The same action asked again while it plays follows it, and does not restart it now.
    ActorLayers m;
    req.base = &stand;
    req.base_anim = 0x20;
    req.speed_run = 0.0f;
    m.step(req, 0.1f);
    REQUIRE(m.start_action(&slash, 5, 1.0f, req));
    m.step(req, 0.4f);
    CHECK_FALSE(m.start_action(&slash, 5, 1.0f, req));
    CHECK(m.group[kUpperBones].time == doctest::Approx(0.4f));
}

TEST_CASE("classic: the follow-up leaves +0x10c4 as the stand request says; legs taking a run") {
    auto stand = clip(10), slash = clip(8), bash = clip(6), run = clip(10);
    ActorLayers l;
    LayerRequest req;
    req.base = &stand;
    req.base_anim = 0x20;
    req.humanoid = true;
    l.step(req, 0.1f);
    REQUIRE(l.start_action(&slash, 5, 1.0f, req));
    l.step(req, 0.3f);
    CHECK_FALSE(l.start_action(&bash, 2, 1.0f, req));
    // The stand request plays the follow-up; +0x10c4 becomes what game::choose_spawn_animation said (0x47 here).
    req.follow_up_after = 0x47;
    CHECK_FALSE(l.step(req, 0.6f));
    CHECK(l.step(req, 0.1f));
    CHECK(l.group[kUpperBones].clip == &bash);
    CHECK(l.follow_up == 0x47);
    CHECK(l.follow_clip == nullptr);
    CHECK_FALSE(l.legs_took_motion);
    // A run under an upper-body action takes the legs: the run is then the animation playing.
    ActorLayers m;
    req.follow_up_after = 0x20;
    m.step(req, 0.1f);
    req.speed_run = 0.7f;
    REQUIRE(m.start_action(&slash, 5, 1.0f, req));
    CHECK_FALSE(m.legs_took_motion);
    req.base = &run;
    req.base_anim = 0x12;
    m.step(req, 0.1f);
    CHECK(m.legs_took_motion);
    CHECK(m.action == &slash);
}

TEST_CASE("cross-fades are per bone group, linear over the blend time") {
    auto a = clip(10), b = clip(10);
    render::GroupTrack g;
    CHECK(g.start(&a, 0x11, 1.0f, true, false, 0.1f)); // nothing playing: switched at once
    CHECK(g.blend == 1.0f);
    CHECK_FALSE(g.start(&a, 0x11, 2.0f, true, false, 0.1f)); // the same clip keeps playing
    CHECK(g.rate == 2.0f);
    CHECK(g.start(&b, 0x12, 1.0f, true, false, 0.1f));
    CHECK(g.previous == &a);
    g.advance(0.05f);
    CHECK(g.blend == doctest::Approx(0.5f));
    g.advance(0.05f);
    CHECK(g.blend == doctest::Approx(1.0f));
    CHECK(g.previous == nullptr);
    CHECK(g.completed() == false);
    g.advance(0.9f);
    CHECK(g.completed());
}

TEST_CASE("an upper-body clip poses only the upper group's bones") {
    assets::CharacterModel m;
    m.code = "HUM";
    m.bone_names = {"HUM_TRACK", "HUMPE_TRACK", "HUMCH_TRACK"};
    m.parents = {-1, 0, 1};
    m.order = {0, 1, 2};
    auto pose = [](float x) {
        assets::AnimationClip c;
        c.frame_count = 1;
        c.tracks.resize(3);
        for (auto& t : c.tracks) {
            assets::BonePose p;
            p.translation = glm::vec3(x, 0.0f, 0.0f);
            t.push_back(p);
        }
        return c;
    };
    auto lower = pose(1.0f), upper = pose(10.0f);
    std::vector<std::uint8_t> groups;
    for (const auto& n : m.bone_names) groups.push_back(classic_bone_group(n));
    render::ActorDraw a;
    a.model = &m;
    a.clip = &lower;
    a.upper_clip = &upper;
    a.bone_groups = &groups;
    std::vector<assets::BonePose> local, previous;
    std::vector<glm::mat4> world;
    render::ActorRenderer::pose_bones(a, local, previous, world);
    REQUIRE(local.size() == 3);
    CHECK(local[0].translation.x == doctest::Approx(1.0f));
    CHECK(local[1].translation.x == doctest::Approx(1.0f));
    CHECK(local[2].translation.x == doctest::Approx(10.0f));
}

namespace {

// A three-bone chain whose root moves by `x`, with an inverse bind on its last bone.
struct PoseFixture {
    assets::CharacterModel model;
    assets::AnimationClip clip;
    PoseFixture() {
        model.parents = {-1, 0, 1};
        model.order = {0, 1, 2};
        model.inverse_bind.assign(3, glm::mat4(1.0f));
        model.inverse_bind[2][3] = {0, 0, -4, 1};
        clip.frame_count = 1;
        clip.tracks.resize(3);
        for (std::size_t b = 0; b < 3; ++b) {
            assets::BonePose p;
            p.translation = glm::vec3(1.0f, static_cast<float>(b), 2.0f);
            p.rotation = glm::angleAxis(0.3f * static_cast<float>(b + 1), glm::vec3(0, 0, 1));
            clip.tracks[b].push_back(p);
        }
    }
};

bool near_mat(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > 1e-4f) return false;
    return true;
}

} // namespace

TEST_CASE("Actor posing on the workers writes each instance's bones into its own slice, as posed one by one") {
    PoseFixture f;
    assets::CharacterMesh plain, skinned, broken;
    skinned.skin_bindings.push_back({2, glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 3))});
    broken.skin_bindings.push_back({9, glm::mat4(1.0f)}); // a host bone past the skeleton: not posed
    std::vector<render::ActorDraw> actors(11);
    for (std::size_t i = 0; i < actors.size(); ++i) {
        actors[i].model = &f.model;
        actors[i].clip = &f.clip;
        actors[i].position = {10.0f * static_cast<float>(i), 5.0f, 1.0f};
        actors[i].yaw = 0.3f * static_cast<float>(i);
        actors[i].scale = 1.0f + 0.1f * static_cast<float>(i);
    }
    // As prepare plans them: each its slice, one after another, in the actors' order.
    auto plan = [&](std::vector<render::ActorRenderer::PoseJob>& jobs) {
        jobs.clear();
        std::uint32_t used = 0;
        for (std::size_t i = 0; i < actors.size(); ++i) {
            const assets::CharacterMesh* mesh = i == 8 ? &broken : i % 3 == 1 ? &skinned : &plain;
            auto count = static_cast<std::uint32_t>(3 + mesh->skin_bindings.size());
            if (i == 4) ++count; // planned for more bones than it makes: not posed
            jobs.push_back({i, mesh, used, count, true});
            used += count;
        }
        return used;
    };
    // The one-by-one pose each slice must hold.
    auto expected = [&](const render::ActorRenderer::PoseJob& job) {
        std::vector<assets::BonePose> local, previous;
        std::vector<glm::mat4> world;
        render::ActorRenderer::pose_bones(actors[job.actor], local, previous, world);
        f.model.skin(world);
        REQUIRE(job.mesh->append_skin_bones(world));
        for (auto& w : world) w = render::ActorRenderer::draw_matrix(actors[job.actor]) * w;
        return world;
    };
    const glm::mat4 untouched(-7.0f);
    for (unsigned workers : {0u, 3u}) {
        CAPTURE(workers);
        core::WorkerPool pool(workers);
        core::set_workers(&pool);
        std::vector<render::ActorRenderer::PoseJob> jobs;
        std::vector<glm::mat4> bones(plan(jobs), untouched);
        render::ActorRenderer::pose_instances(actors, jobs, bones);
        core::set_workers(nullptr);
        for (const auto& job : jobs) {
            CAPTURE(job.actor);
            const bool fails = job.actor == 4 || job.actor == 8;
            CHECK(job.posed == !fails);
            if (fails) {
                for (std::uint32_t b = 0; b < job.count; ++b) CHECK(bones[job.first + b] == untouched);
                continue;
            }
            const auto want = expected(job);
            REQUIRE(want.size() == job.count);
            for (std::uint32_t b = 0; b < job.count; ++b) CHECK(near_mat(bones[job.first + b], want[b]));
        }
    }
    // A slice past the end of the frame's bones is not written.
    std::vector<render::ActorRenderer::PoseJob> past{{0, &plain, 2, 3, true}};
    std::vector<glm::mat4> small(4, untouched);
    render::ActorRenderer::pose_instances(actors, past, small);
    CHECK_FALSE(past[0].posed);
    for (const auto& m : small) CHECK(m == untouched);
}

TEST_CASE("A drawn actor's bones are posed once and kept until the pose is marked stale") {
    PoseFixture f;
    render::ActorDraw a;
    a.model = &f.model;
    a.clip = &f.clip;
    a.position = {10, 20, 30};
    a.scale = 2.0f;
    render::ActorRenderer::BonePoseCache cache;
    std::vector<assets::BonePose> local, previous;
    glm::mat4 world(0.0f), direct(0.0f);
    for (int b = 0; b < 3; ++b) {
        REQUIRE(render::ActorRenderer::cached_bone_world(a, b, cache, local, previous, world));
        REQUIRE(render::ActorRenderer::bone_world(a, b, direct));
        CHECK(near_mat(world, direct));
    }
    CHECK(cache.valid);
    // Kept: the instance moving does not change what the cache gives until it is marked stale.
    const glm::mat4 kept = world;
    a.position.x += 50.0f;
    REQUIRE(render::ActorRenderer::cached_bone_world(a, 2, cache, local, previous, world));
    CHECK(near_mat(world, kept));
    cache.valid = false;
    REQUIRE(render::ActorRenderer::cached_bone_world(a, 2, cache, local, previous, world));
    REQUIRE(render::ActorRenderer::bone_world(a, 2, direct));
    CHECK(near_mat(world, direct));
    CHECK_FALSE(near_mat(world, kept));
    // As bone_world: a bone past the skeleton, a negative one or no clip give nothing.
    CHECK_FALSE(render::ActorRenderer::cached_bone_world(a, 3, cache, local, previous, world));
    CHECK_FALSE(render::ActorRenderer::cached_bone_world(a, -1, cache, local, previous, world));
    a.clip = nullptr;
    CHECK_FALSE(render::ActorRenderer::cached_bone_world(a, 0, cache, local, previous, world));
}

TEST_CASE("Actor role zero position uses the posed name bone before inverse bind") {
    assets::CharacterModel model;
    model.name_bone = 1;
    model.parents = {-1, 0};
    model.order = {0, 1};
    model.inverse_bind.assign(2, glm::mat4(1.0f));
    model.inverse_bind[1][3] = {0, 0, -5, 1};
    assets::AnimationClip clip;
    clip.tracks.resize(2);
    clip.tracks[0].push_back(assets::BonePose{});
    assets::BonePose head;
    head.translation = {0, 0, 5};
    clip.tracks[1].push_back(head);
    render::ActorDraw actor;
    actor.model = &model;
    actor.clip = &clip;
    actor.position = {10, 20, 30};
    actor.scale = 2;
    glm::vec3 position(0.0f);
    REQUIRE(render::ActorRenderer::name_position(actor, position));
    CHECK(position.z == doctest::Approx(40.0f));
    model.name_bone = -1;
    position = {7, 8, 9};
    CHECK_FALSE(render::ActorRenderer::name_position(actor, position));
    CHECK(position == glm::vec3(7, 8, 9));
}

TEST_CASE("other spawns' water: the body and feet regions are probed with each step") {
    WorldState w;
    w.player_id = 9;
    Spawn swimmer;
    swimmer.heights.avatar_height = 3.0f;
    swimmer.update_time = 1.0;
    swimmer.fly_mode = 1;
    swimmer.position = glm::vec3(0.0f, 0.0f, 12.0f); // feet at 9: water up to z 10
    w.spawns[1] = swimmer;
    Spawn stale = swimmer; // no update for more than 12 s: not stepped, its levels kept
    stale.update_time = -20.0;
    w.spawns[2] = stale;
    w.spawns[9] = swimmer; // the local player: its controller's
    auto water = [](const glm::vec3& p) { return static_cast<std::uint8_t>(p.z < 10.0f ? 5 : 0); };
    w.smooth(0.016f, 2.0, {}, water);
    CHECK(w.spawns[1].body_wet == 0);
    CHECK(w.spawns[1].feet_wet == 5);
    CHECK(w.spawns[2].feet_wet == 0);
    CHECK(w.spawns[9].feet_wet == 0);
    w.spawns[1].position.z = 9.0f;
    w.spawns[1].update_time = 2.0;
    w.smooth(0.016f, 2.0, {}, water);
    CHECK(w.spawns[1].body_wet == 5);
}
