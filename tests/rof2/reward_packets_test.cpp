#include <doctest/doctest.h>

#include <limits>

#include "reward_test_packets.h"

using namespace hailstone;
using namespace hailstone::net;
namespace fixture = hailstone::test::reward_fixture;

TEST_CASE("reward packets: native request opcodes, actions and exact identity fields") {
    CHECK(rof2::to_wire(rof2::Op::AchievementReward) == 0x6411);
    CHECK(rof2::to_wire(rof2::Op::RewardSelection) == 0x6471);
    const auto verify = [](const AppPacket& packet, std::uint16_t opcode, std::initializer_list<std::uint32_t> fields) {
        CHECK(packet.opcode == opcode);
        REQUIRE(packet.data.size() == fields.size() * 4);
        ByteReader reader(packet.data);
        for (const auto field : fields) CHECK(reader.le<std::uint32_t>() == field);
        CHECK(reader.empty());
    };
    verify(encode_pending_rewards_request(), 0x6411, {6});
    verify(encode_task_reward_preview(1234), 0x6471, {4, 1234});
    verify(encode_achievement_reward_view(0), 0x6411, {5, 0});
    verify(encode_achievement_reward_view(8), 0x6411, {5, 8});
    verify(encode_reward_inspect(RewardLane::Pending, 700, 93, 777, 1234567), 0x6411, {1, 700, 93, 777, 1234567});
    verify(encode_reward_inspect(RewardLane::Preview, 700, 93, 777, 1234567), 0x6471, {1, 700, 93, 777, 1234567});
    verify(encode_reward_claim(42, 700, 93), 0x6411, {3, 42, 700, 93, 0});
}

TEST_CASE("reward packets: literal collections keep common choices, provider ID collisions and wire IDs") {
    const auto result = decode_reward_packet(fixture::bulk());
    REQUIRE(result);
    CHECK(result->kind == RewardPacket::Kind::Collection);
    REQUIRE(result->sets.size() == 2);
    CHECK(result->sets[0].pending_id == result->sets[1].pending_id);
    CHECK(result->sets[0].set_id == result->sets[1].set_id);
    REQUIRE(result->sets[0].options.size() == 2);
    CHECK(result->sets[0].options[0].common);
    CHECK(result->sets[0].options[1].wire_id == 12);
    CHECK(result->sets[1].options[1].wire_id == 13);
    const auto& item = result->sets[0].options[1].entries[0].items[0];
    CHECK(item.id == 1000);
    CHECK(item.quantity == 2);
    CHECK(item.lore);
    CHECK(item.lore_group == 77);
    CHECK(item.name == "Synthetic reward");
    for (const auto lane : {RewardLane::Pending, RewardLane::Preview}) {
        ByteWriter w; w.le<std::uint32_t>(0); w.le<std::uint8_t>(0);
        const auto clear = decode_reward_packet({reward_opcode(lane), w.take()});
        REQUIRE(clear);
        CHECK(clear->lane == lane);
        CHECK(clear->replace_collection == (lane == RewardLane::Preview));
        CHECK(clear->sets.empty());
        ByteWriter b; b.le<std::uint32_t>(7); b.le<std::int32_t>(0); b.le<std::uint8_t>(0);
        const auto bulk_clear = decode_reward_packet({reward_opcode(lane), b.take()});
        REQUIRE(bulk_clear);
        CHECK(bulk_clear->replace_collection);
        CHECK(bulk_clear->sets.empty());
    }
}

TEST_CASE("reward packets: every supported typed entry has its own literal suffix") {
    ByteWriter w;
    w.le<std::uint32_t>(0); w.le<std::uint8_t>(1);
    w.le<std::uint32_t>(9); w.le<std::uint32_t>(10); fixture::string(w, "Kinds"); w.f32(1.5f);
    w.le<std::uint32_t>(10); w.le<std::int32_t>(1);
    w.le<std::uint32_t>(88); w.le<std::uint32_t>(7); w.le<std::uint8_t>(0); w.le<std::uint8_t>(9);
    fixture::string(w, "Seven entries");
    for (const auto type : {0u, 1u, 2u, 3u, 4u, 5u, 11u}) {
        w.le(type); w.le(type + 100); w.le(type + 200); fixture::string(w, "Literal & <description>");
        if (type == 1 || type == 11) for (const auto value : {11u, 22u, 33u, 44u}) w.le(value);
        if (type == 2) {
            w.le<std::uint32_t>(1); w.le<std::uint32_t>(2345678); w.le<std::uint32_t>(7);
            w.le<std::uint8_t>(0); w.le<std::uint32_t>(0); fixture::string(w, "Authored item");
        }
        if (type == 4) { w.le<std::uint32_t>(987); w.le<std::uint32_t>(3); }
        if (type == 5) { w.le<std::uint32_t>(123); w.le<std::uint8_t>(1); }
    }
    const auto result = decode_reward_packet({0x6471, w.take()});
    REQUIRE(result);
    REQUIRE(result->sets.size() == 1);
    CHECK(result->sets[0].multiplier == doctest::Approx(1.5f));
    const auto& entries = result->sets[0].options[0].entries;
    REQUIRE(entries.size() == 7);
    CHECK(entries[0].entry_id == 100);
    CHECK(entries[0].string_id == 200);
    CHECK(entries[0].description == "Literal & <description>");
    CHECK(entries[1].values == std::array<std::uint32_t, 4>{11, 22, 33, 44});
    CHECK(entries[2].items[0].id == 2345678);
    CHECK(entries[2].items[0].quantity == 7);
    CHECK(entries[3].type == RewardEntryType::Experience);
    CHECK(entries[3].values == std::array<std::uint32_t, 4>{});
    CHECK(entries[4].values[0] == 987);
    CHECK(entries[4].values[1] == 3);
    CHECK(entries[5].values[0] == 123);
    CHECK(entries[5].flag == 1);
    CHECK(entries[6].values[2] == 33);
    CHECK(entries[6].values[3] == 44);
}

TEST_CASE("reward packets: inspection is raw serialized item at byte eight and stays read-only") {
    Item item;
    item.id = 1234567; item.name = "Synthetic preview"; item.serial = "0000000000000042";
    item.slot = ItemSlot::possessions(0); item.stack_count = 2;
    BagItem child; child.index = 0; child.item.id = 7654321; child.item.name = "Synthetic augment";
    item.contents.push_back(child);
    for (const auto lane : {RewardLane::Pending, RewardLane::Preview}) {
        const auto packet = fixture::inspection(lane, item);
        const auto result = decode_reward_packet(packet);
        REQUIRE(result);
        REQUIRE(result->inspection);
        CHECK(result->inspection->item.id == item.id);
        REQUIRE(result->inspection->item.contents.size() == 1);
        CHECK(result->inspection->item.contents[0].item.id == 7654321);
        CHECK_FALSE(decode_reward_packet(fixture::inspection(lane, item, 99)));
        for (std::size_t i = 0; i < packet.data.size(); ++i) {
            auto short_packet = packet; short_packet.data.resize(i);
            CHECK_FALSE(decode_reward_packet(short_packet));
        }
        auto trailing = packet; trailing.data.push_back(std::byte{0});
        CHECK_FALSE(decode_reward_packet(trailing));
    }
}

TEST_CASE("reward packets: truncated, oversized, nonfinite and unknown payloads fail closed") {
    for (const auto& packet : {fixture::collection(), fixture::bulk(), fixture::claim(true)}) {
        for (std::size_t i = 0; i < packet.data.size(); ++i) {
            auto short_packet = packet; short_packet.data.resize(i);
            CHECK_FALSE(decode_reward_packet(short_packet));
        }
        auto trailing = packet; trailing.data.push_back(std::byte{0});
        CHECK_FALSE(decode_reward_packet(trailing));
    }
    auto preview_claim = fixture::claim(true); preview_claim.opcode = 0x6471;
    CHECK_FALSE(decode_reward_packet(preview_claim));
    auto unknown_op = fixture::collection(); unknown_op.opcode = 1;
    CHECK_FALSE(decode_reward_packet(unknown_op));
    auto bad_boolean = fixture::collection(); bad_boolean.data[4] = std::byte{2};
    CHECK_FALSE(decode_reward_packet(bad_boolean));
    for (const auto count : {-1, 257, std::numeric_limits<int>::max()}) {
        ByteWriter w; w.le<std::uint32_t>(7); w.le<std::int32_t>(count); w.le<std::uint8_t>(0);
        CHECK_FALSE(decode_reward_packet({0x6411, w.take()}));
    }
    for (const auto multiplier : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        ByteWriter w; w.le<std::uint32_t>(0); w.le<std::uint8_t>(1);
        w.le<std::uint32_t>(1); w.le<std::uint32_t>(2); fixture::string(w, ""); w.f32(multiplier);
        w.le<std::uint32_t>(2); w.le<std::int32_t>(0);
        CHECK_FALSE(decode_reward_packet({0x6411, w.take()}));
    }
    ByteWriter unknown;
    unknown.le<std::uint32_t>(0); unknown.le<std::uint8_t>(1);
    unknown.le<std::uint32_t>(1); unknown.le<std::uint32_t>(2); fixture::string(unknown, ""); unknown.f32(1);
    unknown.le<std::uint32_t>(2); unknown.le<std::int32_t>(1);
    unknown.le<std::uint32_t>(1); unknown.le<std::uint32_t>(1); unknown.le<std::uint8_t>(0); unknown.le<std::uint8_t>(0);
    fixture::string(unknown, ""); unknown.le<std::uint32_t>(6); unknown.le<std::uint32_t>(1); unknown.le<std::uint32_t>(0);
    fixture::string(unknown, "");
    CHECK_FALSE(decode_reward_packet({0x6411, unknown.take()}));
    AppPacket huge(0x6411, std::vector<std::byte>(kMaxRewardPacketBytes + 1));
    CHECK_FALSE(decode_reward_packet(huge));
}
