#include <doctest/doctest.h>

#include <limits>

#include "rof2/achievement_test_packets.h"
#include "game/achievements.h"

using namespace hailstone;
using namespace hailstone::net;
namespace fixture = hailstone::test::achievement_fixture;

namespace {
AchievementCatalog catalog() {
    auto value = decode_achievement_catalog(fixture::catalog());
    if (!value) throw std::runtime_error("invalid achievement catalog fixture");
    return std::move(*value);
}
AchievementPrimary primary(const game::Achievements& state, std::uint32_t serial = 1, bool complete = false) {
    auto result = decode_achievement_primary(fixture::primary(serial, complete), &*state.catalog());
    if (!result) throw std::runtime_error("invalid achievement primary fixture");
    return std::move(*result);
}
void initialized(game::Achievements& state) {
    state.receive_catalog(catalog());
    REQUIRE(state.receive_primary(primary(state)));
}
AchievementLink link(const game::Achievements& state, std::string_view name = "Peer", std::uint32_t id = 900) {
    auto result = parse_achievement_link(fixture::metadata(name, id), *state.catalog());
    if (!result) throw std::runtime_error("invalid achievement link fixture");
    return std::move(*result);
}
std::vector<AchievementState> snapshot(const game::Achievements& state) {
    auto result = decode_achievement_snapshot(fixture::comparison(), &*state.catalog());
    if (!result) throw std::runtime_error("invalid comparison fixture");
    return std::move(*result);
}
AchievementInspect inspected(const game::Achievements& state, std::string_view name = "Peer", std::uint32_t id = 900) {
    auto result = decode_achievement_inspect(fixture::inspect(name, id), &*state.catalog());
    if (!result) throw std::runtime_error("invalid inspect fixture");
    return std::move(*result);
}
} // namespace

TEST_CASE("achievements: complete primary plus even empty progress initializes counts without inventing completion") {
    game::Achievements state;
    state.receive_catalog(catalog());
    CHECK_FALSE(state.primary_ready()); CHECK_FALSE(state.progress_ready());
    CHECK_FALSE(state.receive_progress({{900, 201, 70000, 1, 2}}));
    REQUIRE(state.receive_primary(primary(state, 1, true)));
    CHECK(state.primary_ready()); CHECK_FALSE(state.progress_ready());
    REQUIRE(state.states()[1]);
    CHECK(state.states()[1]->status == AchievementStatus::Completed);
    CHECK_FALSE(state.states()[1]->counts[1][0]);
    CHECK(state.states()[1]->counts[3].empty());
    REQUIRE(state.receive_progress({}));
    CHECK(state.progress_ready());
    CHECK(state.states()[1]->counts[1][0] == 0);
    CHECK(state.states()[1]->status == AchievementStatus::Completed); // a checked bit is not an invented count
    REQUIRE(state.receive_primary(primary(state, 2)));
    CHECK_FALSE(state.progress_ready());
    CHECK_FALSE(state.states()[1]->counts[1][0]);
}

TEST_CASE("achievements: full-key requirement replacement aggregates display only and skips records it cannot show") {
    game::Achievements state; initialized(state);
    REQUIRE(state.receive_progress({{900, 201, 70000, 1, 2}, {900, 201, 70001, 1, 3}}));
    CHECK(state.states()[1]->counts[1][0] == 5);
    CHECK(state.states()[1]->status == AchievementStatus::Open);
    CHECK(state.states()[1]->satisfied[1][0] == 0);
    const auto revision = state.revision();
    REQUIRE(state.receive_progress({{900, 201, 70000, 1, 2}}));
    CHECK(state.revision() == revision);
    REQUIRE(state.receive_progress({{900, 201, 70000, 1, 7}}));
    CHECK(state.states()[1]->counts[1][0] == 10);
    REQUIRE(state.requirements(900).size() == 2);
    CHECK(state.requirements(900)[0].requirement_id == 70000); // catalog byte display order is zero
    // As RoF2's 0x2b42 handler: a record for an unknown achievement, a descriptive-only component or another
    // definition's component is skipped, and the packet's other records still apply.
    REQUIRE(state.receive_progress({{900, 201, 70000, 1, 88}, {9999, 201, 0, 1, 1}}));
    CHECK(state.states()[1]->counts[1][0] == 88 + 3);
    REQUIRE(state.requirements(900).size() == 2);
    const auto before_skipped = state.revision();
    REQUIRE(state.receive_progress({{900, 303, 1, 3, 1}}));
    REQUIRE(state.receive_progress({{900, 101, 1, 1, 1}})); // component belongs to another definition
    CHECK(state.revision() == before_skipped);
    CHECK(state.requirements(900).size() == 2);
    CHECK(state.states()[1]->counts[1][0] == 88 + 3);
    REQUIRE(state.receive_progress({{900, 201, 70000, 1, 0xffffffffu}, {900, 201, 70001, 1, 0xffffffffu}}));
    CHECK(state.states()[1]->counts[1][0] == 8589934590ull);
    CHECK(state.states()[1]->status == AchievementStatus::Open);
}

TEST_CASE("achievements: a progress record's type is a key, not the bucket, as RoF2's 0x2b42 handler reads it") {
    game::Achievements state; initialized(state);
    // A server's requirement kind (4: kill an NPC) for the bucket-1 component 201, beside a type-1 record.
    REQUIRE(state.receive_progress({{900, 201, 70000, 4, 2}, {900, 201, 70000, 1, 3}}));
    CHECK(state.states()[1]->counts[1][0] == 5);
    REQUIRE(state.requirements(900).size() == 2);
    REQUIRE(state.receive_progress({{900, 200, 7, 9, 6}})); // the bucket-0 component, any type
    CHECK(state.states()[1]->counts[0][0] == 6);
    CHECK(state.states()[1]->counts[1][0] == 5);
    REQUIRE(state.receive_progress({{900, 303, 1, 1, 1}})); // only in the descriptive bucket 3: skipped
    CHECK(state.requirements(900).size() == 3);
}

TEST_CASE("achievements: primary leading u32 is ignored, sparse definition indices and refresh preserve explicit readiness") {
    game::Achievements state; state.receive_catalog(catalog());
    REQUIRE(state.receive_primary(primary(state, 0xfffffffeu)));
    REQUIRE(state.receive_progress({{900, 201, 70000, 1, 2}}));
    // RoF2's handler (0x403b90) steps over the leading u32: a repeated or lower value applies all the same.
    REQUIRE(state.receive_primary(primary(state, 0xfffffffeu)));
    REQUIRE(state.receive_primary(primary(state, 0xfffffffdu)));
    REQUIRE(state.receive_progress({{900, 201, 70000, 1, 2}}));
    ByteWriter w; w.le<std::uint32_t>(0); w.le<std::uint8_t>(0); w.le<std::uint32_t>(1);
    w.le<std::uint32_t>(1); fixture::state(w, AchievementStatus::Locked);
    auto update = decode_achievement_primary({kOpAchievementPrimary, w.take()}, &*state.catalog());
    REQUIRE(update); REQUIRE(state.receive_primary(std::move(*update)));
    CHECK(state.states()[1]->status == AchievementStatus::Locked);
    CHECK(state.states()[1]->counts[1][0] == 2);
    REQUIRE(state.receive_primary(primary(state, 0)));
    CHECK_FALSE(state.progress_ready());
    REQUIRE(state.receive_progress({}));
    CHECK(state.states()[1]->counts[1][0] == 0);
    game::Achievements partial; partial.receive_catalog(catalog());
    auto short_primary = primary(partial); short_primary.updates.pop_back();
    REQUIRE(partial.receive_primary(std::move(short_primary)));
    CHECK_FALSE(partial.primary_ready()); CHECK_FALSE(partial.receive_progress({}));
}

TEST_CASE("achievements: catalog replacement and replacement connection objects retire exact UI identities") {
    game::Achievements state; initialized(state);
    const auto old = state.catalog_token();
    CHECK_FALSE(state.open_selection(old, 900));
    REQUIRE(state.open_selection(old, 1));
    CHECK(state.selection()->definition_index == 1);
    CHECK(state.detail_shown()); CHECK_FALSE(state.shown());
    const auto detail = state.detail_revision();
    REQUIRE(state.receive_progress({}));
    CHECK(state.detail_revision() != detail);
    CHECK(state.detail_state() == &*state.states()[1]);
    state.receive_catalog(catalog());
    CHECK(state.catalog_token() != old);
    CHECK_FALSE(state.selection()); CHECK_FALSE(state.detail_shown());
    CHECK_FALSE(state.open_selection(old, 1));
    game::Achievements replacement; replacement.receive_catalog(catalog());
    CHECK(replacement.catalog_token() != state.catalog_token());
    CHECK_FALSE(replacement.open_selection(state.catalog_token(), 1));
    state.reset(); CHECK_FALSE(state.catalog()); CHECK_FALSE(state.primary_ready());
}

TEST_CASE("achievements: identity-less comparison is isolated and cannot authenticate a newer target") {
    game::Achievements state; initialized(state);
    const auto request = state.reserve_compare(77, 1001, "Peer", 100);
    REQUIRE(request); CHECK(state.compare_current(*request, 77, 1001));
    CHECK_FALSE(state.reserve_compare(88, 1002, "Other", 101));
    state.retire_compare();
    CHECK_FALSE(state.reserve_compare(88, 1002, "Other", 102));
    CHECK_FALSE(state.receive_comparison(snapshot(state), 88, 1002));
    CHECK_FALSE(state.compare_pending()); CHECK(state.comparison().empty());
    const auto current = state.reserve_compare(88, 1002, "Other", 103);
    REQUIRE(current);
    state.cancel_unsent_compare(*request); // stale rollback cannot retire the current reservation
    CHECK(state.compare_current(*current, 88, 1002));
    REQUIRE(state.receive_comparison(snapshot(state), 88, 1002));
    CHECK(state.comparison_name() == "Other"); CHECK(state.shown());
    CHECK(state.comparison()[1].status == AchievementStatus::Completed);
    CHECK_FALSE(state.comparison()[1].counts[1][0]);
    CHECK(state.states()[1]->status == AchievementStatus::Open);
    CHECK(state.reserve_compare(77, 1001, "Peer", 200));
    state.update(200 + game::Achievements::kRequestTimeoutMs);
    CHECK(state.compare_retired());
    CHECK_FALSE(state.receive_comparison(snapshot(state), 77, 1001));
    CHECK_FALSE(state.compare_pending());
}

TEST_CASE("achievements: links need no selected target, and only matched current replies update their detail") {
    game::Achievements state; initialized(state);
    const auto received = link(state);
    REQUIRE(state.open_link(received));
    CHECK(state.selection()->source == game::AchievementDetailSource::Link);
    CHECK(state.detail_state()->counts[1][0] == 4);
    CHECK_FALSE(state.shown());
    const auto request = state.reserve_link(received, 100);
    REQUIRE(request); CHECK(state.link_current(*request));
    CHECK_FALSE(state.receive_inspect(inspected(state, "Wrong")));
    CHECK(state.link_pending());
    CHECK_FALSE(state.receive_inspect(inspected(state, "Peer", 42)));
    CHECK(state.link_pending());
    REQUIRE(state.receive_inspect(inspected(state)));
    CHECK_FALSE(state.link_pending());
    CHECK(state.states()[1]->status == AchievementStatus::Open);
    CHECK_FALSE(state.states()[1]->counts[1][0]);
    REQUIRE(state.reserve_link(received, 200));
    REQUIRE(state.open_link(link(state, "Elsewhere")));
    CHECK(state.link_retired());
    CHECK_FALSE(state.receive_inspect(inspected(state)));
    CHECK(state.detail_link()->player_name == "Elsewhere");
    REQUIRE(state.open_link(received));
    REQUIRE(state.reserve_link(received, 300));
    auto newer = received; newer.state.counts[1][0] = 9;
    REQUIRE(state.open_link(newer));
    CHECK(state.link_retired());
    CHECK_FALSE(state.receive_inspect(inspected(state)));
    CHECK(state.detail_state()->counts[1][0] == 9);
}

TEST_CASE("achievements: hidden detail close and catalog retirement retain undrained link tombstones") {
    game::Achievements state; initialized(state);
    const auto request = state.reserve_link(link(state), 100);
    REQUIRE(request); CHECK_FALSE(state.detail_shown());
    state.close_detail(); CHECK_FALSE(state.link_current(*request));
    CHECK_FALSE(state.reserve_link(link(state), 200));
    CHECK_FALSE(state.receive_inspect(inspected(state)));
    REQUIRE(state.reserve_link(link(state), 300));
    state.receive_catalog(catalog());
    CHECK(state.link_retired());
    CHECK_FALSE(state.receive_inspect(inspected(state)));
    CHECK_FALSE(state.detail_shown());
    const auto fresh = state.reserve_link(link(state), 400);
    REQUIRE(fresh);
    state.cancel_unsent_link(*request); CHECK(state.link_current(*fresh));
    state.cancel_unsent_link(*fresh); CHECK_FALSE(state.link_pending());
}

TEST_CASE("achievements: earned events stay bounded and never mark our achievement complete") {
    game::Achievements state; initialized(state);
    for (std::uint32_t i = 0; i < 70; ++i) {
        AchievementEarned event; event.spawn_id = 100 + i; event.achievement_id = 900; event.sound_id = 3695;
        event.link = link(state); state.receive_earned(std::move(event));
    }
    const auto first = state.take_earned(); REQUIRE(first); CHECK(first->spawn_id == 106);
    std::size_t remaining = 0; while (state.take_earned()) ++remaining;
    CHECK(remaining == 63);
    CHECK(state.states()[1]->status == AchievementStatus::Open);
    CHECK_FALSE(state.states()[1]->counts[1][0]);
}

TEST_CASE("achievements: replaced catalogs keep each pending reply schema until a valid old reply drains") {
    game::Achievements state; initialized(state);
    const auto old_snapshot = snapshot(state);
    const auto old_inspection = inspected(state);
    REQUIRE(state.reserve_compare(77, 1001, "Peer", 100));
    REQUIRE(state.reserve_link(link(state), 100));
    const auto intermediate = decode_achievement_catalog(fixture::catalog("Intermediate", 1, 17));
    REQUIRE(intermediate);
    state.receive_catalog(*intermediate);
    const auto* retained = state.comparison_reply_catalog();
    REQUIRE(retained);
    CHECK(retained == state.inspection_reply_catalog()); // one shared retired catalog
    CHECK(retained->definitions.size() == 2);
    CHECK(retained->definitions[1].components[1].size() == 1);
    const auto newest = decode_achievement_catalog(fixture::catalog("Newest", 3, 17));
    REQUIRE(newest);
    state.receive_catalog(*newest);
    CHECK(state.comparison_reply_catalog() == retained);
    CHECK(state.inspection_reply_catalog() == retained);
    state.update(100 + game::Achievements::kRequestTimeoutMs);
    CHECK(state.compare_retired()); CHECK(state.link_retired());
    CHECK_FALSE(state.reserve_compare(88, 1002, "Other", 10200));

    auto wrong_snapshot = old_snapshot;
    wrong_snapshot.pop_back();
    CHECK_FALSE(state.receive_comparison(wrong_snapshot, 77, 1001));
    wrong_snapshot = old_snapshot;
    wrong_snapshot[1].satisfied[1].push_back(0);
    CHECK_FALSE(state.receive_comparison(wrong_snapshot, 77, 1001));
    auto wrong_inspection = old_inspection;
    wrong_inspection.state.counts[1].push_back(0);
    CHECK_FALSE(state.receive_inspect(wrong_inspection));
    wrong_inspection = old_inspection;
    wrong_inspection.player_name = "Other";
    CHECK_FALSE(state.receive_inspect(wrong_inspection));
    CHECK(state.compare_pending()); CHECK(state.link_pending());

    CHECK_FALSE(state.receive_comparison(old_snapshot, 77, 1001)); // retired data drains without adoption
    CHECK_FALSE(state.receive_inspect(old_inspection));
    CHECK_FALSE(state.compare_pending()); CHECK_FALSE(state.link_pending());
    CHECK_FALSE(state.comparison_reply_catalog()); CHECK_FALSE(state.inspection_reply_catalog());
    CHECK(state.comparison().empty()); CHECK_FALSE(state.selection()); CHECK_FALSE(state.detail_shown());
    CHECK(state.states().size() == 3);
    CHECK_FALSE(state.states()[0]);
    CHECK(state.reserve_compare(88, 1002, "Other", 10300));
    const auto fresh_link = parse_achievement_link(fixture::metadata("Peer", 900, 4, 17), *state.catalog());
    REQUIRE(fresh_link);
    CHECK(state.reserve_link(*fresh_link, 10300));
    CHECK(state.comparison_reply_catalog() == &*state.catalog());
    CHECK(state.inspection_reply_catalog() == &*state.catalog());
    state.reset();
    CHECK_FALSE(state.comparison_reply_catalog()); CHECK_FALSE(state.inspection_reply_catalog());
}
