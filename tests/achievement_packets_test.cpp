#include <doctest/doctest.h>

#include <limits>

#include "achievement_test_packets.h"
#include "net/opcodes.h"
#include "net/player_packets.h"
#include "net/reward_packets.h"

using namespace hailstone;
using namespace hailstone::net;
namespace fixture = hailstone::test::achievement_fixture;

namespace {
AchievementCatalog catalog() {
    auto value = decode_achievement_catalog(fixture::catalog());
    if (!value) throw std::runtime_error("invalid achievement catalog fixture");
    return std::move(*value);
}
AppPacket compressed(const std::vector<std::byte>& body) {
    uLongf size = compressBound(static_cast<uLong>(body.size()));
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (compress2(reinterpret_cast<Bytef*>(bytes.data()), &size, reinterpret_cast<const Bytef*>(body.data()),
                  static_cast<uLong>(body.size()), Z_BEST_SPEED) != Z_OK) throw std::runtime_error("test compression failed");
    bytes.resize(static_cast<std::size_t>(size));
    ByteWriter w; w.le(static_cast<std::uint32_t>(body.size())); w.bytes(bytes);
    return {kOpAchievementDefinitions, w.take()};
}
AppPacket graph(std::uint32_t first_parent, std::uint32_t second_parent) {
    ByteWriter w; w.le<std::uint32_t>(2);
    for (const auto id : {7u, 8u}) {
        w.le(id); w.le(id == 7 ? first_parent : second_parent);
        for (int i = 0; i < 3; ++i) fixture::string(w, "");
        w.le<std::uint32_t>(0); w.le<std::uint32_t>(0); w.le<std::uint32_t>(0);
    }
    w.le<std::uint32_t>(0); return compressed(w.take());
}
} // namespace

TEST_CASE("achievement packets: the switch-label opcode constants agree with the opcode table") {
    CHECK(kOpAchievementDefinitions == to_wire(Op::AchievementDefinitions));
    CHECK(kOpAchievementPrimary == to_wire(Op::AchievementUpdate));
    CHECK(kOpAchievementProgress == to_wire(Op::AchievementProgress));
    CHECK(kOpAchievementSnapshot == to_wire(Op::AchievementState));
    CHECK(kOpAchievementInspect == to_wire(Op::AchievementComparisonReply));
    CHECK(kOpAchievementEarned == to_wire(Op::AchievementEarned));
    CHECK(kOpAchievementWindow == to_wire(Op::AchievementWindow));
    CHECK(kOpAchievementWindowRequest == to_wire(Op::AchievementRequest));
    CHECK(kOpAchievementLinkRequest == to_wire(Op::AchievementLinkRequest));
    CHECK(kOpAchievementCompare == to_wire(Op::AchievementCompareRequest));
    CHECK(kOpPendingRewards == to_wire(Op::AchievementReward));
    CHECK(kOpRewardPreview == to_wire(Op::RewardSelection));
}

TEST_CASE("achievement packets: exact compressed catalog retains received indices and descriptive bucket") {
    const auto decoded = decode_achievement_catalog(fixture::catalog());
    REQUIRE(decoded);
    REQUIRE(decoded->categories.size() == 1);
    CHECK(decoded->categories[0].parent_id == 0xffffffffu);
    REQUIRE(decoded->definitions.size() == 2);
    CHECK(decoded->definitions[1].id == 900);
    CHECK(decoded->definition_indices.at(900) == 1);
    CHECK(decoded->definitions[1].name == fixture::second_name);
    CHECK(decoded->definitions[1].has_reward);
    CHECK(decoded->definitions[0].points == 5);
    CHECK(decoded->definitions[1].components[1][0].id == 201);
    CHECK(decoded->definitions[1].components[1][0].required_count == 5);
    CHECK(decoded->definitions[1].components[3][0].id == 303);
    CHECK(decoded->definitions[1].component_indices[3].at(303) == 0);
    const auto packet = fixture::catalog();
    for (std::size_t n = 0; n < packet.data.size(); ++n) {
        auto short_packet = packet; short_packet.data.resize(n);
        CHECK_FALSE(decode_achievement_catalog(short_packet));
    }
    auto trailing = packet; trailing.data.push_back(std::byte{0});
    CHECK_FALSE(decode_achievement_catalog(trailing));
    ByteWriter bomb; bomb.le(static_cast<std::uint32_t>(kMaxAchievementCatalogBytes + 1)); bomb.le<std::uint8_t>(0);
    CHECK_FALSE(decode_achievement_catalog({kOpAchievementDefinitions, bomb.take()}));
    ByteWriter empty; empty.le<std::uint32_t>(0); empty.le<std::uint32_t>(0);
    const auto disabled = decode_achievement_catalog(compressed(empty.take()));
    REQUIRE(disabled);
    CHECK(disabled->definitions.empty());
    CHECK_FALSE(decode_achievement_catalog(graph(8, 7))); // parent cycle
    CHECK_FALSE(decode_achievement_catalog(graph(999, kAchievementRootCategory)));
    CHECK_FALSE(decode_achievement_catalog(graph(kAchievementRootCategory, 7))); // child list inconsistent
}

TEST_CASE("achievement packets: state word boundaries use buckets one two zero and no type-three state") {
    AchievementCatalog data;
    AchievementDefinition definition; definition.id = 42;
    definition.components[1].resize(17); definition.components[2].resize(1);
    definition.components[0].resize(2); definition.components[3].resize(3);
    data.definitions.push_back(definition); data.definition_indices.emplace(42, 0);
    ByteWriter w; w.le<std::uint32_t>(7); w.le<std::uint8_t>(1); w.le<std::uint32_t>(1);
    w.le<std::int16_t>(0); w.le<std::uint16_t>(0x8001); w.le<std::uint16_t>(1);
    w.le<std::uint16_t>(1); w.le<std::uint16_t>(2); w.le<std::uint32_t>(123456);
    const AppPacket packet(kOpAchievementPrimary, w.take());
    const auto primary = decode_achievement_primary(packet, &data);
    REQUIRE(primary);
    const auto& state = primary->updates[0].state;
    CHECK(state.status == AchievementStatus::Completed);
    CHECK(state.completed_at == 123456);
    CHECK(state.satisfied[1][0] == 1);
    CHECK(state.satisfied[1][15] == 1);
    CHECK(state.satisfied[1][16] == 1);
    CHECK(state.satisfied[2][0] == 1);
    CHECK(state.satisfied[0][0] == 0);
    CHECK(state.satisfied[0][1] == 1);
    CHECK(state.satisfied[3].empty());
    CHECK_FALSE(state.counts[1][0]);
    for (std::size_t n = 0; n < packet.data.size(); ++n) {
        auto short_packet = packet; short_packet.data.resize(n);
        CHECK_FALSE(decode_achievement_primary(short_packet, &data));
    }
    ByteWriter sparse; sparse.le<std::uint32_t>(9); sparse.le<std::uint8_t>(0); sparse.le<std::uint32_t>(1);
    sparse.le<std::uint32_t>(900); fixture::state(sparse);
    const auto small = catalog();
    CHECK_FALSE(decode_achievement_primary({kOpAchievementPrimary, sparse.take()}, &small));
}

TEST_CASE("achievement packets: exact progress framing preserves full requirement IDs independently of display order") {
    const auto value = decode_achievement_progress(fixture::progress(9, 70000));
    REQUIRE(value);
    REQUIRE(value->size() == 1);
    CHECK((*value)[0].achievement_id == 900);
    CHECK((*value)[0].component_id == 201);
    CHECK((*value)[0].requirement_id == 70000);
    CHECK((*value)[0].requirement_type == 1);
    CHECK((*value)[0].current_count == 9);
    auto kind = fixture::progress(); kind.data[16] = std::byte{4}; // a requirement kind, not a bucket
    const auto typed = decode_achievement_progress(kind);
    REQUIRE(typed); CHECK((*typed)[0].requirement_type == 4);
    auto trailing = fixture::progress(); trailing.data.push_back(std::byte{0});
    CHECK_FALSE(decode_achievement_progress(trailing));
    ByteWriter empty; empty.le<std::uint32_t>(0);
    const auto zero = decode_achievement_progress({kOpAchievementProgress, empty.take()});
    REQUIRE(zero); CHECK(zero->empty());
}

TEST_CASE("achievement packets: link snapshots and canonical sharing keep known counts and signed words") {
    auto data = catalog();
    const auto link = parse_achievement_link(fixture::metadata(), data);
    REQUIRE(link);
    CHECK(link->achievement_id == 900);
    CHECK(link->state.counts[1][0] == 4);
    CHECK(link->state.counts[0][0] == 4);
    const auto shared = make_achievement_link("Peer", data.definitions[1], link->state);
    REQUIRE(shared); CHECK(shared->metadata == fixture::metadata());
    const auto request = encode_achievement_link_request(*link);
    CHECK(request.opcode == 0x675d);
    ByteReader r(request.data); CHECK(r.cstring() == "Peer"); CHECK(r.le<std::uint32_t>() == 900);
    CHECK(r.le<std::int16_t>() == 1); CHECK(r.le<std::uint16_t>() == 0); CHECK(r.le<std::uint16_t>() == 0);
    CHECK(r.le<std::uint32_t>() == 4); CHECK(r.le<std::uint32_t>() == 4);
    CHECK(r.le<std::uint8_t>() == 0); CHECK(r.empty());
    auto unknown = link->state; unknown.counts[1][0].reset();
    CHECK_FALSE(make_achievement_link("Peer", data.definitions[1], unknown));
    auto overflow = link->state; overflow.counts[1][0] = 0x100000000ull;
    CHECK_FALSE(make_achievement_link("Peer", data.definitions[1], overflow));
    for (const auto& invalid : {std::string("Pe'er"), std::string("Pe^er"), std::string("Pe\x12" "er"), std::string("Pe\ner")}) {
        CHECK_FALSE(make_achievement_link(invalid, data.definitions[1], link->state));
        CHECK_FALSE(parse_achievement_link(fixture::metadata(invalid), data));
    }
    for (std::size_t n = 0; n < fixture::metadata().size(); ++n)
        CHECK_FALSE(parse_achievement_link(std::string_view(fixture::metadata()).substr(0, n), data));
    CHECK_FALSE(parse_achievement_link(fixture::metadata() + "0^", data));
    CHECK_FALSE(parse_achievement_link("Peer^900^4^0^0^4^4^", data));
    CHECK_FALSE(parse_achievement_link("Peer^900^1^32768^0^4^4^", data));
    auto& definition = data.definitions[1];
    definition.components[1].resize(17); definition.components[0].clear();
    AchievementState words; words.status = AchievementStatus::Completed; words.completed_at = 99;
    words.satisfied[1].resize(17); words.satisfied[1][0] = words.satisfied[1][15] = words.satisfied[1][16] = 1;
    words.counts[1].resize(17, std::uint64_t{1});
    const auto signed_link = make_achievement_link("Peer", definition, words);
    REQUIRE(signed_link);
    CHECK(signed_link->metadata.starts_with("Peer^900^0^-32767^1^99^"));
    REQUIRE(parse_achievement_link(signed_link->metadata, data));
}

TEST_CASE("achievement packets: authoritative inspect and notifications never conflate IDs or raw metadata") {
    const auto data = catalog();
    const auto inspect = decode_achievement_inspect(fixture::inspect(), &data);
    REQUIRE(inspect); CHECK(inspect->player_name == "Peer"); CHECK(inspect->achievement_id == 900);
    const auto snapshot = decode_achievement_snapshot(fixture::comparison(), &data);
    REQUIRE(snapshot); REQUIRE(snapshot->size() == 2);
    CHECK((*snapshot)[1].status == AchievementStatus::Completed);
    CHECK_FALSE((*snapshot)[1].counts[1][0]);
    ByteWriter earned; earned.le<std::uint32_t>(777); earned.le<std::uint32_t>(900); earned.le<std::uint32_t>(3695);
    fixture::string(earned, fixture::metadata());
    const AppPacket event(kOpAchievementEarned, earned.take());
    const auto decoded = decode_achievement_earned(event, &data);
    REQUIRE(decoded); CHECK(decoded->spawn_id == 777); CHECK(decoded->sound_id == 3695);
    ByteWriter guild; guild.le<std::uint32_t>(3); guild.le<std::uint64_t>(0); guild.fixed_string("Peer", 64);
    guild.le<std::uint32_t>(0); guild.le<std::uint32_t>(900); fixture::string(guild, fixture::metadata());
    const AppPacket guild_packet(to_wire(Op::GuildUpdate), guild.take());
    const auto announcement = decode_achievement_guild(guild_packet, &data);
    REQUIRE(announcement); CHECK(announcement->player_name == "Peer"); CHECK(announcement->achievement_id == 900);
    for (const auto& packet : {fixture::inspect(), event, guild_packet}) for (std::size_t n = 0; n < packet.data.size(); ++n) {
        auto short_packet = packet; short_packet.data.resize(n);
        if (packet.opcode == kOpAchievementInspect) CHECK_FALSE(decode_achievement_inspect(short_packet, &data));
        if (packet.opcode == kOpAchievementEarned) CHECK_FALSE(decode_achievement_earned(short_packet, &data));
        if (packet.opcode == to_wire(Op::GuildUpdate)) CHECK_FALSE(decode_achievement_guild(short_packet, &data));
    }
    const auto open = encode_achievement_window_request();
    CHECK(open.opcode == 0x47cb); REQUIRE(open.data.size() == 1); CHECK(open.data[0] == std::byte{0x30});
    CHECK(decode_achievement_window(AppPacket(0x171e)));
    CHECK_FALSE(decode_achievement_window(AppPacket(0x171e, {std::byte{0}})));
}
