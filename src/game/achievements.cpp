#include "game/achievements.h"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

#include "core/process_serial.h"

namespace hailstone::game {
namespace {
bool shape(const net::AchievementState& state, const net::AchievementDefinition& definition) {
    const auto status = static_cast<std::int16_t>(state.status);
    if (status < 0 || status > 3 || (state.status == net::AchievementStatus::Completed) != state.completed_at.has_value()) return false;
    if (!state.satisfied[3].empty() || !state.counts[3].empty()) return false;
    for (const auto type : {1u, 2u, 0u}) {
        if (state.satisfied[type].size() != definition.components[type].size() ||
            state.counts[type].size() != definition.components[type].size()) return false;
    }
    return true;
}
// Buckets 0..2 carry runtime counts; bucket 3 is descriptive only.
bool shown_in(const net::AchievementDefinition& definition, std::uint32_t component) {
    for (const auto bucket : {0u, 1u, 2u})
        if (definition.component_indices[bucket].contains(component)) return true;
    return false;
}
bool same_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
} // namespace

void Achievements::changed() {
    revision_ = core::next_process_identity();
    if (selection_) detail_revision_ = core::next_process_identity();
}
void Achievements::reset() {
    catalog_.reset(); catalog_token_ = core::next_process_identity();
    states_.clear(); requirements_.clear();
    primary_ready_ = progress_ready_ = shown_ = detail_shown_ = false;
    selection_.reset(); detail_link_.reset(); comparison_.clear(); comparison_name_.clear();
    compare_request_.reset(); link_request_.reset(); earned_.clear();
    revision_ = core::next_process_identity(); detail_revision_ = core::next_process_identity();
}
void Achievements::show(bool value) {
    shown_ = value;
    if (!value) retire_compare();
    changed();
}
void Achievements::close_detail() {
    detail_shown_ = false;
    if (link_request_) link_request_->retired = true;
    detail_revision_ = core::next_process_identity();
}
const net::AchievementState* Achievements::detail_state() const {
    if (!selection_ || selection_->catalog_token != catalog_token_) return nullptr;
    if (selection_->source == AchievementDetailSource::Link) return detail_link_ ? &detail_link_->state : nullptr;
    const auto index = selection_->definition_index;
    return index < states_.size() && states_[index] ? &*states_[index] : nullptr;
}
bool Achievements::open_selection(std::uint64_t token, std::uint32_t index) {
    if (!catalog_ || token != catalog_token_ || index >= catalog_->definitions.size()) return false;
    if (link_request_) link_request_->retired = true;
    selection_ = AchievementSelection{token, index, AchievementDetailSource::Own};
    detail_link_.reset(); detail_shown_ = true; changed(); return true;
}
bool Achievements::open_link(net::AchievementLink link) {
    if (!catalog_) return false;
    const auto found = catalog_->definition_indices.find(link.achievement_id);
    if (found == catalog_->definition_indices.end() || !shape(link.state, catalog_->definitions[found->second])) return false;
    // Every explicit view open owns a new context, including a newer snapshot for the same
    // peer/achievement. A prior identity-less request must not replace that newer selection.
    if (link_request_) link_request_->retired = true;
    selection_ = AchievementSelection{catalog_token_, static_cast<std::uint32_t>(found->second), AchievementDetailSource::Link};
    detail_link_ = std::move(link); detail_shown_ = true; changed(); return true;
}
void Achievements::receive_catalog(net::AchievementCatalog catalog) {
    std::vector<std::optional<net::AchievementState>> states(catalog.definitions.size());
    std::shared_ptr<const net::AchievementCatalog> retired_catalog;
    if (catalog_ && ((compare_request_ && !compare_request_->retired_catalog) ||
                     (link_request_ && !link_request_->retired_catalog)))
        retired_catalog = std::make_shared<net::AchievementCatalog>(std::move(*catalog_));
    // Move a replaced catalog only once, sharing it when both requests belong to it.
    // Further replacements must not overwrite an older undrained request's schema.
    if (compare_request_) {
        if (!compare_request_->retired_catalog) compare_request_->retired_catalog = retired_catalog;
        compare_request_->retired = true;
    }
    if (link_request_) {
        if (!link_request_->retired_catalog) link_request_->retired_catalog = retired_catalog;
        link_request_->retired = true;
    }
    catalog_ = std::move(catalog); catalog_token_ = core::next_process_identity(); states_ = std::move(states);
    requirements_.clear(); primary_ready_ = progress_ready_ = false;
    comparison_.clear(); comparison_name_.clear(); earned_.clear(); selection_.reset(); detail_link_.reset();
    detail_shown_ = false; detail_revision_ = core::next_process_identity(); changed();
}
bool Achievements::receive_primary(net::AchievementPrimary primary) {
    // The leading u32 orders nothing: RoF2's 0x618f handler (0x403b90) steps over it unread.
    if (!catalog_ || primary.updates.size() > states_.size()) return false;
    std::set<std::uint32_t> indices;
    for (std::size_t i = 0; i < primary.updates.size(); ++i) {
        const auto& update = primary.updates[i];
        if (update.definition_index >= states_.size() || !indices.insert(update.definition_index).second ||
            (primary.dense && update.definition_index != i) ||
            !shape(update.state, catalog_->definitions[update.definition_index])) return false;
    }
    const bool full = primary.dense && primary.updates.size() == states_.size();
    auto states = states_;
    for (auto& update : primary.updates) {
        auto& old = states[update.definition_index];
        if (!full && old && progress_ready_) update.state.counts = old->counts;
        old = std::move(update.state);
    }
    states_ = std::move(states);
    primary_ready_ = primary_ready_ || full;
    if (full) { progress_ready_ = false; requirements_.clear(); }
    changed(); return true;
}
bool Achievements::receive_progress(const std::vector<net::AchievementProgress>& progress) {
    if (!catalog_ || !primary_ready_ || progress.size() > net::kMaxAchievementRequirements) return false;
    auto requirements = requirements_;
    bool difference = !progress_ready_;
    for (const auto& entry : progress) {
        const auto found = catalog_->definition_indices.find(entry.achievement_id);
        if (found == catalog_->definition_indices.end()) return false;
        const auto& definition = catalog_->definitions[found->second];
        if (!shown_in(definition, entry.component_id)) return false;
        const RequirementKey key{entry.achievement_id, entry.component_id, entry.requirement_id, entry.requirement_type};
        const auto old = requirements.find(key);
        difference = difference || old == requirements.end() || old->second != entry.current_count;
        if (old == requirements.end() && requirements.size() == net::kMaxAchievementRequirements) return false;
        requirements[key] = entry.current_count;
    }
    if (!difference) return true;
    auto states = states_;
    for (auto& state : states) if (state) for (const auto type : {1u, 2u, 0u})
        for (auto& value : state->counts[type]) value = 0;
    // RoF2 sums distinct requirement records for a visible component. Keep a bounded full-key
    // replacement map, and widen only the presentation sum; it never decides achievement status.
    // The record's type is part of its key only: RoF2's 0x2b42 handler (0x403020) finds the component
    // by id in buckets 0, 1 and 2 and sets each one holding it to the sum for (achievement, component)
    // over every requirement id and type (0x7c53b0).
    for (const auto& [key, value] : requirements) {
        const auto [id, component, requirement, type] = key;
        (void)requirement; (void)type;
        const auto definition = catalog_->definition_indices.at(id);
        if (!states[definition]) return false;
        for (const auto bucket : {0u, 1u, 2u}) {
            const auto& indices = catalog_->definitions[definition].component_indices[bucket];
            const auto index = indices.find(component);
            if (index == indices.end()) continue;
            auto& aggregate = states[definition]->counts[bucket][index->second];
            aggregate = aggregate.value_or(0) + value;
        }
    }
    requirements_ = std::move(requirements); states_ = std::move(states); progress_ready_ = true;
    changed(); return true;
}
std::vector<net::AchievementProgress> Achievements::requirements(std::uint32_t id) const {
    std::vector<net::AchievementProgress> result;
    for (auto it = requirements_.lower_bound({id, 0, 0, 0}); it != requirements_.end() && std::get<0>(it->first) == id; ++it) {
        const auto [achievement, component, requirement, type] = it->first;
        result.push_back({achievement, component, requirement, type, it->second});
    }
    return result;
}
void Achievements::receive_earned(net::AchievementEarned earned) {
    if (!catalog_ || !catalog_->find(earned.achievement_id) || earned.link.achievement_id != earned.achievement_id) return;
    earned_.push_back(std::move(earned));
    if (earned_.size() > 64) earned_.pop_front();
}
std::optional<net::AchievementEarned> Achievements::take_earned() {
    if (earned_.empty()) return std::nullopt;
    auto result = std::move(earned_.front()); earned_.pop_front(); return result;
}
void Achievements::clear_comparison() { comparison_.clear(); comparison_name_.clear(); }
void Achievements::clear_link() {
    if (selection_ && selection_->source == AchievementDetailSource::Link) {
        selection_.reset(); detail_link_.reset(); detail_shown_ = false;
        detail_revision_ = core::next_process_identity();
    }
}
std::optional<AchievementCompareRequest> Achievements::reserve_compare(std::uint32_t target, std::uint64_t incarnation,
    std::string name, std::int64_t now) {
    if (!catalog_ || compare_request_ || !target || !incarnation || name.empty() || name.size() > 63) return std::nullopt;
    AchievementCompareRequest request{core::next_process_identity(), catalog_token_, incarnation, target, std::move(name)};
    compare_request_ = ComparePending{request, now, false, {}}; clear_comparison(); changed(); return request;
}
bool Achievements::compare_current(const AchievementCompareRequest& request, std::uint32_t target, std::uint64_t incarnation) const {
    return compare_request_ && !compare_request_->retired && compare_request_->request.token == request.token &&
        request.catalog_token == catalog_token_ && request.target_id == target && request.target_incarnation == incarnation;
}
void Achievements::cancel_unsent_compare(const AchievementCompareRequest& request) {
    if (compare_request_ && compare_request_->request.token == request.token) compare_request_.reset();
}
void Achievements::retire_compare() {
    if (compare_request_) compare_request_->retired = true;
    clear_comparison(); changed();
}
const net::AchievementCatalog* Achievements::comparison_reply_catalog() const {
    if (!compare_request_) return nullptr;
    return compare_request_->retired_catalog ? compare_request_->retired_catalog.get() : catalog_ ? &*catalog_ : nullptr;
}
bool Achievements::receive_comparison(std::vector<net::AchievementState> snapshot, std::uint32_t target, std::uint64_t incarnation) {
    const auto* reply_catalog = comparison_reply_catalog();
    if (!reply_catalog) return false;
    if (snapshot.size() != reply_catalog->definitions.size()) return false;
    for (std::size_t i = 0; i < snapshot.size(); ++i) if (!shape(snapshot[i], reply_catalog->definitions[i])) return false;
    const auto request = compare_request_->request;
    const bool current = compare_current(request, target, incarnation);
    compare_request_.reset();
    if (!current) return false;
    comparison_ = std::move(snapshot); comparison_name_ = request.target_name; shown_ = true; changed(); return true;
}
std::optional<AchievementLinkRequest> Achievements::reserve_link(net::AchievementLink link, std::int64_t now) {
    if (!catalog_ || link_request_ || !catalog_->find(link.achievement_id)) return std::nullopt;
    AchievementLinkRequest request{core::next_process_identity(), catalog_token_, std::move(link)};
    link_request_ = LinkPending{request, now, false, {}}; return request;
}
bool Achievements::link_current(const AchievementLinkRequest& request) const {
    return link_request_ && !link_request_->retired && link_request_->request.token == request.token &&
        request.catalog_token == catalog_token_;
}
void Achievements::cancel_unsent_link(const AchievementLinkRequest& request) {
    if (link_request_ && link_request_->request.token == request.token) link_request_.reset();
}
void Achievements::retire_link() {
    if (link_request_) link_request_->retired = true;
    clear_link(); changed();
}
const net::AchievementCatalog* Achievements::inspection_reply_catalog() const {
    if (!link_request_) return nullptr;
    return link_request_->retired_catalog ? link_request_->retired_catalog.get() : catalog_ ? &*catalog_ : nullptr;
}
bool Achievements::receive_inspect(net::AchievementInspect inspected) {
    const auto* reply_catalog = inspection_reply_catalog();
    if (!reply_catalog) return false;
    const auto& expected = link_request_->request;
    if (inspected.achievement_id != expected.link.achievement_id || !same_name(inspected.player_name, expected.link.player_name)) return false;
    const auto* definition = reply_catalog->find(inspected.achievement_id);
    if (!definition || !shape(inspected.state, *definition)) return false;
    const auto request = expected;
    const bool current = link_current(request);
    link_request_.reset();
    if (!current) return false;
    auto link = net::make_achievement_link(inspected.player_name, *definition, inspected.state);
    return link && open_link(std::move(*link));
}
void Achievements::update(std::int64_t now) {
    if (compare_request_ && now >= compare_request_->sent_ms && now - compare_request_->sent_ms >= kRequestTimeoutMs)
        compare_request_->retired = true;
    if (link_request_ && now >= link_request_->sent_ms && now - link_request_->sent_ms >= kRequestTimeoutMs)
        link_request_->retired = true;
}

} // namespace hailstone::game
