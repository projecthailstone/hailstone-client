// Boats: OP_ControlBoat.
#include "core/log.h"
#include "net/connection/game_connection.h"
#include "game/boats.h"
#include "game/coords.h"
#include "game/race_models.h"

namespace hailstone::net {

namespace {
constexpr std::uint32_t kStrHaveTheHelm = 1407; // 0x57f
} // namespace

void GameConnection::handle_control_boat(const BoatControl& control) {
    // The codec has decoded the protocol's helm-control packet.
    if (!control.take_helm || !world_.spawns.count(control.spawn_id)) return;
    const auto id = control.spawn_id;
    controlled_boat_ = world_.driven_id = id;
    helm_ = {};
    chat(ChatEvent::Kind::System, strings_.format(kStrHaveTheHelm, {}));
    log::info("controlling boat {}", id);
}

void GameConnection::set_standing_on(std::uint32_t vehicle_id) {
    standing_on_ = vehicle_id;
    // ProcessControls: CP has vf58 but is not the local player's +0x150.
    if (controlled_boat_ && controlled_boat_ != standing_on_) release_boat();
}

bool GameConnection::right_click_boat(std::uint32_t id) {
    auto it = world_.spawns.find(id);
    if (it == world_.spawns.end()) return false;
    const game::Spawn& s = it->second;
    if (!s.is_npc || s.is_corpse || !game::is_small_boat_race(s.race, s.gender)) return false;
    if (state_ != ConnectionState::InZone) return true;
    if (controlled_boat_ == id) {
        release_boat();
    } else if (standing_on_ == id) {
        send_zone(messages::outgoing(messages::GameOpcode::ControlBoat, BoatControl{id, true}));
        log::info("boat: asking for the helm of {}", id);
    }
    return true;
}

void GameConnection::release_boat() {
    // Letting go (right click @00537325, or ProcessControls): OP_ControlBoat {id, 0}, control back to the player.
    if (!controlled_boat_ || state_ != ConnectionState::InZone) return;
    send_zone(messages::outgoing(messages::GameOpcode::ControlBoat, BoatControl{controlled_boat_, false}));
    log::info("boat: letting go of {}", controlled_boat_);
    if (helm_moving_) {
        // Stopped where it is: the last update says so (EQEmu keeps the boat where the helm left it).
        helm_ = {};
        steer_boat(false, false, false, false, 0.0f, {});
    }
    controlled_boat_ = world_.driven_id = 0;
}

void GameConnection::steer_boat(bool forward, bool back, bool left, bool right, float dt,
                                const game::HelmClearFn& clear) {
    auto it = controlled_boat_ ? world_.spawns.find(controlled_boat_) : world_.spawns.end();
    if (it == world_.spawns.end() || state_ != ConnectionState::InZone) return;
    game::Spawn& s = it->second;
    const game::BoatPose from = game::drawn_pose(s);
    const game::BoatPose to = game::helm_resolve(helm_, from, game::steer_boat(helm_, from, forward, back, left, right, dt),
                                                 clear);
    const glm::vec3 per_tick = (to.position - from.position) * (dt > 0.0f ? 0.05f / dt : 0.0f);
    s.position = s.display_position = to.position;
    s.heading = s.display_heading = to.heading;
    s.display_error = glm::vec3(0.0f);
    s.display_heading_error = 0.0f;
    s.display_valid = true;
    s.velocity = per_tick;
    s.speed_heading = helm_.turn * 0.05f;
    const bool moving = helm_.speed != 0.0f || helm_.turn != 0.0f;
    const auto now = std::chrono::steady_clock::now();
    // While it moves, ten updates a second (others extrapolate between them from the deltas); one more when it stops.
    if (!(moving ? now - last_helm_send_ >= std::chrono::milliseconds(100) : helm_moving_)) return;
    ClientPositionUpdate u;
    u.sequence = position_tracker_.sequence();
    u.spawn_id = static_cast<std::uint16_t>(s.id);
    const glm::vec3 p = game::world_to_server(s.position), v = game::world_to_server(per_tick);
    u.motion.x = p.x;
    u.motion.y = p.y;
    u.motion.z = p.z;
    u.motion.heading = s.heading;
    u.motion.delta_x = v.x;
    u.motion.delta_y = v.y;
    u.motion.delta_z = v.z;
    u.motion.delta_heading = s.speed_heading;
    send_zone(messages::outgoing(messages::GameOpcode::ClientUpdate, u,
        {messages::Reliability::Unreliable}));
    last_helm_send_ = now;
    helm_moving_ = moving;
}

} // namespace hailstone::net
