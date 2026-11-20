#include <doctest/doctest.h>

#include "game/reward_selection.h"
#include "rof2/reward_test_packets.h"

using namespace hailstone;
using game::RewardLane;
using game::RewardSelection;
namespace fixture = hailstone::test::reward_fixture;

namespace {
auto choose(RewardSelection& state, std::uint32_t option = 12, std::int64_t now = 100) {
    const auto& lane = state.lane(RewardLane::Pending);
    return state.begin_claim(lane.revision, lane.tabs.at(0).token, option, now);
}
auto inspect(RewardSelection& state, RewardLane lane = RewardLane::Pending,
             std::uint32_t option = 12, std::uint32_t id = 1000, std::int64_t now = 100) {
    const auto& view = state.lane(lane);
    return state.begin_inspect(lane, view.revision, view.tabs.at(0).token, option, 20, id, now);
}
net::RewardPacket inspected(RewardLane lane, std::uint32_t id = 1000) {
    net::RewardPacket packet;
    packet.lane = lane; packet.kind = net::RewardPacket::Kind::Inspection;
    packet.inspection.emplace(); packet.inspection->item_id = id;
    packet.inspection->item.id = id; packet.inspection->item.name = "Read-only received item";
    return packet;
}
} // namespace

TEST_CASE("reward selection: primary and preview collections are independent, including replacement identities") {
    RewardSelection state;
    state.receive(fixture::decoded(fixture::collection()));
    state.receive(fixture::decoded(fixture::collection(RewardLane::Preview, 99)));
    const auto pending = state.lane(RewardLane::Pending);
    const auto preview = state.lane(RewardLane::Preview);
    CHECK(pending.shown);
    CHECK(preview.shown);
    CHECK(pending.tabs[0].token != preview.tabs[0].token);
    state.receive(fixture::decoded(fixture::collection()));
    CHECK(state.lane(RewardLane::Pending).revision != pending.revision);
    CHECK(state.lane(RewardLane::Pending).tabs[0].token != pending.tabs[0].token);
    CHECK(state.lane(RewardLane::Preview).revision == preview.revision);
    CHECK_FALSE(state.begin_claim(pending.revision, pending.tabs[0].token, 12, 0));
    CHECK_FALSE(state.begin_claim(preview.revision, preview.tabs[0].token, 99, 0));
    CHECK_FALSE(choose(state, 1)); // common grants are never selectable
    CHECK_FALSE(choose(state, 0));
    CHECK_FALSE(choose(state, 999));
    CHECK(choose(state));
    CHECK_FALSE(choose(state));
    CHECK_FALSE(inspect(state)); // server's claim gate applies to the entire pending lane
    CHECK(inspect(state, RewardLane::Preview, 99));
}

TEST_CASE("reward selection: mixed providers can reuse pending and set IDs but not an ambiguous full claim tuple") {
    RewardSelection state;
    state.receive(fixture::decoded(fixture::bulk()));
    const auto& lane = state.lane(RewardLane::Pending);
    REQUIRE(lane.tabs.size() == 2);
    CHECK(lane.tabs[0].reward.pending_id == lane.tabs[1].reward.pending_id);
    CHECK(lane.tabs[0].reward.set_id == lane.tabs[1].reward.set_id);
    auto request = state.begin_claim(lane.revision, lane.tabs[1].token, 13, 100);
    REQUIRE(request);
    CHECK(request->wire_option_id == 13);
    state.cancel_unsent_claim(*request);
    auto ambiguous = fixture::decoded(fixture::bulk());
    ambiguous.sets[1].options[1].wire_id = 12;
    state.receive(std::move(ambiguous));
    CHECK_FALSE(choose(state));
}

TEST_CASE("reward selection: exact terminal success removes only its reserved tab; denial revises blocked data") {
    for (const bool success : {false, true}) {
        RewardSelection state;
        state.receive(fixture::decoded(fixture::bulk()));
        const auto old = state.lane(RewardLane::Pending);
        REQUIRE(choose(state));
        state.receive(fixture::decoded(fixture::claim(success, 999)));
        CHECK(state.lane(RewardLane::Pending).claim_pending);
        state.receive(fixture::decoded(fixture::claim(success)));
        const auto& lane = state.lane(RewardLane::Pending);
        CHECK_FALSE(lane.claim_pending);
        CHECK_FALSE(lane.claim_uncertain);
        CHECK(lane.revision != old.revision);
        REQUIRE(lane.tabs.size() == (success ? 1 : 2));
        if (success) CHECK(lane.tabs[0].token == old.tabs[1].token);
        else {
            CHECK(lane.tabs[0].blocked);
            CHECK_FALSE(choose(state));
        }
    }
}

TEST_CASE("reward selection: replacement and restore never authorize replay of an uncertain dispatched claim") {
    RewardSelection state;
    state.receive(fixture::decoded(fixture::collection()));
    const auto request = choose(state);
    REQUIRE(request);
    state.update(100 + RewardSelection::kRequestTimeoutMs - 1);
    CHECK(state.lane(RewardLane::Pending).claim_pending);
    state.update(100 + RewardSelection::kRequestTimeoutMs);
    CHECK_FALSE(state.lane(RewardLane::Pending).claim_pending);
    CHECK(state.lane(RewardLane::Pending).claim_uncertain);
    state.receive(fixture::decoded(fixture::collection()));
    const auto fresh = state.lane(RewardLane::Pending);
    CHECK_FALSE(choose(state));
    state.receive(fixture::decoded(fixture::claim(true)));
    CHECK_FALSE(state.lane(RewardLane::Pending).claim_uncertain);
    REQUIRE(state.lane(RewardLane::Pending).tabs.size() == 1);
    CHECK(state.lane(RewardLane::Pending).tabs[0].token == fresh.tabs[0].token);
    const auto newer = choose(state);
    REQUIRE(newer);
    state.cancel_unsent_claim(*request); // stale rollback cannot cancel the new reservation
    CHECK(state.lane(RewardLane::Pending).claim_pending);
    CHECK(state.claim_current(*newer));
}

TEST_CASE("reward selection: closing invalidates confirmation but a terminal still retires its original tab") {
    RewardSelection state;
    state.receive(fixture::decoded(fixture::collection()));
    const auto old = state.lane(RewardLane::Pending);
    REQUIRE(choose(state));
    state.close(RewardLane::Pending);
    CHECK_FALSE(state.lane(RewardLane::Pending).shown);
    CHECK_FALSE(state.begin_claim(old.revision, old.tabs[0].token, 12, 0));
    state.show(RewardLane::Pending);
    state.receive(fixture::decoded(fixture::claim(true)));
    CHECK(state.lane(RewardLane::Pending).tabs.empty());
}

TEST_CASE("reward selection: inspections require received item identity and exact lane, including common entries") {
    RewardSelection state;
    state.receive(fixture::decoded(fixture::collection()));
    state.receive(fixture::decoded(fixture::collection(RewardLane::Preview)));
    CHECK_FALSE(inspect(state, RewardLane::Pending, 12, 55));
    const auto request = inspect(state);
    REQUIRE(request);
    CHECK_FALSE(inspect(state));
    CHECK_FALSE(state.receive(inspected(RewardLane::Preview)));
    CHECK_FALSE(state.receive(inspected(RewardLane::Pending, 99)));
    CHECK(state.lane(RewardLane::Pending).inspect_pending);
    const auto item = state.receive(inspected(RewardLane::Pending));
    REQUIRE(item);
    CHECK(item->id == 1000);
    CHECK_FALSE(state.lane(RewardLane::Pending).inspect_pending);
    CHECK_FALSE(state.receive(inspected(RewardLane::Pending))); // no unsolicited duplicate display
    auto common = fixture::decoded(fixture::collection());
    common.sets[0].options[1].common = true;
    state.receive(std::move(common));
    CHECK(inspect(state));
}

TEST_CASE("reward selection: retired inspection identities drain without authenticating another collection") {
    RewardSelection state;
    state.receive(fixture::decoded(fixture::collection()));
    REQUIRE(inspect(state));
    state.receive(fixture::decoded(fixture::collection()));
    CHECK_FALSE(inspect(state));
    CHECK_FALSE(state.receive(inspected(RewardLane::Pending)));
    REQUIRE(inspect(state));
    state.update(100 + RewardSelection::kRequestTimeoutMs);
    CHECK_FALSE(state.lane(RewardLane::Pending).inspect_pending);
    CHECK_FALSE(inspect(state));
    CHECK_FALSE(state.receive(inspected(RewardLane::Pending)));
    const auto current = inspect(state);
    REQUIRE(current);
    state.cancel_unsent_inspect(*current);
    CHECK_FALSE(state.inspect_current(*current));
    REQUIRE(inspect(state)); // a provably unsent request creates no retired-wire ambiguity
}

TEST_CASE("reward selection: retirement and replacement connection objects never reuse UI ownership tokens") {
    RewardSelection state;
    state.receive(fixture::decoded(fixture::collection()));
    const auto old = state.lane(RewardLane::Pending);
    const auto old_request = choose(state);
    REQUIRE(old_request);
    state.reset();
    CHECK_FALSE(state.lane(RewardLane::Pending).shown);
    CHECK_FALSE(state.lane(RewardLane::Pending).claim_pending);
    state.receive(fixture::decoded(fixture::collection()));
    CHECK_FALSE(state.begin_claim(old.revision, old.tabs[0].token, 12, 100));
    RewardSelection replacement;
    replacement.receive(fixture::decoded(fixture::collection()));
    CHECK(replacement.lane(RewardLane::Pending).revision != old.revision);
    CHECK(replacement.lane(RewardLane::Pending).tabs[0].token != old.tabs[0].token);
    CHECK_FALSE(replacement.begin_claim(old.revision, old.tabs[0].token, 12, 100));
    const auto current = choose(replacement);
    REQUIRE(current);
    replacement.cancel_unsent_claim(*old_request);
    CHECK(replacement.claim_current(*current));
}

TEST_CASE("reward selection: reentrant unsent inspection rollback removes only its own retired identity") {
    RewardSelection state;
    state.receive(fixture::decoded(fixture::collection()));
    const auto unsent = inspect(state);
    REQUIRE(unsent);
    state.close(RewardLane::Pending);
    state.show(RewardLane::Pending);
    CHECK_FALSE(inspect(state));
    state.cancel_unsent_inspect(*unsent);
    const auto newer = inspect(state);
    REQUIRE(newer);
    state.close(RewardLane::Pending);
    state.show(RewardLane::Pending);
    state.cancel_unsent_inspect(*unsent);
    CHECK_FALSE(inspect(state)); // stale rollback cannot erase the newer retired request
    CHECK_FALSE(state.receive(inspected(RewardLane::Pending)));
    CHECK(inspect(state));
}

TEST_CASE("reward selection: retired inspection budget blocks without evicting undrained identities") {
    RewardSelection state;
    for (std::uint32_t id = 1; id <= net::kMaxRewardTabs; ++id) {
        state.receive(fixture::decoded(fixture::collection(RewardLane::Pending, 12, id)));
        REQUIRE(inspect(state, RewardLane::Pending, 12, id));
        state.update(100 + RewardSelection::kRequestTimeoutMs);
    }
    state.receive(fixture::decoded(fixture::collection(RewardLane::Pending, 12, 9999)));
    CHECK_FALSE(inspect(state, RewardLane::Pending, 12, 9999));
    CHECK_FALSE(state.receive(inspected(RewardLane::Pending, 1)));
    CHECK(inspect(state, RewardLane::Pending, 12, 9999));
}

TEST_CASE("reward selection: native pending single upserts by first ID, preview single and bulk replace") {
    RewardSelection state;
    state.receive(fixture::decoded(fixture::bulk(false)));
    const auto old = state.lane(RewardLane::Pending);
    REQUIRE(old.tabs.size() == 2);
    state.receive(fixture::decoded(fixture::single(RewardLane::Pending, 44, 702, 14)));
    const auto added = state.lane(RewardLane::Pending);
    REQUIRE(added.tabs.size() == 3);
    CHECK(added.tabs[0].token == old.tabs[0].token);
    CHECK(added.tabs[1].token == old.tabs[1].token);
    state.receive(fixture::decoded(fixture::single(RewardLane::Pending, 43, 999, 71)));
    const auto changed = state.lane(RewardLane::Pending);
    REQUIRE(changed.tabs.size() == 3);
    CHECK(changed.tabs[0].token == old.tabs[0].token);
    CHECK(changed.tabs[1].token == added.tabs[2].token);
    CHECK(changed.tabs[2].reward.pending_id == 43);
    CHECK(changed.tabs[2].reward.set_id == 999); // key is pending_id, not second wire set_id
    CHECK(changed.tabs[2].token != old.tabs[1].token);
    CHECK_FALSE(state.begin_claim(old.revision, old.tabs[1].token, 13, 100));
    CHECK_FALSE(state.begin_claim(changed.revision, old.tabs[1].token, 13, 100));
    const auto preserved = state.begin_claim(changed.revision, old.tabs[0].token, 12, 100);
    REQUIRE(preserved);
    state.cancel_unsent_claim(*preserved);
    state.receive(fixture::decoded(fixture::clear(RewardLane::Pending, false)));
    CHECK(state.lane(RewardLane::Pending).revision == changed.revision);
    CHECK(state.lane(RewardLane::Pending).tabs.size() == 3);
    auto preview_bulk = fixture::bulk(false); preview_bulk.opcode = 0x6471;
    state.receive(fixture::decoded(preview_bulk));
    REQUIRE(state.lane(RewardLane::Preview).tabs.size() == 2);
    state.receive(fixture::decoded(fixture::single(RewardLane::Preview, 44, 702, 14)));
    REQUIRE(state.lane(RewardLane::Preview).tabs.size() == 1);
    CHECK(state.lane(RewardLane::Preview).tabs[0].reward.pending_id == 44);
    for (const auto lane : {RewardLane::Pending, RewardLane::Preview}) {
        state.receive(fixture::decoded(fixture::clear(lane, true)));
        CHECK(state.lane(lane).tabs.empty());
    }
}

TEST_CASE("reward selection: native pending upsert applies aggregate collection limits atomically") {
    RewardSelection state;
    for (std::uint32_t i = 1; i <= net::kMaxRewardTabs; ++i)
        state.receive(fixture::decoded(fixture::single(RewardLane::Pending, i, 700, i + 1)));
    const auto& full = state.lane(RewardLane::Pending);
    REQUIRE(full.tabs.size() == net::kMaxRewardTabs);
    const auto revision = full.revision;
    state.receive(fixture::decoded(fixture::single(RewardLane::Pending, 9999, 700, 9999)));
    CHECK(state.lane(RewardLane::Pending).tabs.size() == net::kMaxRewardTabs);
    CHECK(state.lane(RewardLane::Pending).revision == revision);
    state.receive(fixture::decoded(fixture::single(RewardLane::Pending, 1, 701, 9999)));
    CHECK(state.lane(RewardLane::Pending).tabs.size() == net::kMaxRewardTabs);
    CHECK(state.lane(RewardLane::Pending).revision != revision);
}
