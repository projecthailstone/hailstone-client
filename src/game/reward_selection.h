#pragma once

// Received Select Reward data only. Inventory, eligibility and reward delivery remain
// authoritative server state; neither a successful claim nor an inspected item grants ownership.

#include <array>
#include <cstdint>
#include <optional>
#include <map>
#include <string>
#include <vector>

#include "net/messages/reward.h"

namespace hailstone::game {

using RewardLane = net::RewardLane;
struct RewardTab {
    std::uint64_t token = 0;
    net::RewardSet reward;
    bool blocked = false;
};
struct RewardLaneState {
    std::uint64_t revision = 0;
    bool shown = false;
    std::vector<RewardTab> tabs;
    bool claim_pending = false, claim_uncertain = false, inspect_pending = false;
    std::string message;
};
struct RewardClaimRequest {
    std::uint64_t request_token = 0, revision = 0, tab_token = 0;
    std::uint32_t pending_id = 0, set_id = 0, wire_option_id = 0;
};
struct RewardInspectRequest {
    std::uint64_t request_token = 0, revision = 0, tab_token = 0;
    RewardLane lane = RewardLane::Pending;
    std::uint32_t set_id = 0, wire_option_id = 0, entry_id = 0, item_id = 0;
};

class RewardSelection {
public:
    static constexpr std::int64_t kRequestTimeoutMs = 10000;
    const RewardLaneState& lane(RewardLane value) const { return lanes_[index(value)]; }
    void show(RewardLane value);
    void close(RewardLane value);
    void close_all();
    void reset();
    // Atomic decoded collections replace/upsert according to the native lane/action. A matched inspection returns an item for
    // the existing read-only display queue. Unknown/stale replies have no visible effect.
    std::optional<net::Item> receive(net::RewardPacket packet);
    void update(std::int64_t now_ms);

    // Reserve before calling transport: an outgoing hook may reenter, retire the context or
    // deliver a terminal event. Rollback accepts only this exact reservation's token.
    std::optional<RewardClaimRequest> begin_claim(std::uint64_t revision, std::uint64_t tab_token,
        std::uint32_t wire_option_id, std::int64_t now_ms, std::string* why = nullptr);
    std::optional<RewardInspectRequest> begin_inspect(RewardLane value, std::uint64_t revision,
        std::uint64_t tab_token, std::uint32_t wire_option_id, std::uint32_t entry_id,
        std::uint32_t item_id, std::int64_t now_ms, std::string* why = nullptr);
    void cancel_unsent_claim(const RewardClaimRequest& request);
    void cancel_unsent_inspect(const RewardInspectRequest& request);
    bool claim_current(const RewardClaimRequest& request) const;
    bool inspect_current(const RewardInspectRequest& request) const;

private:
    static std::size_t index(RewardLane value) { return value == RewardLane::Pending ? 0 : 1; }
    RewardTab* find_tab(RewardLane value, std::uint64_t revision, std::uint64_t token);
    std::uint64_t next_token();
    void retire_inspection(RewardLane value);
    std::array<RewardLaneState, 2> lanes_;
    struct Claim { RewardClaimRequest request; std::int64_t sent_ms = 0; };
    struct Inspect { RewardInspectRequest request; std::int64_t sent_ms = 0; };
    std::optional<Claim> claim_;
    std::array<std::optional<Inspect>, 2> inspections_;
    // Inspection replies have only an item ID. Reusing an ID whose retired reply has not
    // drained could attribute that old reply to a new request. Saturation disables inspection
    // until replies drain rather than evicting identities and pretending correlation is possible.
    std::array<std::map<std::uint32_t, std::uint64_t>, 2> retired_inspections_;
};

} // namespace hailstone::game
