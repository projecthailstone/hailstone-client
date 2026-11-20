// CharacterZoneClient::UseSkill (game/mechanics/use_skill.h) against the original's vectors (recorded from eqgame.exe):
// UseSkill_Combat, _Activity, _Target and _Gather, 1500 records each.
//
// Each record holds the inputs and every callee the original called (the mocks with their arguments and answers,
// the fake virtuals as notes, the float fakes, every packet with its bytes) in call order. The port runs against a
// replay: each call it makes must be the record's next one with the same arguments (an argument recorded as null
// came from bytes the original never writes: any value passes), and gets the recorded answer. Packet bytes recorded
// as "xx" must be 0 from the client. Then the player fields and globals written are compared.
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "game/mechanics/use_skill.h"
#include "net/protocol/rof2/packets/ability_packets.h"
#include "vector_files.h"

using hailstone::test::Json;
namespace us = hailstone::game::mechanics::useskill;

namespace {

struct World {
    us::Spawn me, target, other;
    us::State s;
};

us::Spawn* spawn_named(World& w, const std::string& n) {
    if (n == "me") return &w.me;
    if (n == "target") return &w.target;
    if (n == "other") return &w.other;
    return nullptr;
}

void read_spawn(us::Spawn& s, const Json& j) {
    s.type = static_cast<std::uint8_t>(j["type"].i32());
    s.spawn_id = j["spawn_id"].u32();
    s.stand_state = static_cast<std::uint8_t>(j["stand_state"].i32());
    s.pvp = j["pvp"].truthy() ? 1 : 0;
    s.mercenary = j["mercenary"].truthy() ? 1 : 0;
    s.name = j["name"].text;
    s.displayed_name = s.name;
}

void read_world(World& w, const Json& in, const Json& record) {
    const Json& m = in["me"];
    us::Spawn& me = w.me;
    me.type = static_cast<std::uint8_t>(m["type"].i32());
    me.spawn_id = m["spawn_id"].u32();
    me.stand_state = static_cast<std::uint8_t>(m["stand_state"].i32());
    me.has_mount = m["has_mount"].truthy();
    me.sneak = m["sneak"].truthy() ? 1 : 0;
    me.hide_mode = m["hide_mode"].i32();
    me.pvp = m["pvp"].truthy() ? 1 : 0;
    me.body_wet = m["body_wet"].truthy() ? 1 : 0;
    me.fishing_event = m["fishing_event"].truthy() ? 1 : 0;
    me.fishing_eta = m["fishing_eta"].i32();
    me.combat_skill_used = {m["combat_skill_used_0"].i32(), m["combat_skill_used_1"].i32()};
    me.next_intimidate_time = m["next_intimidate_time"].i32();
    me.intimidate_count = static_cast<std::uint8_t>(m["intimidate_count"].i32());
    using hailstone::test::f32_bits;
    me.y = f32_bits(m["y"]);
    me.x = f32_bits(m["x"]);
    me.z = f32_bits(m["z"]);
    me.speed_y = f32_bits(m["speed_y"]);
    me.speed_x = f32_bits(m["speed_x"]);
    me.speed_z = f32_bits(m["speed_z"]);
    me.heading = f32_bits(m["heading"]);
    // The player's name is not among the inputs: when the target is the player, take it from the call that
    // shows it.
    me.name = "me";
    if (in["target"].text == "me")
        for (const Json& c : record["out"]["calls"].items) {
            const std::string& fn = c["fn"].text;
            if (fn == "trim_name") me.name = c["name"].text;
            else if (fn == "chat_template" || (fn == "build_token_string" && c["id"].i32() == 0x3095))
                me.name = c["p1"].text;
            else continue;
            break;
        }
    me.displayed_name = me.name;
    read_spawn(w.target, in["target_spawn"]);
    read_spawn(w.other, in["other_spawn"]);
    us::State& s = w.s;
    s.me = &w.me;
    s.target = spawn_named(w, in["target"].text);
    s.local_player = spawn_named(w, in["local_player"].text);
    s.controlled_player = spawn_named(w, in["controlled_player"].text);
    const Json& c = in["character"];
    s.status = static_cast<std::uint8_t>(c["status"].i32());
    s.stand_state = static_cast<std::uint8_t>(c["stand_state"].i32());
    s.skill_disarm = c["skill_disarm"].i32();
    s.skill_pick_pockets = c["skill_pick_pockets"].i32();
    s.skill_begging = c["skill_begging"].i32();
    s.innate_slam = c["innate_slam"].i32();
    const Json& g = in["globals"];
    s.connected = g["connected"].truthy();
    s.quiet_log = g["quiet_log"].truthy();
    s.attack = g["attack"].truthy();
    s.autofire = g["autofire"].truthy();
    s.item_pending = g["item_pending"].i32();
    s.currently_interacting = g["currently_interacting"].i32();
    s.ruleset = g["ruleset"].i32();
    s.activity = g["activity"].i32();
    s.feign_value = g["feign_value"].i32();
    s.display_time = g["display_time"].i32();
    s.tracking_window_visible = g["tracking_window_visible"].truthy();
    s.compass_window = g["compass_window"].truthy();
    s.hide_position = {0x11111111, 0x22222222, 0x33333333};
    s.send_count = 1000;
    s.precision = record["precision"].text == "x87-24" ? hailstone::game::mechanics::FpuPrecision::Single24
                                                       : hailstone::game::mechanics::FpuPrecision::Double53;
}

const char* obj_name(us::Obj o) {
    switch (o) {
    case us::Obj::Null: return "null";
    case us::Obj::Character: return "character";
    case us::Obj::Pc: return "pc";
    case us::Obj::PcCharacter: return "pc_character";
    case us::Obj::NullPcCharacter: return "null_pc_character";
    }
    return "?";
}

std::string hex(const std::vector<std::uint8_t>& p) {
    std::string out;
    char b[3];
    for (std::uint8_t v : p) {
        std::snprintf(b, sizeof b, "%02x", v);
        out += b;
    }
    return out;
}

class Replay final : public us::Queries {
public:
    Replay(World& w, const Json& calls) : w_(w), calls_(calls) {}
    std::string error;
    std::size_t used() const { return next_; }

    // ---- the record
    const Json* take(const char* fn) {
        if (!error.empty()) return nullptr;
        if (next_ >= calls_.items.size()) {
            error = std::string("extra call ") + fn;
            return nullptr;
        }
        const Json& c = calls_.items[next_];
        if (c["fn"].text != fn) {
            error = "call " + std::to_string(next_) + ": port " + fn + ", original " + c["fn"].text;
            return nullptr;
        }
        ++next_;
        return &c;
    }
    void arg(const Json* c, const char* key, std::int64_t v) {
        if (!c || !error.empty() || (*c)[key].is_null()) return;
        if (static_cast<std::int32_t>((*c)[key].i64()) != static_cast<std::int32_t>(v))
            error = std::string((*c)["fn"].text) + "." + key + ": port " + std::to_string(v) + ", original " +
                    (*c)[key].text;
    }
    void arg_s(const Json* c, const char* key, const std::string& v) {
        if (!c || !error.empty() || (*c)[key].is_null()) return;
        if ((*c)[key].text != v)
            error = std::string((*c)["fn"].text) + "." + key + ": port " + v + ", original " + (*c)[key].text;
    }
    void arg_b(const Json* c, const char* key, bool v) {
        if (!c || !error.empty() || (*c)[key].is_null()) return;
        if ((*c)[key].truthy() != v) error = std::string((*c)["fn"].text) + "." + key + " differs";
    }
    void arg_f(const Json* c, const char* key, std::uint32_t bits) {
        if (!c || !error.empty() || (*c)[key].is_null()) return;
        if (hailstone::test::f32_bits((*c)[key]) != bits) error = std::string((*c)["fn"].text) + "." + key + " differs";
    }
    std::int32_t ret(const Json* c) { return c ? (*c)["ret"].i32() : 0; }
    std::string role(const us::Spawn* p) const {
        if (!p) return "null";
        if (p == &w_.me) return "me";
        if (p == &w_.target) return "target";
        if (p == &w_.other) return "other";
        return "?";
    }
    us::Handle handle(const std::string& name) {
        if (name.empty() || name == "null") return 0;
        auto it = handles_.find(name);
        if (it != handles_.end()) return it->second;
        us::Handle h = handles_.size() + 1;
        handles_[name] = h;
        names_[h] = name;
        return h;
    }
    std::string handle_name(us::Handle h) const {
        if (!h) return "null";
        auto it = names_.find(h);
        return it == names_.end() ? "?" : it->second;
    }

    // ---- the queries
    std::int32_t cur_hp(std::int32_t a0, std::int32_t a1) override {
        auto* c = take("cur_hp");
        arg(c, "a0", a0);
        arg(c, "a1", a1);
        return ret(c);
    }
    std::int32_t get_adjusted_skill(std::int32_t skill) override {
        auto* c = take("get_adjusted_skill");
        arg(c, "skill", skill);
        return ret(c);
    }
    std::int32_t total_effect(std::int32_t spa, std::uint8_t a1, std::int32_t a2, std::uint8_t a3,
                              std::uint8_t a4) override {
        auto* c = take("total_effect");
        arg(c, "spa", spa);
        arg(c, "a1", a1);
        arg(c, "a2", a2);
        arg(c, "a3", a3);
        arg(c, "a4", a4);
        return ret(c);
    }
    std::string get_string(std::uint32_t id, std::int32_t a1) override {
        auto* c = take("get_string");
        arg(c, "id", id);
        arg(c, "a1", a1);
        return "#" + std::to_string(id);
    }
    void chat(const std::string& text, std::int32_t color, std::uint8_t a2, std::uint8_t a3) override {
        auto* c = take("chat");
        arg_s(c, "text", text);
        arg(c, "color", color);
        arg(c, "a2", a2);
        arg(c, "a3", a3);
    }
    us::Handle get_pc_spell_affect(us::Obj self, std::int32_t spa, std::int32_t* slot, bool other) override {
        auto* c = take("get_pc_spell_affect");
        arg_s(c, "this", obj_name(self));
        arg(c, "spa", spa);
        arg_b(c, "a2", other);
        if (c && slot) {
            if (!c->has("slot")) error = "get_pc_spell_affect: slot not written by the original";
            else *slot = (*c)["slot"].i32();
        } else if (c && c->has("slot")) {
            error = "get_pc_spell_affect: the original passed a slot";
        }
        return c ? handle((*c)["ret"].text) : 0;
    }
    std::int32_t get_skill_combat_type(std::uint32_t skill) override {
        auto* c = take("get_skill_combat_type");
        arg(c, "skill", skill);
        return ret(c);
    }
    bool is_available(std::uint32_t skill) override {
        auto* c = take("is_available");
        arg(c, "skill", skill);
        return ret(c) != 0;
    }
    std::int32_t get_skill_reuse_time(std::uint32_t skill) override {
        auto* c = take("get_skill_reuse_time");
        arg(c, "skill", skill);
        return ret(c);
    }
    void set_skill_timer_duration(std::uint32_t skill, std::int32_t ms) override {
        auto* c = take("set_skill_timer_duration");
        arg(c, "skill", skill);
        arg(c, "ms", ms);
    }
    void set_skill_last_used(std::uint32_t skill, std::int32_t time) override {
        auto* c = take("set_skill_last_used");
        arg(c, "skill", skill);
        arg(c, "time", time);
    }
    void set_skill_can_use(std::uint32_t skill, std::int32_t can_use) override {
        auto* c = take("set_skill_can_use");
        arg(c, "skill", skill);
        arg(c, "can_use", can_use);
    }
    bool send(std::int32_t channel, const hailstone::game::mechanics::SkillSend& action) override {
        auto* c = take("send");
        arg(c, "channel", channel);
        // UseSkill emits a typed intent. Serialize at the RoF2 codec boundary only
        // to check the original packet vectors; gameplay does not own wire opcodes.
        const auto packet = hailstone::net::encode_skill_send(action);
        if (!packet) {
            error = "unsupported skill action";
            return false;
        }
        arg(c, "opcode", packet->opcode);
        arg(c, "size", static_cast<std::int64_t>(packet->data.size() + 2));
        if (c && error.empty()) {
            std::vector<std::uint8_t> payload;
            payload.reserve(packet->data.size());
            for (const auto byte : packet->data) payload.push_back(std::to_integer<std::uint8_t>(byte));
            std::string want = (*c)["payload"].text;
            for (std::size_t i = 0; i + 1 < want.size(); i += 2)
                if (want[i] == 'x') {
                    want[i] = '0';
                    want[i + 1] = '0';
                }
            if (hex(payload) != want) error = "send payload: port " + hex(payload) + ", original " + (*c)["payload"].text;
        }
        return ret(c) != 0;
    }
    void close_connection() override { take("close_connection"); }
    void debug_log_void_connection(std::uint32_t opcode) override {
        arg(take("debug_log_void_connection"), "opcode", opcode);
    }
    void send_ldon_inspect(std::int32_t a0, std::int32_t a1) override { two("send_ldon_inspect", a0, a1); }
    void send_ldon_open(std::int32_t a0, std::int32_t a1) override { two("send_ldon_open", a0, a1); }
    void cmd_reveal(std::int32_t a0, std::int32_t a1) override { two("cmd_reveal", a0, a1); }
    void two(const char* fn, std::int32_t a0, std::int32_t a1) {
        auto* c = take(fn);
        arg(c, "a0", a0);
        arg(c, "a1", a1);
    }
    bool pc_get_hide(us::Obj pc) override {
        auto* c = take("pc_get_hide");
        arg_s(c, "this", obj_name(pc));
        return ret(c) != 0;
    }
    void pc_set_hide(us::Obj pc, std::uint8_t hide) override {
        auto* c = take("pc_set_hide");
        arg_s(c, "this", obj_name(pc));
        arg(c, "hide", hide);
    }
    void pc_set_sneak(us::Obj pc, std::uint8_t sneak) override {
        auto* c = take("pc_set_sneak");
        arg_s(c, "this", obj_name(pc));
        arg(c, "sneak", sneak);
    }
    void get_current_profile() override { take("get_current_profile"); }
    us::Handle get_item(std::int32_t slot) override {
        auto* c = take("get_item");
        arg(c, "slot", slot);
        return c ? handle((*c)["item"].text) : 0;
    }
    void item_ref_release() override { take("item_ref_release"); }
    bool can_use_item(us::Handle item, std::int32_t a1, std::int32_t a2) override {
        auto* c = take("can_use_item");
        arg_s(c, "item", handle_name(item));
        arg(c, "a1", a1);
        arg(c, "a2", a2);
        return ret(c) != 0;
    }
    std::int32_t item_getter(const char* fn, us::Handle item) {
        auto* c = take(fn);
        arg_s(c, "this", handle_name(item));
        return ret(c);
    }
    std::int32_t get_item_type(us::Handle item) override { return item_getter("get_item_type", item); }
    std::int32_t get_item_class(us::Handle item) override { return item_getter("get_item_class", item); }
    std::int32_t get_item_id(us::Handle item) override { return item_getter("get_item_id", item); }
    bool do_attack(us::Spawn& self, std::int32_t slot, std::uint32_t skill, us::Spawn* target) override {
        auto* c = take("do_attack");
        arg_s(c, "this", role(&self));
        arg(c, "slot", slot);
        arg(c, "skill", skill);
        arg_s(c, "target", role(target));
        return ret(c) != 0;
    }
    std::string trim_name(const std::string& name) override {
        arg_s(take("trim_name"), "name", name);
        return name;
    }
    std::string build_token_string(std::uint32_t id, const std::string& p1, std::int32_t p2) override {
        auto* c = take("build_token_string");
        arg(c, "id", id);
        arg_s(c, "p1", p1);
        arg(c, "p2", p2);
        return "#" + std::to_string(id) + ":" + p1;
    }
    void change_position(us::Spawn& spawn, std::uint8_t stand_state) override {
        auto* c = take("change_position");
        arg_s(c, "this", role(&spawn));
        arg(c, "stand_state", stand_state);
    }
    void set_auto_attack(std::uint8_t on) override { arg(take("set_auto_attack"), "on", on); }
    void set_auto_range_attack(std::uint8_t on) override { arg(take("set_auto_range_attack"), "on", on); }
    void update_my_visible_status(us::Obj self, std::uint8_t a0) override {
        auto* c = take("update_my_visible_status");
        arg_s(c, "this", obj_name(self));
        arg(c, "a0", a0);
    }
    void set_activity(std::int32_t kind, std::int32_t spawn) override {
        auto* c = take("set_activity");
        arg(c, "kind", kind);
        arg(c, "spawn", spawn);
    }
    std::int32_t rand_nbr() override { return ret(take("rand_nbr")); }
    std::int32_t random_low_high(std::int32_t low, std::int32_t high) override {
        auto* c = take("random_low_high");
        arg(c, "low", low);
        arg(c, "high", high);
        return ret(c);
    }
    void compass_reset() override { take("compass_reset"); }
    void send_track() override { take("send_track"); }
    std::int32_t not_on_same_deity(us::Spawn& self, us::Spawn& other, std::int32_t a1) override {
        auto* c = take("not_on_same_deity");
        arg_s(c, "this", role(&self));
        arg_s(c, "other", role(&other));
        arg(c, "a1", a1);
        return ret(c);
    }
    std::int32_t get_level(us::Spawn& spawn) override {
        auto* c = take("get_level");
        arg_s(c, "this", role(&spawn));
        return ret(c);
    }
    void push_pending_reason(std::int32_t reason) override { arg(take("push_pending_reason"), "reason", reason); }
    void push_interact_reason(std::int32_t reason) override { arg(take("push_interact_reason"), "reason", reason); }
    void make_me_visible(us::Obj self, std::int32_t a0, std::uint8_t a1) override {
        auto* c = take("make_me_visible");
        arg_s(c, "this", obj_name(self));
        arg(c, "a0", a0);
        arg(c, "a1", a1);
    }
    bool can_i_hit(us::Spawn& self, us::Spawn* target, std::uint32_t extra_bits) override {
        auto* c = take("can_i_hit");
        arg_s(c, "this", role(&self));
        arg_s(c, "target", role(target));
        arg_f(c, "extra", extra_bits);
        return ret(c) != 0;
    }
    void remove_pc_affect(us::Obj self, us::Handle affect, std::int32_t a1) override {
        auto* c = take("remove_pc_affect");
        arg_s(c, "this", obj_name(self));
        arg_s(c, "affect", handle_name(affect));
        arg(c, "a1", a1);
    }
    void notify_pc_affect_change(us::Obj self, std::int32_t slot, std::uint8_t a1) override {
        auto* c = take("notify_pc_affect_change");
        arg_s(c, "this", obj_name(self));
        arg(c, "slot", slot);
        arg(c, "a1", a1);
    }
    void player_per_frame_update(us::Spawn* spawn) override {
        arg_s(take("player_per_frame_update"), "this", role(spawn));
    }
    void token_message(us::Obj character, std::int32_t id, std::int32_t color, std::int32_t a3) override {
        auto* c = take("token_message");
        arg_b(c, "has_character", character != us::Obj::Null);
        arg(c, "id", id);
        arg(c, "color", color);
        arg(c, "a3", a3);
    }
    bool find_item_by_item_type(std::int32_t item_type, std::int32_t* slot, std::int32_t* bag) override {
        auto* c = take("find_item_by_item_type");
        arg(c, "item_type", item_type);
        if (c) {
            if (slot) *slot = (*c)["slot"].i32();
            if (bag) *bag = (*c)["bag_slot"].i32();
            if (c->has("slot") != (slot != nullptr) || c->has("bag_slot") != (bag != nullptr))
                error = "find_item_by_item_type: pointer arguments differ";
        }
        return ret(c) != 0;
    }
    std::string get_id_file(us::Handle item, std::int32_t a0) override {
        auto* c = take("get_id_file");
        arg_s(c, "this", handle_name(item));
        arg(c, "a0", a0);
        return "IT63";
    }
    void update_item_slot(us::Spawn& self, std::uint8_t slot, const std::string& id_file, std::int32_t a2,
                          std::int32_t a3) override {
        auto* c = take("update_item_slot");
        arg_s(c, "this", role(&self));
        arg(c, "slot", slot);
        arg_s(c, "id_file", id_file);
        arg(c, "a2", a2);
        arg(c, "a3", a3);
    }
    us::Handle get_animation(us::Spawn& self) override {
        auto* c = take("get_animation");
        arg_s(c, "this", role(&self));
        return c ? handle((*c)["ret"].text) : 0;
    }
    void animation_vf0c(us::Handle, std::int32_t a0, std::int32_t a1, std::int32_t a2, std::uint32_t a3,
                        std::int32_t a4, std::int32_t a5, std::int32_t a6) override {
        auto* c = take("animation_vf0c");
        arg(c, "a0", a0);
        arg(c, "a1", a1);
        arg(c, "a2", a2);
        arg_f(c, "a3", a3);
        arg(c, "a4", a4);
        arg(c, "a5", a5);
        arg(c, "a6", a6);
    }
    std::int32_t get_time() override { return ret(take("get_time")); }
    void chat_template(us::Obj character, std::uint32_t id, std::int32_t color, const std::string& p1,
                       std::int32_t p2) override {
        auto* c = take("chat_template");
        arg_s(c, "character", obj_name(character));
        arg(c, "id", id);
        arg(c, "color", color);
        arg_s(c, "p1", p1);
        arg(c, "p2", p2);
    }
    std::int32_t get_base_skill(std::int32_t skill, std::int32_t flag) override {
        auto* c = take("get_base_skill");
        arg(c, "skill", skill);
        arg(c, "flag", flag);
        return ret(c);
    }
    bool get_pc_client() override { return ret(take("get_pc_client")) != 0; }
    bool spawn_virtual(const char* fn, us::Spawn& s) {
        auto* c = take(fn);
        arg_s(c, "this", role(&s));
        return ret(c) != 0;
    }
    bool is_interactive_object(us::Spawn& s) override { return spawn_virtual("is_interactive_object", s); }
    bool is_vehicle_race(us::Spawn& s) override { return spawn_virtual("is_vehicle_race", s); }
    bool is_gm(us::Spawn& s) override { return spawn_virtual("is_gm", s); }
    void set_hide_mode(us::Spawn& s, std::int32_t mode) override {
        auto* c = take("set_hide_mode");
        arg_s(c, "this", role(&s));
        arg(c, "mode", mode);
        s.hide_mode = mode;
    }
    void tracking_window_show(std::uint8_t show, std::uint8_t top, std::uint8_t layout) override {
        auto* c = take("tracking_window_show");
        arg(c, "show", show);
        arg(c, "bring_to_top", top);
        arg(c, "update_layout", layout);
    }
    std::uint32_t player_distance(us::Spawn& a, us::Spawn& b, std::uint32_t extra) override {
        auto* c = take("player_distance");
        arg_s(c, "a", role(&a));
        arg_s(c, "b", role(&b));
        arg_f(c, "extra", extra);
        return c ? hailstone::test::f32_bits((*c)["ret"]) : 0;
    }
    std::uint32_t get_melee_range(us::Spawn& a, us::Spawn& b) override {
        auto* c = take("get_melee_range");
        arg_s(c, "a", role(&a));
        arg_s(c, "b", role(&b));
        return c ? hailstone::test::f32_bits((*c)["ret"]) : 0;
    }

private:
    World& w_;
    const Json& calls_;
    std::size_t next_ = 0;
    std::map<std::string, us::Handle> handles_;
    std::map<us::Handle, std::string> names_;
};

std::string check(const Json& r) {
    World w;
    read_world(w, r["in"], r);
    const Json& out = r["out"];
    Replay q(w, out["calls"]);
    us::use_skill(w.s, r["in"]["skill"].u32(), q);
    if (!q.error.empty()) return q.error;
    if (q.used() != out["calls"].items.size())
        return "missing call " + out["calls"][q.used()]["fn"].text + " (call " + std::to_string(q.used()) + ")";
    const Json& m = out["me"];
    if ((w.me.sneak != 0) != m["sneak"].truthy() || w.me.hide_mode != m["hide_mode"].i32() ||
        w.me.fishing_eta != m["fishing_eta"].i32() || (w.me.fishing_event != 0) != m["fishing_event"].truthy() ||
        w.me.next_intimidate_time != m["next_intimidate_time"].i32() ||
        w.me.intimidate_count != m["intimidate_count"].i32())
        return "player fields differ";
    const Json& g = out["globals"];
    if (w.s.hide_position[0] != g["hide_y"].i32() || w.s.hide_position[1] != g["hide_x"].i32() ||
        w.s.hide_position[2] != g["hide_z"].i32())
        return "hide position differs";
    if (w.s.item_pending != g["item_pending"].i32() || w.s.currently_interacting != g["currently_interacting"].i32() ||
        w.s.send_count != g["send_count"].i32())
        return "globals differ";
    return "";
}

} // namespace

TEST_CASE("UseSkill_Combat vectors: combat abilities, bash, slam, taunt") {
    hailstone::test::each_vector("UseSkill_Combat.jsonl", check);
}
TEST_CASE("UseSkill_Activity vectors: hide, sneak, feign death, sense heading, tracking, intimidation, LDoN") {
    hailstone::test::each_vector("UseSkill_Activity.jsonl", check);
}
TEST_CASE("UseSkill_Target vectors: begging, pick pockets, disarm, locks and traps") {
    hailstone::test::each_vector("UseSkill_Target.jsonl", check);
}
TEST_CASE("UseSkill_Gather vectors: bind wound, forage, fishing") {
    hailstone::test::each_vector("UseSkill_Gather.jsonl", check);
}
