// Hover respawn against the loopback EQEmu harness: the corpse's ID survives death, while same-zone completion
// publishes a new living player without a profile or zone-in. Packets follow EQEmu's documented RoF2 layouts.
#include <doctest/doctest.h>

#include <algorithm>
#include <string_view>

#include "core/byte_io.h"
#include "game/coords.h"
#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"
#include "net/messages/zone_offer.h"
#include "net/messages/message.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/packets/spell_packets.h"
#include "plugin/hooks.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

AppPacket hover_window(std::uint32_t delay = 0, std::uint32_t remaining = 300000,
                       std::string_view rez_name = "Resurrect", std::vector<std::uint32_t> ids = {0, 1}) {
    ByteWriter w;
    w.le<std::uint32_t>(0);
    w.le(remaining);
    w.le(delay);
    w.le(static_cast<std::uint32_t>(ids.size()));
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const bool final = i + 1 == ids.size();
        w.le(ids[i]);
        w.le<std::uint32_t>(2);
        for (int component = 0; component < 4; ++component) w.f32(0);
        const std::string_view name = final ? rez_name : "Bind Location";
        w.bytes(std::as_bytes(std::span(name.data(), name.size())));
        w.le<std::uint8_t>(0);
        w.le<std::uint8_t>(final ? 1 : 0); // availability, not a resurrection type
    }
    return AppPacket(rof2::to_wire(rof2::Op::RespawnWindow), w.take()); // no legacy trailing padding
}

AppPacket bind_packet(std::uint16_t zone = 2, std::uint16_t instance = 0,
                      std::uint32_t mana = 0, std::uint32_t endurance = 0) {
    ByteWriter w;
    w.le(zone);
    w.le(instance);
    w.f32(40);
    w.f32(30);
    w.f32(12);
    w.f32(256);
    const std::string name = "Respawn destination";
    w.bytes(std::as_bytes(std::span(name.data(), name.size())));
    w.le<std::uint8_t>(0);
    w.le<std::uint8_t>(1);
    w.le<std::uint32_t>(0); // EQEmu's RoF2 encoder writes zero placeholders for all three vitals
    w.le(mana);
    w.le(endurance);
    return AppPacket(rof2::to_wire(rof2::Op::ZonePlayerToBind), w.take());
}

AppPacket rez_offer(std::uint32_t spell = 100, std::uint16_t zone = 2, std::uint16_t instance = 0) {
    ByteWriter w;
    w.le<std::uint32_t>(0);
    w.le(zone);
    w.le(instance);
    w.f32(30);
    w.f32(40);
    w.f32(12);
    w.le<std::uint32_t>(0);
    w.fixed_string("Gennro", 64);
    w.le<std::uint32_t>(0);
    w.fixed_string("Healer", 64);
    w.le(spell);
    w.fixed_string("Gennro's corpse", 64);
    w.le<std::uint32_t>(0);
    w.zeros(8);
    return AppPacket(rof2::to_wire(rof2::Op::RezzRequest), w.take());
}

AppPacket points(rof2::Op op, std::uint16_t id, std::uint32_t current, std::uint32_t maximum) {
    ByteWriter w;
    w.le(current);
    w.le(maximum);
    w.le(id);
    return AppPacket(rof2::to_wire(op), w.take());
}

// Gameplay tests inject decoded semantic messages. The wire helpers above are
// used only by the independent loopback-server and byte-encoding checks.
messages::GameMessage hover_message(std::uint32_t delay = 0, std::uint32_t remaining = 300000,
                                    std::string_view rez_name = "Resurrect",
                                    std::vector<std::uint32_t> ids = {0, 1}) {
    RespawnWindow window;
    window.time_remaining_ms = remaining;
    window.delay_ms = delay;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const bool last = i + 1 == ids.size();
        window.options.push_back(RespawnOption{ids[i], 2, 0, 0, 0, 0,
                            std::string(last ? rez_name : "Bind Location"),
                            static_cast<std::uint8_t>(last ? 1 : 0)});
    }
    return {messages::GameOpcode::RespawnWindow, std::move(window)};
}
messages::GameMessage bind_message(std::uint16_t zone = 2, std::uint16_t instance = 0,
                                   std::uint32_t mana = 0, std::uint32_t endurance = 0) {
    BindZoneTransition bind;
    bind.destination.zone = static_cast<std::uint32_t>(zone) | (static_cast<std::uint32_t>(instance) << 16);
    bind.destination.x = 40;
    bind.destination.y = 30;
    bind.destination.z = 12;
    bind.destination.heading = 256;
    bind.destination.zone_name = "Respawn destination";
    bind.destination.keep_items = 1;
    bind.destination.mana = static_cast<std::int32_t>(mana);
    bind.destination.endurance = static_cast<std::int32_t>(endurance);
    // The gameplay zone-to-bind handler requires the decoded wire length > 20.
    bind.original_length = 53;
    return {messages::GameOpcode::ZonePlayerToBind, std::move(bind)};
}
messages::GameMessage rez_offer_message(std::uint32_t serial = 100, std::uint16_t zone = 2,
                                        std::uint16_t instance = 0) {
    return {messages::GameOpcode::RezzRequest,
            ZoneOffer{ZoneOfferKind::Resurrection, serial, zone, instance, "Healer"}};
}

struct HoverRig {
    ManualClock clock;
    Rig rig;
    explicit HoverRig(plugin::Hooks* hooks = nullptr) : rig(MockConfig::standard(), options(clock, hooks)) {}

    static ConnectionOptions options(const ManualClock& clock, plugin::Hooks* hooks) {
        auto o = quick_options();
        o.world_protocol.session_config.clock = clock.source(); o.zone_protocol.session_config.clock = clock.source();
        o.hooks = hooks;
        return o;
    }
    bool start(std::uint32_t delay = 0, std::uint32_t remaining = 300000) {
        if (!rig.to_zone()) return false;
        clock.hold();
        die(rig.conn.world().player_id, delay, remaining);
        return rig.until([&] { return rig.conn.respawn_window().has_value(); });
    }
    void die(std::uint32_t id, std::uint32_t delay = 0, std::uint32_t remaining = 300000) {
        Death d;
        d.spawn_id = d.corpse_id = id;
        d.killer_id = 101;
        d.damage = 5000;
        rig.server.zone.send_reliable(encode(d));
        rig.server.zone.send_reliable(hover_window(delay, remaining));
    }
    void publish(std::uint32_t id, std::uint32_t hp = 1000) {
        rig.server.zone.send_reliable(encode(make_spawn("Gennro", id, 0, 1, 40, 30, 12, 256)));
        BuffIcons buffs;
        buffs.entity_id = id;
        buffs.all_buffs = 1;
        buffs.entries.push_back({0, 100, 20, 0, "Healer"});
        rig.server.zone.send_reliable(encode(buffs, false));
        rig.server.hp_update(static_cast<std::int16_t>(id), hp, 5000);
    }
};

void hover_barrier(Rig& rig) {
    const auto before = rig.server.zone_log.size();
    rig.conn.send_zone_message(messages::outgoing(messages::GameOpcode::RandomReq, RandomRollIntent{1, 1}));
    REQUIRE(rig.until([&] {
        return rig.server.zone_log.size() > before && rig.server.zone_log.back().opcode == rof2::to_wire(rof2::Op::RandomReq);
    }));
}

} // namespace

TEST_CASE("Hover respawn: a yes to an offer naming another zone or instance is answered at once") {
    // ConfirmationDialog_HandleResponse @0x52d540 case 0x67: the answer is held back for the respawn window only
    // when the offer's zone dword (zone and instance) is the current zone.
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    REQUIRE(c.zone_id() == 2);
    REQUIRE(c.zone_instance() == 0);
    std::uint16_t zone = 3, instance = 0;
    SUBCASE("another zone") {}
    SUBCASE("another instance of this zone") { zone = 2; instance = 1; }
    h.rig.server.zone.send_reliable(rez_offer(100, zone, instance));
    REQUIRE(h.rig.until([&] { return c.confirmation().has_value(); }));
    c.answer_confirmation(true);
    CHECK_FALSE(c.accepted_hover_rez_option());
    CHECK_FALSE(c.respawn_option_available(1)); // the reserved line stays disabled
    hover_barrier(h.rig);
    REQUIRE(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 1);
    const auto answer = std::find_if(h.rig.server.zone_log.begin(), h.rig.server.zone_log.end(),
                                     [](const AppPacket& p) { return p.opcode == rof2::to_wire(rof2::Op::RezzAnswer); });
    REQUIRE(answer != h.rig.server.zone_log.end());
    REQUIRE(answer->data.size() > 224);
    CHECK(static_cast<std::uint8_t>(answer->data[224]) == 1); // the yes, echoed in the offer
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 0);
}

TEST_CASE("Hover respawn: a yes to an offer for this zone is held back for the respawn window") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(rez_offer_message());
    c.answer_confirmation(true);
    CHECK(c.accepted_hover_rez_option() == 1);
    hover_barrier(h.rig);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 0);
}

TEST_CASE("Hover respawn: a dropped selection retains consent and can be retried") {
    plugin::Hooks hooks;
    HoverRig h(&hooks);
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(rez_offer_message());
    c.answer_confirmation(true);
    h.clock.advance(10ms);
    const auto owner = hooks.new_owner("hover selection drop test");
    bool drop = true;
    hooks.on_packet(owner, plugin::Stream::Zone, plugin::Direction::Out, rof2::to_wire(rof2::Op::RespawnWindow),
        [&](AppPacket&) { return drop ? plugin::Flow::Drop : plugin::Flow::Continue; });
    c.choose_respawn(1);
    CHECK_FALSE(c.respawn_chosen());
    CHECK(c.accepted_hover_rez_option() == 1);
    CHECK(c.respawn_option_available(1));
    hover_barrier(h.rig);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 0);
    drop = false;
    c.choose_respawn(1);
    CHECK(c.respawn_chosen());
    hover_barrier(h.rig);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 1);
    hooks.remove(owner);
}

TEST_CASE("Hover respawn: outgoing hooks cannot commit a retired choice or erase replacement consent") {
    plugin::Hooks hooks;
    HoverRig h(&hooks);
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(rez_offer_message());
    c.answer_confirmation(true);
    h.clock.advance(10ms);
    int mode = 0;
    SUBCASE("the selected destination changes") {}
    SUBCASE("a replacement offer needs consent") { mode = 1; }
    SUBCASE("the connection is retired") { mode = 2; }
    SUBCASE("a new death receives its own consent during the outgoing hook") { mode = 3; }
    const auto owner = hooks.new_owner("hover selection retirement test");
    hooks.on_packet(owner, plugin::Stream::Zone, plugin::Direction::Out, rof2::to_wire(rof2::Op::RespawnWindow),
        [&](AppPacket&) {
            if (mode == 0) c.receive_zone_message_for_test(hover_message(0, 300000, "Changed destination"));
            if (mode == 1) c.receive_zone_message_for_test(rez_offer_message(101));
            if (mode == 2) c.disconnect();
            if (mode == 3) {
                c.receive_zone_message_for_test(bind_message());
                c.receive_zone_message_for_test({messages::GameOpcode::NewSpawn, make_spawn("Gennro", 60, 0, 1, 40, 30, 12)});
                Death death;
                death.spawn_id = death.corpse_id = 60;
                death.damage = 5000;
                c.receive_zone_message_for_test({messages::GameOpcode::Death, death});
                c.receive_zone_message_for_test(hover_message());
                c.receive_zone_message_for_test(rez_offer_message(101));
                c.answer_confirmation(true);
                h.clock.advance(10ms);
                c.choose_respawn(0); // a nested send cannot steal this in-flight reservation
            }
            return plugin::Flow::Continue;
        });
    c.choose_respawn(1);
    hooks.remove(owner);
    CHECK_FALSE(c.respawn_chosen());
    if (mode == 2) {
        CHECK(c.state() == ConnectionState::Disconnected);
        CHECK_FALSE(c.respawn_window());
        h.rig.run_for(10ms);
    } else {
        hover_barrier(h.rig);
        REQUIRE(c.respawn_window());
    }
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 0);
    if (mode == 0 || mode == 1) CHECK_FALSE(c.respawn_option_available(1));
    if (mode == 1) {
        REQUIRE(c.confirmation());
        REQUIRE(c.confirmation()->pending_message);
        const auto* pending = messages::message_get<ZoneOffer>(*c.confirmation()->pending_message);
        REQUIRE(pending);
        CHECK(pending->token == 101);
    }
    if (mode == 3) {
        CHECK(c.accepted_hover_rez_option() == 1);
        c.choose_respawn(1); // releasing the old reservation leaves the new death's consent intact
        CHECK(c.respawn_chosen());
        hover_barrier(h.rig);
        CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 1);
    }
}

TEST_CASE("Hover respawn: a nested outgoing selection cannot send a second option") {
    plugin::Hooks hooks;
    HoverRig h(&hooks);
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(rez_offer_message());
    c.answer_confirmation(true);
    h.clock.advance(10ms);
    const auto owner = hooks.new_owner("hover selection reentry test");
    int calls = 0;
    hooks.on_packet(owner, plugin::Stream::Zone, plugin::Direction::Out, rof2::to_wire(rof2::Op::RespawnWindow),
        [&](AppPacket&) {
            if (++calls == 1) c.choose_respawn(0);
            return plugin::Flow::Continue;
        });
    c.choose_respawn(1);
    CHECK(c.respawn_chosen());
    CHECK(calls == 1);
    hover_barrier(h.rig);
    const auto& log = h.rig.server.zone_log;
    CHECK(MockEqServer::count(log, rof2::Op::RespawnWindow) == 1);
    const auto selection = std::find_if(log.begin(), log.end(), [](const AppPacket& p) { return is_op(p, rof2::Op::RespawnWindow); });
    REQUIRE(selection != log.end());
    CHECK(selection->data == encode_respawn_selection(1).data);
    hooks.remove(owner);
}

TEST_CASE("Hover respawn: choices validate availability and remain one-shot across malformed or refreshed windows") {
    HoverRig h;
    REQUIRE(h.start(100));
    auto& c = h.rig.conn;
    REQUIRE(c.world().player());
    CHECK(c.world().player()->is_corpse);
    CHECK(c.player_status().dead);
    CHECK_FALSE(c.respawn_ready());
    c.choose_respawn(0);
    CHECK_FALSE(c.respawn_chosen());
    h.clock.advance(101ms);
    CHECK(c.respawn_ready());
    c.choose_respawn(1); // no pending resurrection: server would reject this without a reply
    c.choose_respawn(999);
    CHECK_FALSE(c.respawn_chosen());
    CHECK_FALSE(decode_respawn_window(AppPacket(rof2::to_wire(rof2::Op::RespawnWindow), std::vector<std::byte>(15))));
    REQUIRE(c.respawn_window());
    CHECK(c.respawn_window()->options.size() == 2);
    c.choose_respawn(0);
    CHECK(c.respawn_chosen());
    c.choose_respawn(0);
    c.receive_zone_message_for_test(hover_message());
    CHECK(c.respawn_chosen());
    CHECK_FALSE(decode_respawn_window(AppPacket(rof2::to_wire(rof2::Op::RespawnWindow), std::vector<std::byte>(15))));
    CHECK(c.respawn_chosen());
    REQUIRE(h.rig.until([&] { return MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 1; }));
    const auto& log = h.rig.server.zone_log;
    const auto chosen = std::find_if(log.begin(), log.end(), [](const AppPacket& p) { return is_op(p, rof2::Op::RespawnWindow); });
    REQUIRE(chosen != log.end());
    CHECK(chosen->data == std::vector<std::byte>(4));
}

TEST_CASE("Hover respawn: same-zone publication replaces player identity and placement while retaining the corpse") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    const auto corpse = c.world().player_id;
    // Resources may arrive while the server's live entity ID is temporarily zero, before its bind acknowledgement.
    h.rig.server.zone.send_reliable(points(rof2::Op::ManaUpdate, 0, 23, 3000));
    h.rig.server.zone.send_reliable(points(rof2::Op::EnduranceUpdate, 0, 45, 4000));
    h.rig.server.zone.send_reliable(bind_packet());
    REQUIRE(h.rig.until([&] { return !c.respawn_window(); }));
    CHECK(c.player_status().dead); // never move using the corpse's ID while publication is outstanding
    CHECK(c.player_status().mana == 23);
    CHECK(c.player_status().endurance == 45);
    CHECK_FALSE(c.take_respawn_placement());
    const auto corpse_position = c.world().spawns.at(corpse).position;
    c.set_player_motion(glm::vec3(100, 200, 300), 128, glm::vec3(1), true);
    c.set_player_drawn(glm::vec3(100, 200, 300), 128);
    CHECK(c.world().spawns.at(corpse).position == corpse_position);
    CHECK(c.world().spawns.at(corpse).display_position != glm::vec3(100, 200, 300));
    c.send_position_now();
    // A stale living record for the corpse's old ID cannot revive it or end the publication wait.
    c.receive_zone_message_for_test({messages::GameOpcode::NewSpawn, make_spawn("Gennro", corpse, 0, 1, 5, 6, 7)});
    CHECK(c.world().spawns.at(corpse).is_corpse);
    CHECK_FALSE(c.take_respawn_placement());
    // Corpses, other PCs and a same-name NPC cannot become the player.
    c.receive_zone_message_for_test({messages::GameOpcode::NewSpawn, make_spawn("Gennro", 50, 3, 1, 1, 2, 3)});
    c.receive_zone_message_for_test({messages::GameOpcode::NewSpawn, make_spawn("Gennro", 51, 1, 1, 1, 2, 3)});
    c.receive_zone_message_for_test({messages::GameOpcode::NewSpawn, make_spawn("Other", 52, 0, 1, 1, 2, 3)});
    c.receive_zone_message_for_test(hover_message()); // cannot reopen the acknowledged transaction
    CHECK_FALSE(c.respawn_window());
    CHECK(c.world().player_id == corpse);
    CHECK(c.player_status().dead);
    h.publish(60);
    REQUIRE(h.rig.until([&] { return c.world().player_id == 60 && c.player_status().hp == 1000; }));
    CHECK_FALSE(c.player_status().dead);
    CHECK(c.player_status().stand_state == kStandStanding);
    CHECK(c.world().spawns.at(corpse).is_corpse);
    CHECK_FALSE(c.world().player()->is_corpse);
    CHECK(c.player_status().mana == 23);
    CHECK(c.player_status().endurance == 45);
    CHECK(c.spell_state().buffs[0].spell == 100);
    REQUIRE(c.profile());
    CHECK(c.profile()->x == 40);
    auto placement = c.take_respawn_placement();
    REQUIRE(placement);
    CHECK(placement->position == glm::vec3(30, 40, 12));
    CHECK(placement->heading == 256);
    CHECK_FALSE(c.take_respawn_placement());
    c.send_position_now();
    REQUIRE(h.rig.until([&] { return !h.rig.server.client_updates.empty(); }));
    CHECK(h.rig.server.client_updates.back().spawn_id == 60);
    CHECK(h.rig.server.client_updates.back().motion.x == 40);
    CHECK(h.rig.server.client_updates.back().motion.y == 30);
    CHECK(h.rig.server.client_updates.back().motion.delta_x == 0);
    h.rig.server.remove_spawn(corpse);
    REQUIRE(h.rig.until([&] { return c.world().spawns.count(corpse) == 0; }));
    CHECK(c.world().player_id == 60);
    // Publication recovery is armed only by hover completion, never by an ordinary spawn with our name.
    c.receive_zone_message_for_test({messages::GameOpcode::NewSpawn, make_spawn("Gennro", 61, 0, 1, 1, 2, 3)});
    CHECK(c.world().player_id == 60);
    h.die(60);
    REQUIRE(h.rig.until([&] { return c.respawn_window().has_value(); }));
    CHECK(c.player_status().dead);
    CHECK(c.world().spawns.at(60).is_corpse);
    h.rig.server.zone.send_reliable(bind_packet());
    h.publish(62);
    REQUIRE(h.rig.until([&] { return c.world().player_id == 62; }));
    CHECK(c.world().spawns.at(60).is_corpse);
}

TEST_CASE("Hover respawn: a same-zone respawn lets go of the boat being driven") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    auto& boat = c.world_for_test().spawns[9001];
    boat.id = 9001;
    boat.race = 141;
    boat.gender = 2;
    boat.is_npc = true;
    c.receive_zone_message_for_test({messages::GameOpcode::ControlBoat, BoatControl{9001, true}});
    REQUIRE(c.world().driven_id == 9001);
    h.rig.server.zone.send_reliable(bind_packet());
    REQUIRE(h.rig.until([&] { return !c.respawn_window(); }));
    h.publish(60);
    REQUIRE(h.rig.until([&] { return c.world().player_id == 60; }));
    CHECK(c.controlled_boat() == 0);
    CHECK(c.world().driven_id == 0);
}

TEST_CASE("Hover respawn: Yes keeps both destinations and the timer until a destination is selected") {
    HoverRig h;
    REQUIRE(h.start(1000));
    auto& c = h.rig.conn;
    // Ordinary resurrection does not send a resource update if zero mana and endurance are already reported.
    c.receive_zone_message_for_test({messages::GameOpcode::ManaChange, ManaChange{0, 777, 0, false, 0}});
    bool resources_changed = false;
    std::uint32_t destination = 1;
    std::uint32_t spell = 100;
    std::uint32_t mana = 0, endurance = 777;
    SUBCASE("choose resurrection with zero mana and unchanged endurance") {}
    SUBCASE("Divine Rez resources precede the bind packet") {
        spell = 2738;
        mana = 3000;
        endurance = 4000;
        resources_changed = true;
    }
    SUBCASE("accept the offer but choose bind") {
        destination = 0;
        mana = 3000;
        endurance = 4000;
        resources_changed = true;
    }
    h.rig.server.zone.send_reliable(rez_offer(spell));
    REQUIRE(h.rig.until([&] { return c.confirmation().has_value(); }));
    REQUIRE(c.confirmation()->id == kConfirmRezz);
    REQUIRE(c.respawn_window()); // the hover window remains alongside the offer prompt
    CHECK(c.respawn_option_available(0));
    CHECK_FALSE(c.respawn_option_available(1));
    const auto age = c.respawn_age_ms();
    const auto remaining = c.respawn_ms_left();
    const auto session = c.respawn_session();
    c.answer_confirmation(true);
    CHECK_FALSE(c.confirmation());
    REQUIRE(c.respawn_window());
    CHECK(c.respawn_window()->options.size() == 2);
    CHECK_FALSE(c.respawn_chosen());
    CHECK(c.respawn_option_available(0));
    CHECK(c.respawn_option_available(1));
    CHECK(c.respawn_age_ms() == age);
    CHECK(c.respawn_ms_left() == remaining);
    CHECK(c.respawn_session() == session);
    c.choose_respawn(destination); // accepting an offer does not bypass the server's Select delay
    CHECK_FALSE(c.respawn_chosen());
    h.clock.advance(100ms);
    h.rig.run_for(10ms);
    CHECK(c.respawn_ms_left() == remaining - 100);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 0);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 0);
    h.clock.advance(1000ms);
    c.choose_respawn(destination);
    CHECK(c.respawn_chosen());
    c.choose_respawn(destination);
    REQUIRE(h.rig.until([&] { return MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 1; }));
    const auto& log = h.rig.server.zone_log;
    const auto selection = std::find_if(log.begin(), log.end(), [](const AppPacket& p) { return is_op(p, rof2::Op::RespawnWindow); });
    REQUIRE(selection != log.end());
    CHECK(selection->data == encode_respawn_selection(destination).data);
    CHECK(MockEqServer::count(log, rof2::Op::RezzAnswer) == 0);
    c.answer_confirmation(true);
    CHECK_FALSE(c.confirmation());
    // No window refresh accompanies a real offer. An already committed hover must not ask another question.
    c.receive_zone_message_for_test(rez_offer_message(101));
    CHECK_FALSE(c.confirmation());
    // Unchanged zero mana has no new ManaChange packet. Divine Rez and bind report restored resources first.
    if (resources_changed)
        c.receive_zone_message_for_test({messages::GameOpcode::ManaChange, ManaChange{mana, endurance, 0, false, 0}});
    c.receive_zone_message_for_test(bind_message());
    CHECK(c.player_status().mana == mana);
    CHECK(c.player_status().endurance == endurance);
    h.publish(70);
    REQUIRE(h.rig.until([&] { return c.world().player_id == 70; }));
    CHECK_FALSE(c.player_status().dead);
    CHECK(c.player_status().mana == mana);
    // Stock EQEmu can report these after its bind packet, including a full-resource Divine Rez.
    c.receive_zone_message_for_test({messages::GameOpcode::ManaChange, ManaChange{3000, 4000, 0, false, 0}});
    CHECK(c.player_status().mana == 3000);
    CHECK(c.player_status().endurance == 4000);
}

TEST_CASE("Hover respawn: No declines the offer once and leaves bind available") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    h.rig.server.zone.send_reliable(rez_offer());
    REQUIRE(h.rig.until([&] { return c.confirmation().has_value(); }));
    c.answer_confirmation(false);
    CHECK_FALSE(c.confirmation());
    CHECK_FALSE(c.respawn_chosen());
    REQUIRE(c.respawn_window());
    CHECK(c.respawn_option_available(0));
    CHECK_FALSE(c.respawn_option_available(1));
    h.clock.advance(10ms);
    REQUIRE(h.rig.until([&] { return MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 1; }));
    const auto& log = h.rig.server.zone_log;
    const auto answer = std::find_if(log.begin(), log.end(), [](const AppPacket& p) { return is_op(p, rof2::Op::RezzAnswer); });
    REQUIRE(answer != log.end());
    CHECK(answer->data == answer_rezz(rez_offer(), false).data);
    c.answer_confirmation(false);
    c.choose_respawn(1);
    CHECK_FALSE(c.respawn_chosen());
    c.choose_respawn(0);
    CHECK(c.respawn_chosen());
    REQUIRE(h.rig.until([&] { return MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 1; }));
    CHECK(MockEqServer::count(log, rof2::Op::RezzAnswer) == 1);
}

TEST_CASE("Hover respawn: accepted offers follow the reserved final row without matching its label") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(hover_message(0, 300000, "Return to your remains"));
    c.receive_zone_message_for_test(rez_offer_message());
    c.answer_confirmation(true);
    CHECK(c.respawn_option_available(1));
    c.receive_zone_message_for_test(hover_message(0, 300000, "Return to your remains"));
    CHECK(c.respawn_option_available(1)); // an identical window refresh retains local acceptance
    c.receive_zone_message_for_test(hover_message(0, 300000, "A different reserved destination"));
    CHECK_FALSE(c.respawn_option_available(1)); // a changed row cannot reuse the earlier acceptance
    c.receive_zone_message_for_test(rez_offer_message(101));
    c.answer_confirmation(true);
    CHECK(c.respawn_option_available(1));
    c.receive_zone_message_for_test(rez_offer_message(102));
    REQUIRE(c.confirmation());
    CHECK_FALSE(c.respawn_option_available(1)); // replacement offers need their own approval
    CHECK_FALSE(c.respawn_chosen());
    c.answer_confirmation(true);
    CHECK(c.respawn_option_available(1));
    h.clock.advance(10ms);
    h.rig.run_for(10ms);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 0);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 0);
}

TEST_CASE("Hover respawn: an unknown option map cannot immediately resurrect on Yes") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    std::vector<std::uint32_t> ids{0};
    SUBCASE("a single row has no reserved resurrection destination") {}
    SUBCASE("nonsequential ids do not establish the EQEmu reserved row") { ids = {0, 7}; }
    c.receive_zone_message_for_test(hover_message(0, 300000, "Resurrect", ids));
    c.receive_zone_message_for_test(rez_offer_message());
    REQUIRE(c.confirmation());
    c.answer_confirmation(true);
    CHECK_FALSE(c.respawn_chosen());
    CHECK_FALSE(c.respawn_option_available(ids.back()));
    h.clock.advance(10ms);
    c.choose_respawn(ids.back());
    h.rig.run_for(10ms);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 0);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 0);
}

TEST_CASE("Hover respawn: an advertised available resurrection still requires the pending offer's consent") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    auto available = hover_message();
    auto* available_window = messages::message_get<RespawnWindow>(available);
    REQUIRE(available_window);
    available_window->options.back().validity = 0;
    c.receive_zone_message_for_test(available);
    c.receive_zone_message_for_test(rez_offer_message());
    REQUIRE(c.confirmation());
    CHECK_FALSE(c.respawn_option_available(1));
    c.answer_confirmation(false);
    CHECK_FALSE(c.respawn_option_available(1));
    c.receive_zone_message_for_test(available);
    CHECK_FALSE(c.respawn_option_available(1)); // a repeated raw flag cannot undo the user's No
    CHECK(c.respawn_option_available(0));
}

TEST_CASE("Resurrection outside hover: Yes still echoes the accepted offer immediately") {
    HoverRig h;
    REQUIRE(h.rig.to_zone());
    auto& c = h.rig.conn;
    CHECK_FALSE(c.respawn_window());
    h.rig.server.zone.send_reliable(rez_offer());
    REQUIRE(h.rig.until([&] { return c.confirmation().has_value(); }));
    c.answer_confirmation(true);
    REQUIRE(h.rig.until([&] { return MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 1; }));
    const auto& log = h.rig.server.zone_log;
    const auto answer = std::find_if(log.begin(), log.end(), [](const AppPacket& p) { return is_op(p, rof2::Op::RezzAnswer); });
    REQUIRE(answer != log.end());
    CHECK(answer->data == answer_rezz(rez_offer(), true).data);
    CHECK(MockEqServer::count(log, rof2::Op::RespawnWindow) == 0);
}

TEST_CASE("Hover respawn: a new death discards an unanswered offer for an earlier corpse") {
    HoverRig h;
    REQUIRE(h.rig.to_zone());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(rez_offer_message());
    REQUIRE(c.confirmation());
    CHECK_FALSE(c.respawn_window());
    h.clock.hold();
    h.die(c.world().player_id);
    REQUIRE(h.rig.until([&] { return c.respawn_window().has_value(); }));
    CHECK(c.player_status().dead);
    CHECK_FALSE(c.confirmation());
    CHECK_FALSE(c.respawn_option_available(1));
    c.answer_confirmation(true); // the old corpse's prompt cannot grant consent for this death
    CHECK_FALSE(c.respawn_option_available(1));
    CHECK_FALSE(c.respawn_chosen());
    h.clock.advance(10ms);
    h.rig.run_for(10ms);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 0);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 0);
}

TEST_CASE("Hover respawn: timeout is server authoritative and discards superseded resurrection questions") {
    HoverRig h;
    REQUIRE(h.start(0, 1000));
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(rez_offer_message());
    c.answer_confirmation(true);
    CHECK(c.respawn_option_available(1));
    c.receive_zone_message_for_test(rez_offer_message(101)); // replaces the server's previous pending offer
    REQUIRE(c.confirmation());
    REQUIRE(c.confirmation()->pending_message);
    const auto* pending = messages::message_get<ZoneOffer>(*c.confirmation()->pending_message);
    REQUIRE(pending);
    CHECK(pending->token == 101);
    CHECK_FALSE(c.respawn_option_available(1));
    c.answer_confirmation(true);
    CHECK(c.respawn_option_available(1));
    h.clock.advance(1001ms);
    CHECK(c.respawn_ms_left() == 0);
    CHECK(c.player_status().dead);
    CHECK_FALSE(c.respawn_chosen()); // no local guess or synthesized option when the server timer expires
    c.receive_zone_message_for_test(bind_message());
    CHECK_FALSE(c.confirmation());
    c.answer_confirmation(true);
    h.publish(80);
    REQUIRE(h.rig.until([&] { return c.world().player_id == 80; }));
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 0);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 0);
    h.die(80);
    REQUIRE(h.rig.until([&] { return c.respawn_window().has_value(); }));
    CHECK_FALSE(c.respawn_option_available(1)); // approval of the expired offer cannot survive another death
}

TEST_CASE("Hover respawn: explicit nonzero bind resources override earlier resource reports") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test({messages::GameOpcode::ManaChange, ManaChange{12, 34, 0, false, 0}});
    c.receive_zone_message_for_test(bind_message(2, 0, 456, 789));
    CHECK(c.player_status().dead);
    CHECK(c.player_status().mana == 456);
    CHECK(c.player_status().endurance == 789);
    h.publish(90);
    REQUIRE(h.rig.until([&] { return c.world().player_id == 90; }));
    CHECK(c.player_status().mana == 456);
    CHECK(c.player_status().endurance == 789);
}

TEST_CASE("Hover respawn: cross-zone bind uses a full zone-in") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(rez_offer_message());
    c.answer_confirmation(true);
    CHECK(c.respawn_option_available(1));
    h.rig.server.zone.send_reliable(bind_packet(1));
    REQUIRE(h.rig.until([&] { return h.rig.server.zone_entries == 2 && c.state() == ConnectionState::InZone; }));
    CHECK(c.zone_id() == 1);
    CHECK_FALSE(c.player_status().dead);
    CHECK_FALSE(c.respawn_window());
    CHECK_FALSE(c.confirmation());
    CHECK_FALSE(c.take_respawn_placement());
    CHECK(c.world().spawns.count(101) == 0);
    // A zero-ID update is special only while hovering; it cannot overwrite a living player's vitals.
    const auto mana = c.player_status().mana;
    c.receive_zone_message_for_test({messages::GameOpcode::ManaUpdate, PointsUpdate{99, 100, 0}});
    CHECK(c.player_status().mana == mana);
    h.die(c.world().player_id);
    REQUIRE(h.rig.until([&] { return c.respawn_window().has_value(); }));
    CHECK_FALSE(c.respawn_option_available(1)); // approval from the previous zone was discarded
    c.disconnect();
    CHECK_FALSE(c.respawn_window());
    CHECK_FALSE(c.take_respawn_placement());
}

TEST_CASE("Hover respawn: disconnect discards an accepted but unselected resurrection") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(rez_offer_message());
    c.answer_confirmation(true);
    CHECK(c.respawn_option_available(1));
    c.disconnect();
    CHECK_FALSE(c.confirmation());
    CHECK_FALSE(c.respawn_window());
    // A later fixture window cannot borrow consent from the disconnected session.
    c.receive_zone_message_for_test(hover_message());
    CHECK_FALSE(c.respawn_option_available(1));
    CHECK_FALSE(c.respawn_chosen());
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RezzAnswer) == 0);
    CHECK(MockEqServer::count(h.rig.server.zone_log, rof2::Op::RespawnWindow) == 0);
}

TEST_CASE("Hover respawn: disconnect clears the publication wait and unconsumed placement") {
    HoverRig h;
    REQUIRE(h.start());
    auto& c = h.rig.conn;
    c.receive_zone_message_for_test(rez_offer_message());
    c.receive_zone_message_for_test(bind_message());
    CHECK_FALSE(c.respawn_window());
    CHECK_FALSE(c.confirmation());
    CHECK(c.player_status().dead);
    const auto corpse = c.world().player_id;
    bool published = false;
    SUBCASE("disconnect while waiting for the new living spawn") {}
    SUBCASE("disconnect with placement still waiting for the app") {
        h.publish(95);
        REQUIRE(h.rig.until([&] { return c.world().player_id == 95; }));
        CHECK_FALSE(c.player_status().dead);
        published = true;
    }
    const auto player = c.world().player_id;
    c.disconnect();
    CHECK(c.state() == ConnectionState::Disconnected);
    CHECK_FALSE(c.respawn_window());
    CHECK_FALSE(c.confirmation());
    CHECK_FALSE(c.take_respawn_placement());
    // There is no pending transaction to accept a late publication after disconnect.
    c.receive_zone_message_for_test({messages::GameOpcode::NewSpawn, make_spawn("Gennro", 96, 0, 1, 40, 30, 12)});
    CHECK(c.world().player_id == player);
    CHECK_FALSE(c.take_respawn_placement());
    CHECK(c.world().spawns.at(corpse).is_corpse);
    CHECK(c.player_status().dead == !published);
}
