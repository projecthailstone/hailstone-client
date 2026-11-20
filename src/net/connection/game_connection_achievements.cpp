#include "net/connection/game_connection.h"

#include <algorithm>
#include <format>
#include <string_view>

#include "core/log.h"
#include "game/option_commands.h"
#include "net/messages/achievement.h"
#include "net/messages/player_commands.h"

namespace hailstone::net {
namespace {
const AchievementDefinition* own_definition(const game::Achievements& model, std::uint64_t token, std::uint32_t index) {
    if (!model.catalog() || model.catalog_token() != token || index >= model.catalog()->definitions.size() ||
        index >= model.states().size() || !model.states()[index] ||
        model.states()[index]->status == AchievementStatus::Hidden) return nullptr;
    return &model.catalog()->definitions[index];
}
std::string chat_tag(const AchievementLink& link, std::string_view name) {
    return std::string("\x12") + '3' + link.metadata + '\'' + std::string(name) + '\x12';
}
} // namespace

namespace {
constexpr std::string_view kCompareWaiting = "Waiting for the selected player's achievement comparison.";
constexpr std::string_view kCompareTimedOut =
    "The comparison timed out. Compare again once the server's late reply arrives, or after zoning.";
} // namespace

void GameConnection::reset_achievements() {
    achievements_.reset();
    achievement_view_intent_ = core::next_process_identity();
    achievement_refresh_token_.reset(); achievement_refresh_requested_ms_.reset();
    achievement_compare_target_.reset(); achievement_link_target_.reset();
    achievement_link_request_token_.reset();
    achievement_message_.clear();
}
void GameConnection::show_achievements(bool show) {
    achievements_.show(show);
    if (show) request_achievements();
    else {
        achievement_view_intent_ = core::next_process_identity();
        achievement_refresh_token_.reset();
    }
}
void GameConnection::close_achievement_detail() { achievements_.close_detail(); }
void GameConnection::reopen_achievement_detail() {
    if (state_ != ConnectionState::InZone || zone_change_pending_ || !achievements_.selection()) return;
    if (achievements_.selection()->source == game::AchievementDetailSource::Link && achievements_.detail_link())
        achievements_.open_link(*achievements_.detail_link());
    else achievements_.open_selection(achievements_.catalog_token(), achievements_.selection()->definition_index);
}
bool GameConnection::open_achievement(std::uint64_t token, std::uint32_t index) {
    return state_ == ConnectionState::InZone && !zone_change_pending_ && own_definition(achievements_, token, index) &&
        achievements_.open_selection(token, index);
}
bool GameConnection::request_achievements() {
    if (!reward_request_ready() || achievement_refresh_token_) return false;
    const auto now = reward_now_ms();
    if (achievement_refresh_requested_ms_ && now >= *achievement_refresh_requested_ms_ &&
        now - *achievement_refresh_requested_ms_ < 1000) return false;
    const auto old = achievement_refresh_requested_ms_;
    const auto request = core::next_process_identity();
    achievement_view_intent_ = request; achievement_refresh_token_ = request;
    achievement_refresh_requested_ms_ = now; achievement_refresh_sent_ms_ = now;
    achievement_message_ = "Waiting for the server's achievement catalog and progress.";
    const auto current = [&] { return reward_request_ready() && achievement_view_intent_ == request &&
        achievement_refresh_token_ == request; };
    if (current() && submit_zone_action(messages::outgoing(messages::GameOpcode::AchievementWindowRequest),
                                        "Achievement catalog refresh")) return true;
    if (achievement_refresh_token_ == request) {
        achievement_refresh_token_.reset(); achievement_refresh_requested_ms_ = old;
        achievement_message_ = "The achievement refresh was not sent.";
    }
    return false;
}
std::pair<std::uint32_t, std::uint64_t> GameConnection::achievement_target() const {
    const auto found = world_.spawns.find(world_.target_id);
    if (found == world_.spawns.end() || found->second.is_npc || found->second.is_corpse ||
        !world_.player() || !(distance_to(world_.target_id) <= 20.0f)) return {};
    return {found->first, found->second.incarnation};
}
bool GameConnection::compare_achievements() {
    if (!reward_request_ready() || !achievements_.catalog()) return false;
    const auto target = achievement_target();
    if (!target.first || !target.second) return false;
    const auto found = world_.spawns.find(target.first);
    const auto request = achievements_.reserve_compare(target.first, target.second, found->second.name, reward_now_ms());
    if (!request) return false;
    const auto current = [&] {
        const auto at = achievement_target();
        return reward_request_ready() && achievements_.compare_current(*request, at.first, at.second);
    };
    if (current() && submit_zone_action(messages::outgoing(messages::GameOpcode::AchievementCompare,
                                                            AchievementCompareRequest{target.first, world_.player_id}),
                                         "Compare achievements")) {
        achievement_compare_target_ = target;
        achievement_message_ = std::string(kCompareWaiting);
        return true;
    }
    achievements_.cancel_unsent_compare(*request);
    return false;
}
void GameConnection::clear_achievement_comparison() {
    achievements_.retire_compare(); achievement_compare_target_.reset();
}
bool GameConnection::inspect_achievement_link(std::string_view metadata) {
    if (!reward_request_ready() || !achievements_.catalog()) return false;
    auto link = parse_achievement_link(metadata, *achievements_.catalog());
    if (!link) return false;
    const auto peer = std::find_if(world_.spawns.begin(), world_.spawns.end(), [&](const auto& entry) {
        return !entry.second.is_npc && !entry.second.is_corpse && game::same_no_case(entry.second.name, link->player_name);
    });
    if (!achievements_.open_link(*link)) return false;
    // A new displayed snapshot has its own ownership. An older retired
    // request remains in the model only to drain its reply, not this view.
    achievement_link_target_.reset(); achievement_link_request_token_.reset();
    // A chat link is a received snapshot. The native client can display it even
    // when its owner has left; only a local named player can answer an inspection.
    if (peer == world_.spawns.end()) {
        achievement_message_ = "Showing the linked snapshot; this player is not in the current zone.";
        return true;
    }
    const auto owner_id = peer->first; const auto owner_incarnation = peer->second.incarnation;
    const auto request = achievements_.reserve_link(std::move(*link), reward_now_ms());
    if (!request) {
        achievement_message_ = "Showing the linked snapshot while an earlier inspection reply drains.";
        return true;
    }
    achievement_link_target_ = {owner_id, owner_incarnation};
    achievement_link_request_token_ = request->token;
    const auto current = [&] {
        const auto at = world_.spawns.find(owner_id);
        return reward_request_ready() && achievements_.link_current(*request) && at != world_.spawns.end() &&
            at->second.incarnation == owner_incarnation && game::same_no_case(at->second.name, request->link.player_name);
    };
    if (current() && submit_zone_action(messages::outgoing(messages::GameOpcode::AchievementLinkRequest, request->link),
                                         "Inspect linked achievement", current) && current()) {
        achievement_message_ = "Showing the linked snapshot; waiting for the player's current data.";
        return true;
    }
    achievements_.cancel_unsent_link(*request);
    if (achievement_link_request_token_ == request->token) {
        achievement_link_target_.reset(); achievement_link_request_token_.reset();
    }
    return false;
}
bool GameConnection::achievement_reward(std::uint64_t token, std::uint32_t index) {
    const auto eligible = [&] {
        const auto* definition = own_definition(achievements_, token, index);
        return reward_request_ready() && definition && definition->has_reward;
    };
    if (!eligible()) return false;
    const auto now = reward_now_ms();
    if (reward_view_requested_ms_[0] && now >= *reward_view_requested_ms_[0] && now - *reward_view_requested_ms_[0] < 500)
        return false;
    const auto old = reward_view_requested_ms_[0];
    const auto revision = rewards_.lane(game::RewardLane::Pending).revision;
    reward_view_requested_ms_[0] = now;
    const auto current = [&] { return eligible() && rewards_.lane(game::RewardLane::Pending).revision == revision; };
    if (current() && submit_zone_action(messages::outgoing(messages::GameOpcode::AchievementRewardView,
                                                            AchievementRewardView{index}),
                                         "Inspect achievement reward", current) && current()) {
        rewards_.show(game::RewardLane::Pending); return true;
    }
    if (reward_view_requested_ms_[0] == now) reward_view_requested_ms_[0] = old;
    return false;
}
std::optional<std::string> GameConnection::achievement_chat_link(std::uint64_t token, std::uint32_t index) const {
    const auto* definition = own_definition(achievements_, token, index);
    if (!reward_request_ready() || !definition) return std::nullopt;
    auto link = make_achievement_link(character_, *definition, *achievements_.states()[index]);
    if (!link) return std::nullopt;
    return chat_tag(*link, definition->name);
}
void GameConnection::update_achievements() {
    const auto now = reward_now_ms();
    achievements_.update(now);
    if (achievement_compare_target_ && *achievement_compare_target_ != achievement_target()) {
        achievements_.retire_compare(); achievement_compare_target_.reset();
        if (achievement_message_ == kCompareWaiting) achievement_message_.clear();
    }
    // A comparison has no request nonce, so a timed-out one stays reserved until a late reply drains it
    // (Achievements::update); say so rather than keep waiting.
    if (achievements_.compare_retired() && achievement_message_ == kCompareWaiting)
        achievement_message_ = std::string(kCompareTimedOut);
    if (achievement_link_target_) {
        const auto at = world_.spawns.find(achievement_link_target_->first);
        if (at == world_.spawns.end() || at->second.incarnation != achievement_link_target_->second) {
            achievements_.retire_link(); achievement_link_target_.reset(); achievement_link_request_token_.reset();
        }
    }
    if (achievement_refresh_token_ && now >= achievement_refresh_sent_ms_ &&
        now - achievement_refresh_sent_ms_ >= game::Achievements::kRequestTimeoutMs) {
        achievement_refresh_token_.reset();
        achievement_message_ = "The achievement refresh timed out. Refresh again to request a new server snapshot.";
    }
}
bool GameConnection::handle_achievements(const messages::GameMessage& message) {
    using messages::GameOpcode;
    const auto* catalog = achievements_.catalog() ? &*achievements_.catalog() : nullptr;
    bool recognized = true, valid = false;
    const bool active = state_ == ConnectionState::ZoneEntering || (state_ == ConnectionState::InZone && !zone_change_pending_);
    if (message.opcode == GameOpcode::AchievementGuild) {
        if (active) if (auto decoded = messages::message_get<AchievementGuild>(message)) {
            const auto* definition = catalog->find(decoded->achievement_id);
            const bool own = game::same_no_case(decoded->player_name, character_);
            const auto mode = chat_filters_.mode(32);
            if (definition && mode != 1 && (mode != 3 || own))
                chat(ChatEvent::Kind::Guild, std::format("{} completed {}.", decoded->player_name,
                     chat_tag(decoded->link, definition->name)), 0, own ? 348 : 349, decoded->player_name);
            return true; // guild announcements never replace the character's state
        }
        return false; // ordinary guild updates keep their existing handler
    }
    switch (message.opcode) {
    case GameOpcode::AchievementDefinitions:
        if (active) if (auto decoded = messages::message_get<AchievementCatalog>(message)) {
            achievements_.receive_catalog(*decoded); achievement_compare_target_.reset();
            achievement_link_target_.reset(); achievement_link_request_token_.reset();
            // Reward tabs have independent server identities. Refreshing the
            // catalog must not close unrelated task/pending reward views.
            valid = true;
        }
        break;
    case GameOpcode::AchievementPrimary:
        if (active) if (auto decoded = messages::message_get<AchievementPrimary>(message)) valid = achievements_.receive_primary(*decoded);
        break;
    case GameOpcode::AchievementProgress:
        if (active) if (auto decoded = messages::message_get<std::vector<AchievementProgress>>(message)) valid = achievements_.receive_progress(*decoded);
        break;
    case GameOpcode::AchievementSnapshot:
        if (active) if (auto decoded = messages::message_get<std::vector<AchievementState>>(message)) {
            const auto target = achievement_target();
            achievements_.receive_comparison(*decoded, target.first, target.second); valid = true;
            // Shown, or a late reply drained: either way nothing is waiting any more.
            if (!achievements_.compare_pending() &&
                (achievement_message_ == kCompareWaiting || achievement_message_ == kCompareTimedOut))
                achievement_message_.clear();
        }
        break;
    case GameOpcode::AchievementInspect:
        if (active) if (auto decoded = messages::message_get<AchievementInspect>(message)) {
            if (achievement_link_target_) {
                const auto at = world_.spawns.find(achievement_link_target_->first);
                if (at == world_.spawns.end() || at->second.incarnation != achievement_link_target_->second)
                    achievements_.retire_link();
            }
            achievements_.receive_inspect(*decoded); valid = true;
            if (!achievements_.link_pending()) { achievement_link_target_.reset(); achievement_link_request_token_.reset(); }
        }
        break;
    case GameOpcode::AchievementWindow:
        if (active && messages::message_get<AchievementWindow>(message)) {
            achievements_.show(); achievement_refresh_token_.reset();
            achievement_message_ = achievements_.primary_ready() ? "" : "Waiting for achievement state."; valid = true;
        }
        break;
    case GameOpcode::AchievementEarned:
        if (active) if (auto decoded = messages::message_get<AchievementEarned>(message)) {
            const auto found = world_.spawns.find(decoded->spawn_id);
            if (found != world_.spawns.end() && !found->second.is_npc &&
                game::same_no_case(found->second.name, decoded->link.player_name)) {
                const bool own = decoded->spawn_id == world_.player_id;
                const auto epoch = achievements_.catalog_token();
                const auto incarnation = found->second.incarnation;
                const auto mode = chat_filters_.mode(32);
                const auto* definition = catalog->find(decoded->achievement_id);
                const std::string name = definition ? definition->name : std::string();
                if (definition && mode != 1 && (mode != 3 || own)) {
                    const auto tag = chat_tag(decoded->link, name);
                    chat(ChatEvent::Kind::System, std::format("{} completed {}.", decoded->link.player_name, tag), 0, own ? 348 : 349);
                    const auto actor = world_.spawns.find(decoded->spawn_id);
                    if (!reward_request_ready() || achievements_.catalog_token() != epoch || actor == world_.spawns.end() ||
                        actor->second.incarnation != incarnation) { valid = true; break; }
                    if (own) {
                        // Hailstone's presentation, with the existing overlay's standard
                        // fades. The notification never changes authoritative own state.
                        text_overlay_.display("Achievement completed: " + name, 14, 1, 255, 500, 500, 3000,
                                              static_cast<std::uint32_t>(now_ms()));
                    }
                }
                if (decoded->sound_id) {
                    SoundCue cue; cue.kind = SoundCue::Kind::SpawnWave;
                    cue.target = decoded->spawn_id; cue.spell = decoded->sound_id; sound_cues_.push_back(cue);
                }
                achievements_.receive_earned(*decoded);
            }
            valid = true;
        }
        break;
    default:
        recognized = false;
        break;
    }
    if (recognized && active && !valid) log::debug("ignored achievement state event without a matching active request");
    return recognized;
}
} // namespace hailstone::net
