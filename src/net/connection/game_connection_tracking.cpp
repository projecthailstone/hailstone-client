// GameConnection: the tracking window's list (CTrackingWnd; game/tracking.h): OP_Track out on show, every 6 s with
// auto update and from UseSkill(53); the reply parsed, sorted, filtered and shown; Track sends OP_TrackTarget.

#include <algorithm>

#include "core/log.h"
#include "game/combat.h"
#include "game/mechanics/abilities.h"
#include "game/mechanics/character.h"
#include "net/connection/game_connection.h"

namespace hailstone::net {

namespace {

std::string trim_name(const std::string& name) { return game::display_name(name); }

bool in_group_fn(const void* ctx, std::int32_t id) {
    const auto* c = static_cast<const GameConnection*>(ctx);
    const auto& w = c->world();
    auto it = w.spawns.find(static_cast<std::uint32_t>(id));
    if (it == w.spawns.end()) return id == static_cast<std::int32_t>(w.player_id);
    return it->first == w.player_id || c->group().has_member(game::display_name(it->second.name)) ||
           c->group().has_member(it->second.name);
}

} // namespace

std::optional<GameConnection::TrackingAdmission> GameConnection::tracking_admission() const try {
    if (tracking_admission_running_ || state_ != ConnectionState::InZone || pstatus_.dead || !world_.player() || !profile_)
        return std::nullopt;
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{tracking_admission_running_};
    tracking_admission_running_ = true;
    const auto context = interaction_context();
    const auto lifecycle = server_ext().lifecycle_revision(*this);
    bool allowed = false;
    with_character([&](game::mechanics::Character& character) {
        // with_character's effect-only helper intentionally has empty tables. Tracking uses real skill caps.
        game::mechanics::Character tracker(character.sheet(), tables(), character.spells());
        allowed = server_ext().tracking_allowed(*this, character.sheet().class_id, player_class_mask(),
            static_cast<std::uint32_t>(std::max(0, tracker.adjusted_skill(game::mechanics::skill::kTracking))));
    });
    if (!allowed || interaction_context() != context || server_ext().lifecycle_revision(*this) != lifecycle)
        return std::nullopt;
    return TrackingAdmission{context, lifecycle};
} catch (...) { return std::nullopt; }

bool GameConnection::tracking_admitted() const { return tracking_admission().has_value(); }

bool GameConnection::tracking_admission_current(TrackingAdmission admission) const {
    const auto current = tracking_admission();
    return current && current->context == admission.context && current->lifecycle == admission.lifecycle;
}

void GameConnection::retire_tracking() {
    ++tracking_action_generation_;
    if (tracking_request_) tracking_request_retired_ = true; // keep the wire tombstone until its reply drains
    tracking_data_.reset(); tracking_actors_.clear();
    tracking_.open = false; tracking_.tracked = 0;
    tracking_.list = {};
    ++tracking_.revision;
}

void GameConnection::maintain_tracking() {
    // An OP_Track unanswered for 10 s (our own number) is taken as never answered, so the next refresh goes out; the
    // list shown stays. A reply that comes after it is the next request's (the replies carry no request id).
    if (tracking_request_ && now_ms() - tracking_request_at_ > 10000) tracking_request_.reset();
    if ((tracking_data_ && !tracking_admission_current(*tracking_data_)) ||
        (tracking_request_ && !tracking_admission_current(*tracking_request_)) ||
        (tracking_.open && !tracking_admitted())) retire_tracking();
}

void GameConnection::open_tracking(bool skill_admitted) try {
    const auto context = interaction_context();
    if (!skill_admitted) {
        const auto allowed = server_ext().before_use_skill(*this, game::mechanics::skill::kTracking);
        if (allowed && !*allowed) { retire_tracking(); return; }
    }
    maintain_tracking();
    if (context != interaction_context() || !tracking_admitted()) { retire_tracking(); return; }
    // CTrackingWnd::Activate: the times cleared and every entry reset (the level kept), then AboutToShow's OP_Track.
    tracking_.next_controls = tracking_.next_track = 0;
    for (auto& e : tracking_.list) {
        e.distance = -1.0f;
        e.spawn_id = 0;
        e.name.clear();
        e.sort_name.clear();
        e.position = -1;
        e.flags = 0;
    }
    // CTrackingWnd::Activate: the auto update check box from TrackAutoUpdate (T, t or 1; missing FALSE).
    tracking_.auto_update = client_options_.on(game::ClientOption::TrackAutoUpdate);
    tracking_.open = true;
    ++tracking_.revision;
    send_track();
} catch (...) { retire_tracking(); }

void GameConnection::close_tracking() {
    ++tracking_action_generation_;
    tracking_.open = false;
    if (tracking_request_) tracking_request_retired_ = true;
}

void GameConnection::set_track_auto_update(bool on) {
    tracking_.auto_update = on;
    client_options_.set(game::ClientOption::TrackAutoUpdate, on ? 1 : 0);
    save_client_option(game::ClientOption::TrackAutoUpdate);
}

void GameConnection::send_track() {
    maintain_tracking();
    if (tracking_request_) return; // one in flight at a time: the replies carry no request id
    const auto admitted = tracking_admission();
    if (!admitted) { retire_tracking(); return; }
    const auto serial = ++tracking_request_serial_;
    tracking_request_ = admitted; tracking_request_retired_ = false; tracking_request_at_ = now_ms();
    const auto current = [this, admitted, serial] {
        return tracking_request_ && serial == tracking_request_serial_ && !tracking_request_retired_ &&
            tracking_admission_current(*admitted) && tracking_request_ && serial == tracking_request_serial_ && !tracking_request_retired_;
    };
    try {
        if (!send_zone_if_current(messages::outgoing(messages::GameOpcode::Track), current) && serial == tracking_request_serial_) {
            tracking_request_.reset(); retire_tracking();
        }
    } catch (...) {
        if (serial == tracking_request_serial_) tracking_request_.reset();
        retire_tracking();
    }
}

void GameConnection::track_spawn(std::int32_t spawn_id, std::uint32_t expected_revision) try {
    const auto entering = tracking_action_generation_;
    maintain_tracking();
    if (!tracking_data_ || !tracking_admission_current(*tracking_data_) ||
        (expected_revision && expected_revision != tracking_.revision)) return;
    if (entering != tracking_action_generation_) return;
    // WndNotification, the Track button: Send_OP_TrackTarget(0x35, 0) marks tracking as the activity and
    // OP_TrackTarget {id} names the spawn; "You begin tracking %1." (12040, colour 0x10e), then the window closes.
    auto it = std::find_if(tracking_.list.begin(), tracking_.list.end(),
                           [&](const game::TrackEntry& e) { return e.spawn_id == spawn_id && spawn_id != 0; });
    if (it == tracking_.list.end()) return;
    const auto actor = tracking_actors_.find(spawn_id);
    if (actor != tracking_actors_.end() && !matches(actor->second)) return;
    const std::optional<ActorRef> actor_ref = actor == tracking_actors_.end() ? std::nullopt : std::optional(actor->second);
    const auto admission = *tracking_data_;
    const auto revision = tracking_.revision;
    const auto name = it->sort_name.empty() ? it->name : it->sort_name;
    const auto generation = ++tracking_action_generation_;
    const auto owned = [this, admission, revision, generation, actor_ref] {
        return tracking_action_generation_ == generation && tracking_data_ && tracking_.revision == revision &&
            tracking_data_->context == admission.context && tracking_data_->lifecycle == admission.lifecycle &&
            interaction_context() == admission.context && (!actor_ref || matches(*actor_ref));
    };
    if (!send_zone_if_current(messages::outgoing(messages::GameOpcode::TrackTarget,static_cast<std::uint32_t>(spawn_id)), [this, admission, owned] {
        return owned() && tracking_admission_current(admission) && owned();
    }) || !owned()) return;
    tracking_.tracked = spawn_id;
    system(12040, {name}, 0x10e);
    tracking_.open = false;
} catch (...) { retire_tracking(); }

void GameConnection::set_track_sort(std::uint32_t order) {
    tracking_.sort_order = order;
    // DoTrackSort: a ranger's order (above 6 nothing), others order 0.
    bool ranger = profile_ && profile_->class_ == 4;
    std::uint32_t o = ranger ? order : 0;
    if (o <= 6) game::track_sort(tracking_.list, static_cast<int>(o));
    ++tracking_.revision;
}

void GameConnection::set_track_filter(const game::TrackFilter& f) {
    tracking_.filter = f;
    track_filter_all();
}

void GameConnection::track_filter_all() {
    const bool ranger = profile_ && profile_->class_ == 4;
    const int mine = pstatus_.level;
    for (auto& e : tracking_.list) {
        if (e.spawn_id == 0) continue;
        std::int32_t owner = 0;
        if (auto it = world_.spawns.find(static_cast<std::uint32_t>(e.spawn_id)); it != world_.spawns.end())
            owner = static_cast<std::int32_t>(it->second.pet_owner_id);
        auto standard = static_cast<std::uint32_t>(game::standard_con(e.level, mine));
        game::track_filter(e, standard, ranger, tracking_.filter, &in_group_fn, owner, this);
    }
    ++tracking_.revision;
}

void GameConnection::handle_track_reply(const TrackReply& reply) {
    // Any reply answers the request in flight, a short one too: set_tracking_list reads it as RoF2 does (zero past
    // its end, game/tracking.cpp).
    maintain_tracking();
    if (!tracking_request_) return;
    const auto request = *tracking_request_;
    const bool retired = tracking_request_retired_;
    tracking_request_.reset(); tracking_request_retired_ = false;
    if (retired || !tracking_admission_current(request)) return;
    if (reply.timer) {
        log::debug("track: the reply is a timer, not a list");
        return;
    }
    const bool ranger = profile_ && profile_->class_ == 4;
    // Codec has decoded the RoF2 list. Sorting and display-name normalization remain gameplay decisions.
    for (auto& entry : tracking_.list) {
        entry.spawn_id=0; entry.distance=-1.0f; entry.name.clear(); entry.sort_name.clear();
        entry.position=-1; entry.flags=0;
    }
    for (std::size_t i=0;i<reply.entries.size() && i<tracking_.list.size();++i) {
        tracking_.list[i]=reply.entries[i];
        tracking_.list[i].sort_name=trim_name(tracking_.list[i].name);
    }
    const auto order=ranger?tracking_.sort_order:0u;
    if (order<=6) game::track_sort(tracking_.list,static_cast<int>(order));
    track_filter_all();
    tracking_data_ = request;
    tracking_actors_.clear();
    for (const auto& entry : tracking_.list)
        if (auto actor = captureActorRef(static_cast<std::uint32_t>(entry.spawn_id)))
            tracking_actors_.emplace(entry.spawn_id, *actor);
    std::size_t n = std::count_if(tracking_.list.begin(), tracking_.list.end(), [](const auto& e) { return e.position >= 0; });
    log::info("track: {} spawns in the reply", n);
}

std::vector<GameConnection::TrackLine> GameConnection::tracking_lines() const {
    if (!tracking_data_ || !tracking_admission_current(*tracking_data_)) return {};
    std::vector<std::int32_t> group;
    for (const auto& m : group_.slots) {
        if (m.name.empty()) continue;
        for (const auto& [id, s] : world_.spawns)
            if (game::display_name(s.name) == m.name || s.name == m.name) group.push_back(static_cast<std::int32_t>(id));
    }
    std::vector<TrackLine> out;
    for (const auto& e : tracking_.list) {
        if (e.spawn_id == 0 || (e.flags & game::kTrackHidden)) continue;
        out.push_back({game::track_line(e), game::track_color(e, group), e.spawn_id});
    }
    return out;
}

} // namespace hailstone::net
