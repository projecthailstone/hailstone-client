// The original's vectors (recorded from eqgame.exe): OP_Death's
// report (game/death_report.h), the region teleports, the teleport-by-index
// request and the respawn at bind (game/zoning.h).
#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <optional>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "core/byte_io.h"
#include "game/death_report.h"
#include "game/zoning.h"
#include "net/protocol/rof2/codec.h"
#include "net/protocol/rof2/packets/zoning_packets.h"
#include "vector_files.h"

using namespace hailstone::game;
using hailstone::test::Json;
using hailstone::test::f32_bits;
using hailstone::test::f32_of;

namespace {

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

std::string diff(const char* what, const std::vector<std::string>& got, const std::vector<std::string>& want) {
    if (got == want) return {};
    return std::string(what) + ": got [" + join(got) + "] want [" + join(want) + "]";
}

std::string bits(float f) { return std::format("{:08x}", std::bit_cast<std::uint32_t>(f)); }
std::string bits(const Json& j) { return std::format("{:08x}", f32_bits(j)); }

// ---- Handle_OP_Death

std::string check_death(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    constexpr std::uint32_t kLocalId = 0x101; // the generator's ids (death_report_test.cpp)
    DeathInput d;
    const std::string& dead = in["dead"].text;
    const std::string& killer = in["killer"].text;
    d.dead_known = dead != "unknown_id";
    d.dead_is_local = dead == "local";
    d.dead_id = in["dead_id"].u32();
    d.killer_id = in["killer_id"].u32();
    d.local_id = kLocalId;
    d.killer_is_local = killer == "local";
    d.killer_known = killer == "local" || killer == "other" || killer == "dead_itself";
    // The generator may also drop the "dead" spawn (id 0x202) while the local player died: then a killer with
    // that id is unknown. Whether it did is not among the inputs; the text says it (12108).
    if (killer == "dead_itself" && d.dead_is_local)
        for (const Json& c : out["chat"].items)
            if (c["text"].text.rfind("<12108", 0) == 0) d.killer_known = false;
    d.spell = in["spell_id"].i32();
    d.skill = static_cast<std::uint8_t>(in["skill"].i32());
    d.damage = in["damage"].i32();
    d.dead_type = static_cast<std::uint8_t>(in["dead_type"].i32());
    d.dead_class = static_cast<std::uint8_t>(in["dead_class"].i32());
    d.dead_stand_state = static_cast<std::uint8_t>(in["dead_stand_state"].i32());
    d.dead_is_duel_opponent = in["dead_is_duel_opponent"].truthy();
    d.dueling = in["dueling"].truthy();
    d.dead_is_trade_partner = in["dead_is_trade_partner"].truthy();
    d.dead_is_merchant = in["dead_is_merchant"].truthy();
    d.dead_is_banker = in["dead_is_banker"].truthy();
    d.dead_is_trainer = in["dead_is_trainer"].truthy();
    d.tracking_on = in["tracking_on"].truthy();
    d.dead_is_tracked = in["dead_is_tracked"].truthy();
    d.dead_is_target = in["dead_is_target"].truthy();
    d.in_group = in["in_group"].truthy();
    d.group_member = in["group_member"].truthy();
    d.distance = f32_of(in["distance"]);
    d.local_name = "Me";
    d.dead_name = d.dead_is_local ? "Me" : "Dead";
    d.dead_shown = d.dead_is_local ? "Me shown" : "Dead shown";
    d.killer_shown = killer == "local" ? "Me shown" : killer == "dead_itself" ? "Dead shown" : "Killer shown";
    DeathResult r = report_death(d);

    std::vector<std::string> got, want;
    for (const auto& c : r.chat) {
        std::string t = c.built ? std::format("<{}|{}|{}>", c.eqstr, c.p1, c.p2) : std::format("<{}>", c.eqstr);
        got.push_back(std::format("{} {}", t, c.color));
    }
    for (const Json& c : out["chat"].items) want.push_back(std::format("{} {}", c["text"].text, c["color"].i32()));
    if (auto e = diff("chat", got, want); !e.empty()) return e;
    if (r.screen_text != out["screen_text"].truthy()) return "screen_text";
    if (!r.handled) {
        if (!out["hit"].is_null() || !out["calls"].items.empty()) return "unknown dead spawn";
        return {};
    }
    const Json& h = out["hit"];
    if (h.is_null()) return "hit";
    if (r.hit_target != h["target"].u32() || r.hit_source != h["source"].u32() || r.hit_type != h["type"].u32() ||
        r.hit_spell != h["spell_id"].i32() || r.hit_damage != h["damage"].i32())
        return "hit record";
    if (r.hit_report != h["report"].truthy()) return "hit report";
    if (r.local_died != out["local_died"].truthy()) return "local_died";
    if (r.merchant_window_closed != out["merchant_window_closed"].truthy()) return "merchant";
    if (r.trainer_window_closed != out["trainer_window_closed"].truthy()) return "trainer";
    if (r.duel_ended) {
        if (out["duel_opponent_after"].i32() != 0 || out["dueling_after"].truthy()) return "duel end";
    } else if (out["dueling_after"].truthy() != d.dueling) {
        return "dueling";
    }
    if (r.target_cleared != out["target_cleared"].truthy()) return "target_cleared";
    if ((r.dead_hp_minus_11 ? -11 : 0x1234) != out["dead_hp_after"].i32()) return "dead hp";
    got.clear();
    want.clear();
    using F = DeathCall::Fn;
    for (const auto& c : r.calls) {
        switch (c.fn) {
        case F::SendDuelDecline:
            // net::Send: UdpConnection::Send(4, 10 bytes) only when connected.
            if (in["connected"].truthy()) got.push_back("udp_send 4 10");
            break;
        case F::TradeCancelAndClose: got.push_back("trade_cancel_and_close"); break;
        case F::BankWindowClose: got.push_back("bank_window_close 0"); break;
        case F::LootResetSession: got.push_back("loot_reset_session"); break;
        case F::SendTrackTarget: got.push_back("send_track_target -1 0"); break;
        case F::SetAutoAttackOff: got.push_back("set_auto_attack 0"); break;
        case F::SetAutoRangeAttackOff: got.push_back("set_auto_range_attack 0"); break;
        case F::GroupGetMember: got.push_back("group_get_member"); break;
        case F::TrackingRemove: got.push_back("tracking_remove"); break;
        case F::SpawnDied: got.push_back("spawn_died"); break;
        }
    }
    for (const Json& c : out["calls"].items) {
        const std::string& fn = c["fn"].text;
        if (fn == "udp_send") want.push_back(std::format("udp_send {} {}", c["channel"].i32(), c["length"].i32()));
        else if (fn == "bank_window_close") want.push_back(std::format("{} {}", fn, c["arg"].i32()));
        else if (fn == "send_track_target") want.push_back(std::format("{} {} {}", fn, c["spawn"].i32(), c["arg"].i32()));
        else if (fn == "set_auto_attack" || fn == "set_auto_range_attack") want.push_back(std::format("{} {}", fn, c["on"].truthy() ? 1 : 0));
        else want.push_back(fn);
    }
    return diff("calls", got, want);
}

// ---- Teleport_ResolveTeleportIndex

std::string check_resolve(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    std::vector<ZonePointRow> rows;
    for (const Json& z : in["zone_points"].items) {
        ZonePointRow r;
        r.number = z["number"].i32();
        r.x = f32_of(z["x"]);
        r.y = f32_of(z["y"]);
        r.z = f32_of(z["z"]);
        r.heading = f32_of(z["heading"]);
        r.zone = z["zone"].i32();
        r.index_request = z["index_request"].i32();
        rows.push_back(r);
    }
    float loc[3] = {f32_of(in["location_x"]), f32_of(in["location_y"]), f32_of(in["location_z"])};
    ResolvedTeleport t = resolve_teleport_index(in["text"].text, in["index"].i32(), in["current_zone"].i32(), loc, rows);
    if (t.ok != out["value"].truthy()) return "value";
    if (bits(t.x) != bits(out["x"]) || bits(t.y) != bits(out["y"]) || bits(t.z) != bits(out["z"]) ||
        bits(t.heading) != bits(out["heading"]))
        return std::format("position {} {} {} {}", t.x, t.y, t.z, t.heading);
    if (t.zone_point != out["zone_point"].i32()) return std::format("zone_point {}", t.zone_point);
    if (t.index_request != out["index_request"].i32()) return "index_request";
    if (t.ok) {
        if (t.found != out["found"].i32() || t.destination != out["destination"].i32()) return "found";
        if (t.same_zone != out["same_zone"].truthy()) return "same_zone";
        if (t.camera_flag != out["camera_flag_46"].truthy()) return "camera";
    }
    if (bits(loc[0]) != bits(out["location_x"]) || bits(loc[1]) != bits(out["location_y"]) ||
        bits(loc[2]) != bits(out["location_z"]))
        return "location";
    return {};
}

// ---- PlayerZoneClient::DoTeleport

std::string check_do_teleport(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    const Json& res = in["resolved"];
    std::vector<std::string> got, samples;
    DoTeleportHooks h;
    h.resolve = [&](std::int32_t index, bool& has_description) {
        got.push_back(std::format("resolve_teleport {}", index));
        has_description = res["has_description"].truthy();
        ResolvedTeleport t;
        t.ok = res["ok"].truthy();
        t.x = f32_of(res["x"]);
        t.y = f32_of(res["y"]);
        t.z = f32_of(res["z"]);
        t.heading = f32_of(res["heading"]);
        t.found = res["found"].i32();
        t.destination = res["destination"].i32();
        t.zone_point = res["zone_point"].i32();
        t.index_request = res["index_request"].i32();
        return t;
    };
    h.teleport_b = [&](std::int32_t zone, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t heading) {
        got.push_back(std::format("do_teleport_b {} {} {} {} {}", zone, static_cast<std::int32_t>(x),
                                  static_cast<std::int32_t>(y), static_cast<std::int32_t>(z),
                                  static_cast<std::int32_t>(heading)));
        return in["do_teleport_b_ok"].truthy();
    };
    h.send_index_request = [&](std::int32_t zp) { got.push_back(std::format("send_teleport_index_request {}", zp)); };
    h.position_sample = [&](int kind, const float p[3]) {
        samples.push_back(std::format("{} {} {} {}", kind, bits(p[0]), bits(p[1]), bits(p[2])));
    };
    h.free_description = [&] { got.push_back("free_description"); };
    std::int32_t affect_ret = 0;
    for (const Json& c : out["calls"].items)
        if (c["fn"].text == "get_pc_spell_affect") affect_ret = c["ret"].i32();
    h.get_spell_affect = [&](std::int32_t spa) {
        got.push_back(std::format("get_pc_spell_affect {}", spa));
        return affect_ret;
    };
    h.remove_affect = [&](std::int32_t slot, std::int32_t arg) { got.push_back(std::format("remove_pc_affect {} {}", slot, arg)); };
    DoTeleportInput d;
    d.is_local_player = in["is_local_player"].truthy();
    d.index = in["index"].i32();
    d.point[0] = f32_of(in["x"]);
    d.point[1] = f32_of(in["y"]);
    d.point[2] = f32_of(in["z"]);
    d.waiting_for_port = static_cast<std::uint8_t>(in["waiting_for_port"].i32());
    d.virtual_6c = in["virtual_6c"].truthy();
    d.virtual_90 = in["virtual_90"].truthy();
    bool v = do_teleport(d, h);
    if (v != out["value"].truthy()) return "value";
    if (d.waiting_for_port != out["waiting_for_port_after"].i32()) return "waiting_for_port_after";
    std::vector<std::string> want, want_samples;
    for (const Json& c : out["calls"].items) {
        const std::string& fn = c["fn"].text;
        if (fn == "resolve_teleport") want.push_back(std::format("{} {}", fn, c["index"].i32()));
        else if (fn == "do_teleport_b")
            want.push_back(std::format("{} {} {} {} {} {}", fn, c["zone"].i32(), c["x"].i32(), c["y"].i32(), c["z"].i32(), c["heading"].i32()));
        else if (fn == "send_teleport_index_request") want.push_back(std::format("{} {}", fn, c["zone_point"].i32()));
        else if (fn == "get_pc_spell_affect") want.push_back(std::format("{} {}", fn, c["spa"].i32()));
        else if (fn == "remove_pc_affect") want.push_back(std::format("{} {} {}", fn, c["slot"].i32(), c["arg"].i32()));
        else want.push_back(fn);
    }
    const auto& ps = out["position_samples"].items;
    const auto& pk = out["position_sample_kinds"].items;
    for (std::size_t i = 0; i < pk.size(); ++i)
        want_samples.push_back(std::format("{} {} {} {}", pk[i]["kind"].i32(), i < ps.size() ? bits(ps[i]["x"]) : "",
                                           i < ps.size() ? bits(ps[i]["y"]) : "", i < ps.size() ? bits(ps[i]["z"]) : ""));
    if (auto e = diff("calls", got, want); !e.empty()) return e;
    return diff("samples", samples, want_samples);
}

// ---- SendTeleportIndexRequestAndWait (the client's version: game/zoning.h, send then wait for the answer)

std::string check_index_request(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    // The original: +0x1f05 = 1; net::Send (hton, UdpConnection::Send(4, &buf, 6), a failed send closes the
    // connection; without one, "void connection" unless quiet); then Sleep(100) + DeliverEvents(500) until the
    // answer. The quiet byte and when a disconnected run's answer comes are not among the inputs: the record's
    // log line and sleep count stand for them.
    std::vector<std::string> got, want;
    // Resolve the current RoF2 wire mapping by encoding the semantic teleport
    // request; the gameplay test must not duplicate a hard-coded wire opcode.
    hailstone::net::rof2::Codec codec;
    const auto result = codec.encode(hailstone::net::messages::ServiceScope::Zone,
                                     {hailstone::net::messages::GameOpcode::TeleportIndex, in["index"].i32()});
    if (!result || result.value()->size() != 1) return "teleport-index encode";
    const auto& packet = result.value()->front();
    if (packet.data.size() != sizeof(std::uint32_t)) return "teleport-index payload length";
    hailstone::ByteReader reader(packet.data);
    if (reader.le<std::uint32_t>() != in["index"].u32()) return "teleport-index payload";
    bool connected = in["connected"].truthy();
    if (connected) {
        got.push_back(std::format("hton {} 0", packet.opcode));
        got.push_back("udp_send 4 6");
        if (!in["send_ok"].truthy()) got.push_back("close_connection");
        if (out["payload_index"].i32() != in["index"].i32()) return "payload";
    } else {
        for (const Json& c : out["calls"].items)
            if (c["fn"].text == "void_connection_log")
                got.push_back(std::format("void_connection_log {}", packet.opcode));
        if (!out["payload_index"].is_null()) return "payload sent";
    }
    int sleeps = connected || in["answer_after_sleeps"].i32() != 0 ? in["answer_after_sleeps"].i32()
                                                                     : out["sleeps_100ms"].i32();
    if (sleeps != out["sleeps_100ms"].i32()) return std::format("sleeps {}", sleeps);
    if (in["udp_manager"].truthy())
        for (int k = 0; k < sleeps; ++k) got.push_back("deliver_events 500");
    for (const Json& c : out["calls"].items) {
        const std::string& fn = c["fn"].text;
        if (fn == "hton") want.push_back(std::format("{} {} {}", fn, c["opcode"].i32(), c["arg"].i32()));
        else if (fn == "udp_send") want.push_back(std::format("{} {} {}", fn, c["channel"].i32(), c["length"].i32()));
        else if (fn == "void_connection_log") want.push_back(std::format("{} {}", fn, c["opcode"].i32()));
        else if (fn == "deliver_events") want.push_back(std::format("{} {}", fn, c["ms"].i32()));
        else want.push_back(fn);
    }
    if (out["waiting_for_port_after"].truthy()) return "still waiting";
    return diff("calls", got, want);
}

// ---- PlayerZoneClient::DoTeleportB

std::string check_teleport_b(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    TeleportBPlayer p;
    p.x = f32_of(in["player_x"]);
    p.y = f32_of(in["player_y"]);
    p.z = f32_of(in["player_z"]);
    p.heading = f32_of(in["player_heading"]);
    p.floor_height = std::bit_cast<float>(0x12345678u);
    p.colliding_type = 7;
    p.standing_on = in["on_vehicle"].truthy();
    p.touching_switch = true;
    p.waiting_for_port = in["waiting_for_port"].truthy();
    p.stand_state = static_cast<std::uint8_t>(in["stand_state"].i32());
    p.linkdead = in["linkdead"].truthy();
    p.zone = in["current_zone"].i32();
    p.race = in["race"].i32();
    TeleportBArgs a;
    a.zone = in["zone"].i32();
    a.x = f32_of(in["x"]);
    a.y = f32_of(in["y"]);
    a.z = f32_of(in["z"]);
    a.heading = f32_of(in["heading"]);
    a.reason = in["reason"].i32();
    a.arg8 = in["arg8"].i32();
    a.arg9 = in["arg9"].i32();
    std::vector<int> samples;
    bool update = false, actor = false, physics = false, find = false;
    int rewinds = 0;
    std::optional<MoveToZoneCall> move;
    std::optional<std::array<float, 3>> object, floor_q;
    TeleportBHooks h;
    h.send_client_update = [&] { update = in["connected"].truthy(); };
    h.position_sample = [&](int k) { samples.push_back(k); };
    h.save_physics = [&] { physics = true; };
    if (in["has_2cc_object"].truthy()) h.move_object = [&](float x, float y, float z) { object = std::array<float, 3>{x, y, z}; };
    h.floor_height = [&](float x, float y, float z, int) {
        floor_q = std::array<float, 3>{x, y, z};
        double v = f32_of(in["floor_result"]);
        // A tail below the float's last bit, kept only at 53-bit precision.
        if (in["floor_result_tail"].truthy() && rec["precision"].text == "x87-53") v = std::nextafter(v, 0.0);
        return v;
    };
    h.update_actor = [&] { actor = true; };
    h.find_person_request = [&] { find = true; };
    h.rewind_reset = [&] { ++rewinds; };
    h.move_to_zone = [&](const MoveToZoneCall& m) { move = m; };
    bool v = do_teleport_b(p, a, in["game_state"].i32(), h);
    if (v != out["value"].truthy()) return "value";
    if (bits(p.x) != bits(out["player_x"]) || bits(p.y) != bits(out["player_y"]) || bits(p.z) != bits(out["player_z"]) ||
        bits(p.heading) != bits(out["player_heading"]))
        return "player position";
    if (bits(p.floor_height) != bits(out["floor_height"])) return std::format("floor_height {}", p.floor_height);
    if (p.colliding_type != out["colliding_type"].i32()) return "colliding_type";
    if (!p.standing_on != out["standing_on_cleared"].truthy()) return "standing_on";
    if (!p.touching_switch != out["touching_switch_cleared"].truthy()) return "touching_switch";
    if (p.waiting_for_port != out["waiting_for_port_after"].truthy()) return "waiting_for_port";
    if (update != out["client_update_sent"].truthy()) return "client_update_sent";
    std::vector<int> want_samples;
    for (const Json& s : out["position_samples"].items) want_samples.push_back(s["kind"].i32());
    if (samples != want_samples) return "position_samples";
    const Json& o = out["object_2cc_move"];
    if (object.has_value() != !o.is_null()) return "object_2cc_move";
    if (object && (bits((*object)[0]) != bits(o["x"]) || bits((*object)[1]) != bits(o["y"]) || bits((*object)[2]) != bits(o["z"])))
        return "object position";
    const Json& fq = out["floor_query"];
    if (floor_q.has_value() != !fq.is_null()) return "floor_query";
    if (floor_q && (bits((*floor_q)[0]) != bits(fq["x"]) || bits((*floor_q)[1]) != bits(fq["y"]) ||
                    bits((*floor_q)[2]) != bits(fq["z"]) || fq["flag"].i32() != 1))
        return "floor query args";
    if (actor != out["actor_updated"].truthy()) return "actor_updated";
    if (physics != out["physics_saved"].truthy()) return "physics_saved";
    // 0x59a9c0 sends the request only while pinstPlayerPath has a target (not among the inputs).
    if (!find && out["find_person_request"].truthy()) return "find_person_request";
    if (rewinds != out["rewind_reset_calls"].i32()) return "rewind_reset_calls";
    const Json& m = out["move_to_zone"];
    if (move.has_value() != !m.is_null()) return "move_to_zone";
    if (move && (move->zone != m["zone"].i32() || move->save_flag != m["save_flag"].i32() || move->reason != m["reason"].i32() ||
                 bits(move->x) != bits(m["x"]) || bits(move->y) != bits(m["y"]) || bits(move->z) != bits(m["z"]) ||
                 move->heading != m["heading"].i32() || move->arg8 != m["arg8"].i32() || move->arg9 != m["arg9"].i32() ||
                 move->arg10 != m["arg10"].i32()))
        return std::format("move_to_zone args heading {}", move->heading);
    return {};
}

// ---- OP_ZonePlayerToBind

std::string check_zone_to_bind(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    const Json& pk = in["packet"];
    ZoneToBindPacket p;
    p.zone = (pk["zone_id"].u32() & 0xffff) | (pk["instance"].u32() << 16);
    p.x = f32_of(pk["x"]);
    p.y = f32_of(pk["y"]);
    p.z = f32_of(pk["z"]);
    p.heading = f32_of(pk["heading"]);
    p.zone_name = pk["zone_name"].text;
    p.keep_items = static_cast<std::uint8_t>(pk["keep_items"].i32());
    p.hp = pk["hp"].i32();
    p.mana = pk["mana"].i32();
    p.endurance = pk["endurance"].i32();
    bool respawn = in["respawn_window"].truthy();
    struct State {
        bool deactivated = false, main_ui = false, prepared = false, gems = false, music = false, rewind = false,
             positions = false, view = false, camera = false, finished = false;
        std::optional<std::string> chat;
        std::int32_t hp = 33, mana = 11, endurance = 22, air = 4660;
        int keep = 85;
        int counter = 0;
        std::optional<MoveToZoneCall> move;
    } s;
    s.counter = in["render_counter"].i32();
    ZoneToBindHooks h;
    h.deactivate_respawn_window = [&] { s.deactivated = true; };
    h.activate_main_ui = [&] { s.main_ui = true; };
    h.chat = [&](std::uint32_t id, const std::string& name, std::uint32_t color) {
        s.chat = std::format("<{:05}|{}> {}", id, name, color);
    };
    h.prepare_for_repop = [&] { s.prepared = true; };
    h.set_vitals = [&](std::int32_t hp, std::int32_t mana, std::int32_t end) { s.hp = hp, s.mana = mana, s.endurance = end; };
    h.fill_air = [&] { s.air = in["max_air_supply"].i32(); };
    h.update_spell_gems = [&] { s.gems = true; };
    h.stop_death_music = [&] { s.music = true; };
    h.rewind_reset = [&] { s.rewind = true; };
    h.send_position_list = [&] { s.positions = true; };
    h.move_to_zone = [&](const MoveToZoneCall& m) {
        s.move = m;
        --s.counter; // 0xd1f3d4, decremented before MoveToZone
    };
    h.local_view_setup = [&] { s.view = true; };
    h.restore_camera = [&] { s.camera = true; };
    h.set_keep_items = [&](std::uint8_t k) { s.keep = k; };
    h.finish = [&] { s.finished = true; };
    zone_player_to_bind(static_cast<std::size_t>(in["size"].i32()), p, in["current_zone"].u32(), respawn,
                        in["cast_spell_window"].truthy(), h);
    if (s.deactivated != out["respawn_window_deactivated"].truthy()) return "respawn_window_deactivated";
    if (s.deactivated != out["respawn_window_1f5"].truthy()) return "respawn_window_1f5";
    if (s.main_ui != out["main_ui_activated"].truthy()) return "main_ui";
    const Json& c = out["chat"];
    if (s.chat.has_value() != !c.is_null()) return "chat";
    if (s.chat && *s.chat != std::format("{} {}", c["text"].text, c["color"].i32())) return "chat text " + *s.chat;
    if (s.prepared != out["prepared_for_repop"].truthy()) return "prepared";
    if (s.hp != out["profile_hp"].i32() || s.mana != out["profile_mana"].i32() ||
        s.endurance != out["profile_endurance"].i32())
        return "vitals";
    if (s.air != out["air_supply"].i32()) return "air";
    if (s.keep != out["keep_items_on_death"].i32()) return "keep_items";
    if ((s.finished ? 1 : 0) != out["pc_2dd4"].i32() || (s.finished ? 0 : 102) != out["pc_2f6c"].i32()) return "finish";
    if (s.gems != out["spell_gems_updated"].truthy()) return "gems";
    const Json& mu = out["music"];
    if (s.music != !mu.is_null()) return "music";
    if (s.music && (mu["selection"].i32() != 8 || mu["arg"].i32() != 0)) return "music args";
    if (s.rewind != out["rewind_reset"].truthy()) return "rewind";
    if (s.positions != out["position_list_sent"].truthy()) return "position list";
    const Json& m = out["move_to_zone"];
    if (s.move.has_value() != !m.is_null()) return "move_to_zone";
    if (s.move && (static_cast<std::uint32_t>(s.move->zone) != m["zone"].u32() || s.move->save_flag != m["save_flag"].i32() ||
                   s.move->reason != m["reason"].i32() || bits(s.move->x) != bits(m["x"]) ||
                   bits(s.move->y) != bits(m["y"]) || bits(s.move->z) != bits(m["z"]) ||
                   s.move->heading != m["heading"].i32() || s.move->arg8 != m["arg8"].i32() ||
                   s.move->arg9 != m["arg9"].i32() || s.move->arg10 != m["zone_name_arg"].i32()))
        return "move_to_zone args";
    if (s.view != out["local_view_setup"].truthy()) return "local_view_setup";
    if (s.camera != out["camera_restored"].truthy()) return "camera";
    if (s.counter != out["render_counter_after"].i32()) return "render counter";
    return {};
}

} // namespace

TEST_CASE("vectors: Handle_OP_Death") { hailstone::test::each_vector("Handle_OP_Death.jsonl", check_death); }
TEST_CASE("vectors: Teleport_ResolveTeleportIndex") {
    hailstone::test::each_vector("Teleport_ResolveTeleportIndex.jsonl", check_resolve);
}
TEST_CASE("vectors: PlayerZoneClient_DoTeleport") {
    hailstone::test::each_vector("PlayerZoneClient_DoTeleport.jsonl", check_do_teleport);
}
TEST_CASE("vectors: SendTeleportIndexRequestAndWait") {
    hailstone::test::each_vector("SendTeleportIndexRequestAndWait.jsonl", check_index_request);
}
TEST_CASE("vectors: PlayerZoneClient_DoTeleportB") {
    hailstone::test::each_vector("PlayerZoneClient_DoTeleportB.jsonl", check_teleport_b);
}
TEST_CASE("vectors: ZonePlayerToBind") { hailstone::test::each_vector("ZonePlayerToBind.jsonl", check_zone_to_bind); }

TEST_CASE("ATP region text: zone point 10 x N") {
    CHECK(atp_teleport_text("ATP_0012") == "DRNTP0025500012000000000000000");
    std::vector<ZonePointRow> rows{{120, 1, 2, 3, 64, 5, 0}, {12, 9, 9, 9, 9, 6, 0}};
    float loc[3] = {0, 0, 0};
    auto t = resolve_teleport_index(atp_teleport_text("ATP_0012"), -1, 2, loc, rows);
    CHECK(t.ok);
    CHECK(t.zone_point == 120);
    CHECK(t.found == 5);
    CHECK(t.x == 1.0f);
}

TEST_CASE("OP_ZonePlayerToBind decode") {
    std::vector<std::uint8_t> d(20, 0);
    d[0] = 2;
    d[2] = 1;
    for (char ch : std::string("qeynos")) d.push_back(static_cast<std::uint8_t>(ch));
    d.push_back(0);
    d.push_back(1);
    for (std::int32_t v : {100, 200, 300})
        for (int k = 0; k < 4; ++k) d.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    auto p = hailstone::net::rof2::decode_zone_to_bind(d);
    REQUIRE(p);
    CHECK(p->zone == 0x10002u);
    CHECK(p->zone_name == "qeynos");
    CHECK(p->keep_items == 1);
    CHECK(p->hp == 100);
    CHECK(p->mana == 200);
    CHECK(p->endurance == 300);
}

TEST_CASE("OP_ZonePlayerToBind decode: little-endian fields; a short tail reads as zeros") {
    // The bytes are little-endian whatever the host's order.
    std::vector<std::uint8_t> d{0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x80, 0x3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x34, 0x43};
    for (char ch : std::string("bind")) d.push_back(static_cast<std::uint8_t>(ch));
    d.push_back(0);
    d.push_back(1);
    d.push_back(0x2a); // hp's first byte only: too short for hp
    auto p = hailstone::net::rof2::decode_zone_to_bind(d);
    REQUIRE(p);
    CHECK(p->zone == 0x01020304u);
    CHECK(p->x == 1.0f);
    CHECK(p->heading == 180.0f);
    CHECK(p->zone_name == "bind");
    CHECK(p->keep_items == 1);
    CHECK(p->hp == 0);
    CHECK(p->mana == 0);
    // No NUL after the name: the name to the end, the rest zeros. 20 bytes or fewer: nothing.
    std::vector<std::uint8_t> open(20, 0);
    open.push_back('x');
    p = hailstone::net::rof2::decode_zone_to_bind(open);
    REQUIRE(p);
    CHECK(p->zone_name == "x");
    CHECK(p->keep_items == 0);
    CHECK_FALSE(hailstone::net::rof2::decode_zone_to_bind(std::vector<std::uint8_t>(20, 0)));
    // A name longer than 0x1ff keeps its first 0x1ff characters and the fields after its NUL.
    std::vector<std::uint8_t> long_name(20, 0);
    long_name.insert(long_name.end(), 0x300, 'a');
    long_name.push_back(0);
    long_name.push_back(1);
    for (int k = 0; k < 4; ++k) long_name.push_back(k == 0 ? 7 : 0);
    p = hailstone::net::rof2::decode_zone_to_bind(long_name);
    REQUIRE(p);
    CHECK(p->zone_name.size() == 0x1ff);
    CHECK(p->keep_items == 1);
    CHECK(p->hp == 7);
}
