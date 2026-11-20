#include <doctest/doctest.h>

#include <algorithm>

#include "manual_clock.h"
#include "mock_server/harness.h"
#include "mq_test_world.h"
#include "net/protocol/rof2/packets/reward_packets.h"
#include "net/protocol/rof2/packets/task_packets.h"
#include "plugin/hooks.h"
#include "rof2/reward_test_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;
namespace fixture = hailstone::test::reward_fixture;

namespace {
// Direct semantic fixtures for GameConnection behavior. The separate fixture:: functions remain
// wire-only inputs for the loopback codec tests below.
RewardSet reward_set(std::uint32_t pending = 42, std::uint32_t set_id = 700,
                     std::uint32_t option_id = 12, std::uint32_t item_id = 1000) {
    RewardSet set;
    set.pending_id = pending;
    set.set_id = set_id;
    set.title = "Fixture choices";
    set.multiplier = 1.0f;
    RewardOption shared;
    shared.wire_id = 1;
    shared.common = true;
    shared.label = "Common";
    RewardEntry text;
    text.type = RewardEntryType::Text;
    text.entry_id = 10;
    text.description = "Shared grant";
    shared.entries.push_back(std::move(text));
    RewardOption choice;
    choice.wire_id = option_id;
    choice.label = "Choose item";
    RewardEntry reward;
    reward.type = RewardEntryType::Item;
    reward.entry_id = 20;
    reward.description = "Fixture reward";
    reward.items.push_back(RewardItem{item_id, 2, true, 77, "Synthetic reward"});
    choice.entries.push_back(std::move(reward));
    set.options.push_back(std::move(shared));
    set.options.push_back(std::move(choice));
    return set;
}
RewardPacket semantic_collection(RewardLane lane = RewardLane::Pending,
                                 std::uint32_t pending = 42, std::uint32_t set_id = 700,
                                 std::uint32_t option = 12, std::uint32_t item = 1000) {
    RewardPacket packet;
    packet.lane = lane;
    packet.replace_collection = lane == RewardLane::Preview;
    packet.sets.push_back(reward_set(pending, set_id, option, item));
    return packet;
}
RewardPacket semantic_bulk(RewardLane lane) {
    RewardPacket packet;
    packet.lane = lane;
    packet.replace_collection = true;
    packet.sets.push_back(reward_set());
    packet.sets.push_back(reward_set(43, 701, 13, 1001));
    return packet;
}
RewardPacket semantic_clear(RewardLane lane, bool bulk) {
    RewardPacket packet;
    packet.lane = lane;
    packet.replace_collection = bulk || lane == RewardLane::Preview;
    return packet;
}
RewardPacket semantic_claim(bool success) {
    RewardPacket packet;
    packet.kind = RewardPacket::Kind::Claim;
    packet.claim = RewardClaimReply{42, 700, 12, success};
    return packet;
}
messages::GameMessage reward_message(RewardPacket packet) {
    return {packet.lane == RewardLane::Pending ? messages::GameOpcode::AchievementReward
                                              : messages::GameOpcode::RewardSelection, std::move(packet)};
}
std::uint32_t action(const AppPacket& packet) {
    if (packet.data.size() < 4) return 0xffffffffu;
    ByteReader r(packet.data); return r.le<std::uint32_t>();
}
std::size_t requests(const Rig& rig, std::uint16_t opcode, std::uint32_t kind) {
    return static_cast<std::size_t>(std::count_if(rig.server.zone_log.begin(), rig.server.zone_log.end(),
        [&](const AppPacket& p) { return p.opcode == opcode && action(p) == kind; }));
}
void barrier(Rig& rig) {
    const auto count = [&] { return std::count_if(rig.server.zone_log.begin(), rig.server.zone_log.end(),
        [](const AppPacket& packet) { return packet.opcode == rof2::to_wire(rof2::Op::RandomReq); }); };
    const auto before = count();
    rig.conn.send_zone_message(messages::outgoing(messages::GameOpcode::RandomReq, RandomRollIntent{1, 1}));
    REQUIRE(rig.until([&] { return count() > before; }));
}
void collection(Rig& rig, RewardLane lane = RewardLane::Pending, std::uint32_t option = 12,
                std::uint32_t item = 1234567) {
    const auto old = rig.conn.rewards().lane(lane).revision;
    rig.server.zone.send_reliable(fixture::collection(lane, option, item));
    REQUIRE(rig.until([&] { return rig.conn.rewards().lane(lane).revision != old; }));
    REQUIRE(rig.conn.rewards().lane(lane).tabs.size() == 1);
}
bool claim(Rig& rig, std::uint32_t option = 12) {
    const auto& lane = rig.conn.rewards().lane(RewardLane::Pending);
    return rig.conn.reward_claim(lane.revision, lane.tabs.at(0).token, option);
}
bool inspect(Rig& rig, RewardLane value = RewardLane::Pending, std::uint32_t option = 12,
             std::uint32_t item = 1234567) {
    const auto& lane = rig.conn.rewards().lane(value);
    return rig.conn.reward_inspect(value, lane.revision, lane.tabs.at(0).token, option, 20, item);
}
} // namespace

TEST_CASE("reward connection: stock requests, both lanes and serialized inspection use ordinary read-only display") {
    Rig rig;
    REQUIRE(rig.to_zone());
    CHECK(requests(rig, 0x6411, 6) == 0); // zone entry is not an automatic restore/claim loop
    REQUIRE(rig.conn.request_pending_rewards());
    REQUIRE(rig.until([&] { return requests(rig, 0x6411, 6) == 1; }));
    CHECK_FALSE(rig.conn.request_pending_rewards()); // bounded explicit refresh
    collection(rig);
    collection(rig, RewardLane::Preview, 93);
    const auto inventory_revision = rig.conn.inventory().revision();
    const auto inventory_size = rig.conn.inventory().items().size();
    CHECK_FALSE(inspect(rig, RewardLane::Pending, 999));
    CHECK_FALSE(inspect(rig, RewardLane::Pending, 12, 777));
    REQUIRE(inspect(rig));
    REQUIRE(rig.until([&] { return requests(rig, 0x6411, 1) == 1; }));
    const auto request = std::find_if(rig.server.zone_log.begin(), rig.server.zone_log.end(),
        [](const AppPacket& p) { return p.opcode == 0x6411 && action(p) == 1; });
    REQUIRE(request != rig.server.zone_log.end());
    REQUIRE(request->data.size() == 20);
    ByteReader reader(request->data);
    for (const auto field : {1u, 700u, 12u, 20u, 1234567u}) CHECK(reader.le<std::uint32_t>() == field);
    Item item; item.id = 1234567; item.name = "Received reward preview"; item.serial = "0000000000000099";
    item.slot = ItemSlot::possessions(0);
    rig.server.zone.send_reliable(fixture::inspection(RewardLane::Preview, item)); // wrong lane has no request
    rig.server.zone.send_reliable(fixture::inspection(RewardLane::Pending, item, 55)); // mismatched item rejected
    rig.server.zone.send_reliable(fixture::inspection(RewardLane::Pending, item));
    std::optional<Item> displayed;
    REQUIRE(rig.until([&] { displayed = rig.conn.take_item_display(); return displayed.has_value(); }));
    CHECK(displayed->id == 1234567);
    CHECK(displayed->name == "Received reward preview");
    CHECK(rig.conn.inventory().revision() == inventory_revision);
    CHECK(rig.conn.inventory().items().size() == inventory_size);
    REQUIRE(inspect(rig, RewardLane::Preview, 93));
    REQUIRE(rig.until([&] { return requests(rig, 0x6471, 1) == 1; }));
    rig.server.zone.send_reliable(fixture::inspection(RewardLane::Preview, item));
    REQUIRE(rig.until([&] { displayed = rig.conn.take_item_display(); return displayed.has_value(); }));
    REQUIRE(claim(rig));
    REQUIRE(rig.until([&] { return requests(rig, 0x6411, 3) == 1; }));
    CHECK_FALSE(claim(rig));
    CHECK_FALSE(inspect(rig));
    auto wrong_lane = fixture::claim(true); wrong_lane.opcode = 0x6471;
    rig.server.zone.send_reliable(wrong_lane);
    rig.server.zone.send_reliable(fixture::claim(true, 99));
    rig.server.zone.send_reliable(fixture::claim(true));
    REQUIRE(rig.until([&] { return !rig.conn.rewards().lane(RewardLane::Pending).claim_pending; }));
    CHECK(rig.conn.rewards().lane(RewardLane::Pending).tabs.empty());
    CHECK(rig.conn.rewards().lane(RewardLane::Preview).tabs.size() == 1);
    CHECK(rig.conn.inventory().revision() == inventory_revision); // claim ACK does not grant an item locally
    barrier(rig);
    CHECK(requests(rig, 0x6411, 3) == 1);
    CHECK(requests(rig, 0x6471, 3) == 0);
}

TEST_CASE("reward connection: malformed replacement, stale confirmation and uncertain claim cannot replay") {
    ManualClock clock;
    auto options = quick_options(); options.world_protocol.session_config.clock = clock.source(); options.zone_protocol.session_config.clock = clock.source();
    Rig rig(MockConfig::standard(), options);
    REQUIRE(rig.to_zone());
    collection(rig);
    const auto old = rig.conn.rewards().lane(RewardLane::Pending);
    auto malformed = fixture::bulk(); malformed.data.pop_back();
    CHECK_FALSE(decode_reward_packet(malformed));
    CHECK(rig.conn.rewards().lane(RewardLane::Pending).revision == old.revision);
    collection(rig, RewardLane::Pending, 71);
    CHECK_FALSE(rig.conn.reward_claim(old.revision, old.tabs[0].token, 12));
    clock.hold();
    REQUIRE(claim(rig, 71));
    REQUIRE(rig.until([&] { return requests(rig, 0x6411, 3) == 1; }));
    clock.advance(std::chrono::milliseconds(game::RewardSelection::kRequestTimeoutMs));
    rig.step();
    CHECK(rig.conn.rewards().lane(RewardLane::Pending).claim_uncertain);
    REQUIRE(rig.conn.request_pending_rewards());
    REQUIRE(rig.until([&] { return requests(rig, 0x6411, 6) == 1; }));
    collection(rig, RewardLane::Pending, 71);
    const auto fresh = rig.conn.rewards().lane(RewardLane::Pending);
    CHECK_FALSE(claim(rig, 71));
    rig.server.zone.send_reliable(fixture::claim(true, 71));
    REQUIRE(rig.until([&] { return !rig.conn.rewards().lane(RewardLane::Pending).claim_uncertain; }));
    REQUIRE(rig.conn.rewards().lane(RewardLane::Pending).tabs.size() == 1);
    CHECK(rig.conn.rewards().lane(RewardLane::Pending).tabs[0].token == fresh.tabs[0].token);
    barrier(rig);
    CHECK(requests(rig, 0x6411, 3) == 1);
    rig.conn.disconnect();
    CHECK(rig.conn.rewards().lane(RewardLane::Pending).tabs.empty());
    CHECK_FALSE(rig.conn.rewards().lane(RewardLane::Pending).shown);
}

TEST_CASE("reward connection: outgoing hooks cannot send a retired confirmation or corrupt nested terminal state") {
    for (const int mode : {0, 1, 2, 3}) {
        CAPTURE(mode);
        plugin::Hooks hooks;
        auto options = quick_options(); options.hooks = &hooks;
        Rig rig(MockConfig::standard(), options);
        REQUIRE(rig.to_zone());
        collection(rig);
        const auto owner = hooks.new_owner("reward test");
        hooks.on_packet(owner, plugin::Stream::Zone, plugin::Direction::Out, 0x6411,
            [&](AppPacket& packet) {
                if (action(packet) != 3) return plugin::Flow::Continue;
                if (mode == 0) return plugin::Flow::Drop;
                if (mode == 1) rig.conn.close_reward_selection(RewardLane::Pending);
                if (mode == 2) rig.conn.receive_zone_message_for_test(reward_message(semantic_collection()));
                if (mode == 3) rig.conn.receive_zone_message_for_test(reward_message(semantic_claim(false)));
                return plugin::Flow::Continue;
            });
        CHECK_FALSE(claim(rig));
        CHECK_FALSE(rig.conn.rewards().lane(RewardLane::Pending).claim_pending);
        CHECK_FALSE(rig.conn.rewards().lane(RewardLane::Pending).claim_uncertain);
        REQUIRE(rig.conn.rewards().lane(RewardLane::Pending).tabs.size() == 1);
        if (mode == 3) CHECK(rig.conn.rewards().lane(RewardLane::Pending).tabs[0].blocked);
        hooks.remove(owner);
        barrier(rig);
        CHECK(requests(rig, 0x6411, 3) == 0);
        if (mode == 0 || mode == 2) CHECK(claim(rig));
    }
}

TEST_CASE("reward connection: task preview requires received reward flag and zone retirement removes both lanes") {
    Rig rig;
    REQUIRE(rig.to_zone());
    CHECK_FALSE(rig.conn.preview_task_reward(999));
    game::TaskOffers offers;
    game::TaskOffer offer; offer.task_id = 999; offer.title = "Synthetic task"; offer.reward_preview = true;
    offers.offers.push_back(offer);
    rig.conn.receive_zone_message_for_test({messages::GameOpcode::TaskSelectWindow, offers});
    REQUIRE(rig.conn.preview_task_reward(999));
    REQUIRE(rig.until([&] { return requests(rig, 0x6471, 4) == 1; }));
    const auto request = std::find_if(rig.server.zone_log.begin(), rig.server.zone_log.end(),
        [](const AppPacket& p) { return p.opcode == 0x6471 && action(p) == 4; });
    REQUIRE(request != rig.server.zone_log.end());
    REQUIRE(request->data.size() == 8);
    ByteReader reader(request->data); CHECK(reader.le<std::uint32_t>() == 4); CHECK(reader.le<std::uint32_t>() == 999);
    collection(rig);
    collection(rig, RewardLane::Preview);
    GameConnectionTestAccess::status(rig.conn).dead = true;
    REQUIRE(inspect(rig, RewardLane::Preview));
    REQUIRE(claim(rig)); // native reward senders impose no additional living/HP gate
    GameConnectionTestAccess::status(rig.conn).dead = false;
    rig.server.request_zone_change(2, 10, 20, 30);
    REQUIRE(rig.until([&] { return rig.conn.state() == ConnectionState::Zoning; }));
    CHECK(rig.conn.rewards().lane(RewardLane::Pending).tabs.empty());
    CHECK(rig.conn.rewards().lane(RewardLane::Preview).tabs.empty());
    CHECK_FALSE(rig.conn.request_pending_rewards());
    CHECK_FALSE(rig.conn.rewards().lane(RewardLane::Pending).shown);
}

TEST_CASE("reward connection: outgoing explicit-open hook close retires even an initially hidden page") {
    for (const auto lane : {RewardLane::Pending, RewardLane::Preview}) {
        plugin::Hooks hooks;
        auto options = quick_options(); options.hooks = &hooks;
        Rig rig(MockConfig::standard(), options);
        REQUIRE(rig.to_zone());
        game::TaskOffers offers;
        game::TaskOffer offer; offer.task_id = 999; offer.reward_preview = true;
        offers.offers.push_back(offer);
        rig.conn.receive_zone_message_for_test({messages::GameOpcode::TaskSelectWindow, offers});
        REQUIRE_FALSE(rig.conn.rewards().lane(lane).shown);
        const auto owner = hooks.new_owner("reward open test");
        const auto kind = lane == RewardLane::Pending ? 6u : 4u;
        hooks.on_packet(owner, plugin::Stream::Zone, plugin::Direction::Out, reward_opcode(lane),
            [&](AppPacket& packet) {
                if (action(packet) == kind) rig.conn.close_reward_selection(lane);
                return plugin::Flow::Continue;
            });
        if (lane == RewardLane::Pending) CHECK_FALSE(rig.conn.request_pending_rewards());
        else CHECK_FALSE(rig.conn.preview_task_reward(999));
        CHECK_FALSE(rig.conn.rewards().lane(lane).shown);
        hooks.remove(owner);
        barrier(rig);
        CHECK(requests(rig, reward_opcode(lane), kind) == 0);
    }
}

TEST_CASE("reward connection: native single and bulk collection actions preserve the correct lane identities") {
    Rig rig;
    REQUIRE(rig.to_zone());
    rig.conn.receive_zone_message_for_test(reward_message(semantic_bulk(RewardLane::Pending)));
    const auto old = rig.conn.rewards().lane(RewardLane::Pending);
    REQUIRE(old.tabs.size() == 2);
    rig.conn.receive_zone_message_for_test(reward_message(semantic_collection(RewardLane::Pending, 44, 702, 14)));
    REQUIRE(rig.conn.rewards().lane(RewardLane::Pending).tabs.size() == 3);
    rig.conn.receive_zone_message_for_test(reward_message(semantic_collection(RewardLane::Pending, 43, 999, 71)));
    const auto current = rig.conn.rewards().lane(RewardLane::Pending);
    REQUIRE(current.tabs.size() == 3);
    CHECK(current.tabs[0].token == old.tabs[0].token);
    CHECK_FALSE(rig.conn.reward_claim(current.revision, old.tabs[1].token, 13));
    rig.conn.receive_zone_message_for_test(reward_message(semantic_clear(RewardLane::Pending, false)));
    CHECK(rig.conn.rewards().lane(RewardLane::Pending).revision == current.revision);
    rig.conn.receive_zone_message_for_test(reward_message(semantic_bulk(RewardLane::Preview)));
    REQUIRE(rig.conn.rewards().lane(RewardLane::Preview).tabs.size() == 2);
    rig.conn.receive_zone_message_for_test(reward_message(semantic_collection(RewardLane::Preview, 44, 702, 14)));
    CHECK(rig.conn.rewards().lane(RewardLane::Preview).tabs.size() == 1);
    for (const auto lane : {RewardLane::Pending, RewardLane::Preview}) {
        rig.conn.receive_zone_message_for_test(reward_message(semantic_clear(lane, true)));
        CHECK(rig.conn.rewards().lane(lane).tabs.empty());
    }
}

TEST_CASE("reward connection: preview handoff rechecks the received task and preview flag after hooks") {
    for (const bool remove : {false, true}) {
        plugin::Hooks hooks;
        auto options = quick_options(); options.hooks = &hooks;
        Rig rig(MockConfig::standard(), options);
        REQUIRE(rig.to_zone());
        game::TaskOffers offers;
        game::TaskOffer offer; offer.task_id = 999; offer.reward_preview = true;
        offers.offers.push_back(offer);
        rig.conn.receive_zone_message_for_test({messages::GameOpcode::TaskSelectWindow, offers});
        const auto owner = hooks.new_owner("reward preview task test");
        hooks.on_packet(owner, plugin::Stream::Zone, plugin::Direction::Out, 0x6471,
            [&](AppPacket& packet) {
                if (action(packet) == 4) {
                    if (remove) offers.offers.clear();
                    else offers.offers[0].reward_preview = false;
                    rig.conn.receive_zone_message_for_test({messages::GameOpcode::TaskSelectWindow, offers});
                }
                return plugin::Flow::Continue;
            });
        CHECK_FALSE(rig.conn.preview_task_reward(999));
        CHECK_FALSE(rig.conn.rewards().lane(RewardLane::Preview).shown);
        hooks.remove(owner);
        barrier(rig);
        CHECK(requests(rig, 0x6471, 4) == 0);
    }
}
