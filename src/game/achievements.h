#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "net/messages/achievement.h"

namespace hailstone::game {

enum class AchievementDetailSource { Own, Link };
struct AchievementSelection {
    std::uint64_t catalog_token = 0;
    std::uint32_t definition_index = 0;
    AchievementDetailSource source = AchievementDetailSource::Own;
};
struct AchievementCompareRequest {
    std::uint64_t token = 0, catalog_token = 0, target_incarnation = 0;
    std::uint32_t target_id = 0;
    std::string target_name;
};
struct AchievementLinkRequest {
    std::uint64_t token = 0, catalog_token = 0;
    net::AchievementLink link;
};

// Character-scoped, received stock data. Criteria, lock policy, completion and grants
// remain server decisions. Comparisons and linked snapshots never replace own state.
class Achievements {
public:
    static constexpr std::int64_t kRequestTimeoutMs = 10000;
    const std::optional<net::AchievementCatalog>& catalog() const { return catalog_; }
    std::uint64_t catalog_token() const { return catalog_token_; }
    std::uint64_t revision() const { return revision_; }
    std::uint64_t detail_revision() const { return detail_revision_; }
    const std::vector<std::optional<net::AchievementState>>& states() const { return states_; }
    const std::vector<net::AchievementState>& comparison() const { return comparison_; }
    const std::string& comparison_name() const { return comparison_name_; }
    bool primary_ready() const { return primary_ready_; }
    bool progress_ready() const { return progress_ready_; }
    bool shown() const { return shown_; }
    bool detail_shown() const { return detail_shown_; }
    const std::optional<AchievementSelection>& selection() const { return selection_; }
    const std::optional<net::AchievementLink>& detail_link() const { return detail_link_; }
    const net::AchievementState* detail_state() const;
    std::vector<net::AchievementProgress> requirements(std::uint32_t achievement_id) const;

    void reset();
    void show(bool value = true);
    void close_detail();
    bool open_selection(std::uint64_t catalog_token, std::uint32_t definition_index);
    bool open_link(net::AchievementLink link);
    void receive_catalog(net::AchievementCatalog catalog);
    bool receive_primary(net::AchievementPrimary primary);
    bool receive_progress(const std::vector<net::AchievementProgress>& progress);
    void receive_earned(net::AchievementEarned earned);
    std::optional<net::AchievementEarned> take_earned();

    std::optional<AchievementCompareRequest> reserve_compare(std::uint32_t target_id, std::uint64_t incarnation,
        std::string target_name, std::int64_t now_ms);
    bool compare_current(const AchievementCompareRequest& request, std::uint32_t target_id,
                         std::uint64_t incarnation) const;
    void cancel_unsent_compare(const AchievementCompareRequest& request);
    // Retains a dispatched identity-less request as a tombstone until its reply drains.
    void retire_compare();
    bool compare_pending() const { return compare_request_.has_value(); }
    bool compare_retired() const { return compare_request_ && compare_request_->retired; }
    // A retired reply still uses the catalog under which its request was dispatched.
    // Decoding against a replacement catalog would prevent its tombstone from draining.
    const net::AchievementCatalog* comparison_reply_catalog() const;
    bool receive_comparison(std::vector<net::AchievementState> snapshot, std::uint32_t current_target,
                            std::uint64_t current_incarnation);
    std::optional<AchievementLinkRequest> reserve_link(net::AchievementLink link, std::int64_t now_ms);
    bool link_current(const AchievementLinkRequest& request) const;
    void cancel_unsent_link(const AchievementLinkRequest& request);
    void retire_link();
    bool link_pending() const { return link_request_.has_value(); }
    bool link_retired() const { return link_request_ && link_request_->retired; }
    const net::AchievementCatalog* inspection_reply_catalog() const;
    bool receive_inspect(net::AchievementInspect inspected);
    void update(std::int64_t now_ms);

private:
    using RequirementKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;
    void changed();
    void clear_comparison();
    void clear_link();
    std::optional<net::AchievementCatalog> catalog_;
    std::uint64_t catalog_token_ = 0, revision_ = 0, detail_revision_ = 0;
    std::vector<std::optional<net::AchievementState>> states_;
    std::map<RequirementKey, std::uint32_t> requirements_;
    bool primary_ready_ = false, progress_ready_ = false, shown_ = false, detail_shown_ = false;
    std::optional<AchievementSelection> selection_;
    std::optional<net::AchievementLink> detail_link_;
    std::vector<net::AchievementState> comparison_;
    std::string comparison_name_;
    struct ComparePending {
        AchievementCompareRequest request;
        std::int64_t sent_ms = 0;
        bool retired = false;
        std::shared_ptr<const net::AchievementCatalog> retired_catalog;
    };
    struct LinkPending {
        AchievementLinkRequest request;
        std::int64_t sent_ms = 0;
        bool retired = false;
        std::shared_ptr<const net::AchievementCatalog> retired_catalog;
    };
    std::optional<ComparePending> compare_request_;
    std::optional<LinkPending> link_request_;
    std::deque<net::AchievementEarned> earned_;
};

} // namespace hailstone::game
