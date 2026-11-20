#include "net/connection/game_connection.h"

#include <algorithm>
#include <chrono>

#include "core/log.h"

namespace hailstone::net {
namespace {
std::size_t lane_index(game::RewardLane lane) { return lane == game::RewardLane::Pending ? 0 : 1; }
bool rate_limited(const std::optional<std::int64_t>& last, std::int64_t now, std::int64_t interval) {
    return last && now >= *last && now - *last < interval;
}
} // namespace

std::int64_t GameConnection::reward_now_ms() const {
    const auto at = options_.zone_protocol.session_config.now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count();
}

bool GameConnection::reward_request_ready(std::string* why) const {
    // The native manager senders check their live connection and received choices, not HP/death.
    // Eligibility and delivery remain the server's decision, including while a respawn is shown.
    if (state_ == ConnectionState::InZone && !zone_change_pending_ && zone_endpoint_ &&
        zone_endpoint_->state() == SessionState::Connected) return true;
    if (why) *why = "Reward requests require a connection to the current zone.";
    return false;
}
void GameConnection::reset_rewards() {
    rewards_.reset();
    reward_view_requested_ms_ = {};
    reward_inspect_requested_ms_ = {};
}
void GameConnection::show_reward_selection(bool show) {
    if (show) {
        rewards_.show(game::RewardLane::Pending);
        request_pending_rewards(); // native explicit open calls PendingRewardMgr::RequestPendingRewards
    } else rewards_.close_all();
}
void GameConnection::close_reward_selection(game::RewardLane lane) { rewards_.close(lane); }

bool GameConnection::request_pending_rewards() {
    if (!reward_request_ready()) return false;
    const auto now = reward_now_ms();
    if (rate_limited(reward_view_requested_ms_[0], now, 500)) return false;
    // Reserve the rate gate before outgoing hooks, which may call the UI again.
    const auto old = reward_view_requested_ms_[0];
    const auto revision = rewards_.lane(game::RewardLane::Pending).revision;
    reward_view_requested_ms_[0] = now;
    const auto current = [&] { return reward_request_ready() && rewards_.lane(game::RewardLane::Pending).revision == revision; };
    if (!send_zone_if_current(messages::outgoing(messages::GameOpcode::AchievementReward, RewardIntent{}), current)) {
        if (current() && reward_view_requested_ms_[0] == now) reward_view_requested_ms_[0] = old;
        return false;
    }
    rewards_.show(game::RewardLane::Pending);
    return true;
}
bool GameConnection::preview_task_reward(std::uint32_t task_id) {
    if (!reward_request_ready() || !task_id) return false;
    const auto eligible = [&] {
        const bool active = std::any_of(tasks_.tasks.begin(), tasks_.tasks.end(),
            [task_id](const game::Task& task) { return task.task_id == task_id && task.reward_set; });
        const bool offered = tasks_.offers && std::any_of(tasks_.offers->offers.begin(), tasks_.offers->offers.end(),
            [task_id](const game::TaskOffer& task) { return task.task_id == task_id && task.reward_preview; });
        return active || offered;
    };
    if (!eligible()) return false;
    const auto now = reward_now_ms();
    if (rate_limited(reward_view_requested_ms_[1], now, 500)) return false;
    const auto old = reward_view_requested_ms_[1];
    const auto revision = rewards_.lane(game::RewardLane::Preview).revision;
    reward_view_requested_ms_[1] = now;
    const auto current = [&] { return reward_request_ready() && eligible() &&
        rewards_.lane(game::RewardLane::Preview).revision == revision; };
    if (!send_zone_if_current(messages::outgoing(messages::GameOpcode::RewardSelection,
        RewardIntent{RewardIntent::Kind::PreviewTask, RewardLane::Preview, task_id}), current)) {
        if (current() && reward_view_requested_ms_[1] == now) reward_view_requested_ms_[1] = old;
        return false;
    }
    rewards_.show(game::RewardLane::Preview);
    return true;
}
bool GameConnection::reward_claim(std::uint64_t revision, std::uint64_t token, std::uint32_t option,
                                std::string* why) {
    if (!reward_request_ready(why)) return false;
    const auto request = rewards_.begin_claim(revision, token, option, reward_now_ms(), why);
    if (!request) return false;
    const auto current = [&] { return reward_request_ready() && rewards_.claim_current(*request); };
    if (send_zone_if_current(messages::outgoing(messages::GameOpcode::AchievementReward,
        RewardIntent{RewardIntent::Kind::Claim, RewardLane::Pending, 0, request->pending_id, request->set_id, request->wire_option_id}), current)) return true;
    rewards_.cancel_unsent_claim(*request);
    if (why) *why = "The reward choice was not sent.";
    return false;
}
bool GameConnection::reward_inspect(game::RewardLane lane, std::uint64_t revision, std::uint64_t token,
    std::uint32_t option, std::uint32_t entry, std::uint32_t item, std::string* why) {
    if (!reward_request_ready(why)) return false;
    const auto now = reward_now_ms();
    const auto i = lane_index(lane);
    if (rate_limited(reward_inspect_requested_ms_[i], now, 100)) {
        if (why) *why = "Please wait briefly before inspecting another reward item.";
        return false;
    }
    const auto request = rewards_.begin_inspect(lane, revision, token, option, entry, item, now, why);
    if (!request) return false;
    const auto old = reward_inspect_requested_ms_[i];
    reward_inspect_requested_ms_[i] = now;
    const auto current = [&] { return reward_request_ready() && rewards_.inspect_current(*request); };
    if (send_zone_if_current(messages::outgoing(lane == RewardLane::Pending ? messages::GameOpcode::AchievementReward : messages::GameOpcode::RewardSelection,
        RewardIntent{RewardIntent::Kind::Inspect, lane, 0, 0, request->set_id, option, entry, item}), current)) return true;
    rewards_.cancel_unsent_inspect(*request);
    if (reward_inspect_requested_ms_[i] == now) reward_inspect_requested_ms_[i] = old;
    if (why) *why = "The item inspection was not sent.";
    return false;
}
bool GameConnection::handle_rewards(const messages::GameMessage& message) {
    using messages::GameOpcode;
    if (message.opcode != GameOpcode::AchievementReward && message.opcode != GameOpcode::RewardSelection) return false;
    if (state_ != ConnectionState::InZone && state_ != ConnectionState::ZoneEntering) return true;
    if (const auto* decoded = messages::message_get<RewardPacket>(message)) {
        if (auto item = rewards_.receive(*decoded)) {
            item_displays_.push_back(std::move(*item));
            if (item_displays_.size() > 16) item_displays_.pop_front();
        }
    }
    return true;
}

} // namespace hailstone::net
