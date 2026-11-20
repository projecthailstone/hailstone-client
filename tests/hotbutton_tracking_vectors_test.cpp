// The original's vectors (recorded from eqgame.exe): pressing a hot button, its right click and the
// socials' queue (game/hot_button_press.h), and the tracking window's list (game/tracking.h).
#include <doctest/doctest.h>

#include <bit>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "game/hot_button_press.h"
#include "game/tracking.h"
#include "net/protocol/rof2/packets/tracking_packets.h"
#include "net/protocol/rof2/opcode_map.h"
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

// The answers DoHotButton's callees gave, taken from the recorded calls in order.
const Json* recorded(const Json& calls, const char* fn, std::size_t& next) {
    for (; next < calls.items.size(); ++next)
        if (calls.items[next]["fn"].text == fn) return &calls.items[next++];
    return nullptr;
}

const char* window_name(HotButtonWindow w) {
    switch (w) {
    case HotButtonWindow::Options: return "options";
    case HotButtonWindow::Inventory: return "inventory";
    default: return "spellbook";
    }
}

class Press final : public HotButtonQueries {
public:
    Press(const Json& in, const Json& out) : in_(in), out_(out) {}
    std::vector<std::string> calls, notes;
    std::optional<std::pair<std::int32_t, std::int32_t>> point;

    void cant_use_command() override {
        calls.push_back("get_string 12084");
        calls.push_back("dsp_chat 2113941300 273 1 1");
    }
    void must_be_stopped() override {
        calls.push_back("get_string 12085");
        calls.push_back("dsp_chat_2 2113941301");
    }
    void execute_cmd(std::uint32_t c) override { calls.push_back(std::format("execute_cmd {} 1 0", c)); }
    void ranged_attack() override { calls.push_back("do_attack 11 0"); }
    std::uint32_t list_entry(std::uint8_t, std::int32_t) override {
        if (in_.has("list_entry")) return in_["list_entry"].u32();
        std::size_t at = 0;
        const Json* c = recorded(out_["calls"], "use_skill", at);
        return c ? (*c)["skill"].u32() : 0;
    }
    void use_skill(std::uint8_t s) override { calls.push_back(std::format("use_skill {}", s)); }
    std::int32_t combat_ability(std::int32_t index) override {
        calls.push_back(std::format("get_combat_ability {}", index));
        std::size_t at = 0;
        const Json* c = recorded(out_["calls"], "get_combat_ability", at);
        return c ? (*c)["ret"].i32() : 0;
    }
    bool spell_exists(std::int32_t spell) override {
        notes.push_back(std::format("spell_manager_get_spell {}", spell));
        return in_["spell_exists"].truthy();
    }
    void do_combat_ability(std::int32_t spell) override { calls.push_back(std::format("do_combat_ability {} 1", spell)); }
    void set_current_social(std::int32_t s) override { calls.push_back(std::format("set_current_social {}", s)); }
    void leadership_ability(std::int32_t g, std::int32_t a) override {
        calls.push_back(std::format("leadership_activate_ability {} {}", g, a));
    }
    std::optional<HotButtonSlot> slot_from(const char* fn) {
        std::size_t at = 0;
        const Json* c = recorded(out_["calls"], fn, at);
        if (!c || !(*c)["ret_nonnull"].truthy()) return std::nullopt;
        const Json& r = in_["slot_window_rect"];
        return HotButtonSlot{in_["slot_window_set"].truthy(), r["left"].i32(), r["top"].i32()};
    }
    std::optional<HotButtonSlot> find_inv_slot(std::int32_t slot, std::int32_t bag) override {
        calls.push_back(std::format("find_inv_slot {} {}", slot, bag));
        return slot_from("find_inv_slot");
    }
    std::optional<HotButtonSlot> find_krono_slot() override {
        calls.push_back("find_krono_slot");
        return slot_from("find_krono_slot");
    }
    void inv_slot_right_click(std::int32_t x, std::int32_t y) override {
        notes.push_back("slot_window_screen_rect");
        calls.push_back("inv_slot_right_click");
        point = {x, y};
    }
    void who() override { calls.push_back("who"); }
    void group_follow() override { calls.push_back("group_follow"); }
    void group_invite() override { calls.push_back("group_invite 0"); }
    void disband() override { calls.push_back("disband"); }
    void camp() override { calls.push_back("camp"); }
    void sit() override { calls.push_back("sit"); }
    void toggle_view() override { calls.push_back("toggle_view"); }
    bool window_active(HotButtonWindow) override {
        calls.push_back("window_is_active");
        const Json* c = recorded(out_["calls"], "window_is_active", active_at_);
        return c && (*c)["ret"].truthy();
    }
    void window_activate(HotButtonWindow w) override { notes.push_back(std::string(window_name(w)) + "_activate"); }
    void window_deactivate(HotButtonWindow w) override { notes.push_back(std::string(window_name(w)) + "_deactivate"); }
    void clear_bard_twist() override { calls.push_back("clear_bard_twist"); }
    void spell_gem_left_click(std::int32_t gem) override { calls.push_back(std::format("spell_gem_left_click {}", gem)); }
    std::optional<HotButtonPetButton> pet_button(std::int32_t b) override {
        calls.push_back(std::format("pet_window_get_button {}", b));
        std::size_t at = 0;
        const Json* c = recorded(out_["calls"], "pet_window_get_button", at);
        if (!c || !(*c)["ret_nonnull"].truthy()) return std::nullopt;
        return HotButtonPetButton{static_cast<std::uint8_t>(in_["pet_button_enabled"].i32()),
                                  static_cast<std::uint8_t>(in_["pet_button_checked"].i32())};
    }
    void pet_button_set_check(bool c) override { notes.push_back(std::format("pet_button_set_check {} 0", c ? 1 : 0)); }
    void pet_window_click(std::int32_t) override { notes.push_back("pet_window_notification 1 1 0"); }

private:
    const Json& in_;
    const Json& out_;
    std::size_t active_at_ = 0;
};

std::vector<std::string> wanted_calls(const Json& calls) {
    std::vector<std::string> want;
    for (const Json& c : calls.items) {
        const std::string& fn = c["fn"].text;
        if (fn == "get_string") want.push_back(std::format("get_string {}", c["id"].i32()));
        else if (fn == "dsp_chat")
            want.push_back(std::format("dsp_chat {} {} {} {}", c["text"].i64(), c["color"].i32(), c["a"].i32(), c["b"].i32()));
        else if (fn == "dsp_chat_2") want.push_back(std::format("dsp_chat_2 {}", c["text"].i64()));
        else if (fn == "execute_cmd")
            want.push_back(std::format("execute_cmd {} {} {}", c["command"].i32(), c["key_down"].i32(), c["data"].i32()));
        else if (fn == "do_attack") want.push_back(std::format("do_attack {} {}", c["skill"].i32(), c["a"].i32()));
        else if (fn == "use_skill") want.push_back(std::format("use_skill {}", c["skill"].u32() & 0xff));
        else if (fn == "get_combat_ability") want.push_back(std::format("get_combat_ability {}", c["index"].i32()));
        else if (fn == "do_combat_ability") want.push_back(std::format("do_combat_ability {} {}", c["spell"].i32(), c["a"].i32()));
        else if (fn == "set_current_social") want.push_back(std::format("set_current_social {}", c["social"].i32()));
        else if (fn == "leadership_activate_ability")
            want.push_back(std::format("{} {} {}", fn, c["group"].i32(), c["ability"].i32()));
        else if (fn == "find_inv_slot") want.push_back(std::format("find_inv_slot {} {}", c["slot"].i32(), c["bag_slot"].i32()));
        else if (fn == "group_invite") want.push_back(std::format("group_invite {}", c["name"].i32()));
        else if (fn == "spell_gem_left_click") want.push_back(std::format("{} {}", fn, c["gem"].i32()));
        else if (fn == "pet_window_get_button") want.push_back(std::format("{} {}", fn, c["button"].i32()));
        else want.push_back(fn);
    }
    return want;
}

std::vector<std::string> wanted_notes(const Json& notes) {
    std::vector<std::string> want;
    for (const Json& n : notes.items) {
        const std::string& fn = n["fn"].text;
        if (fn == "spell_manager_get_spell") want.push_back(std::format("{} {}", fn, n["spell"].i32()));
        else if (fn == "pet_button_set_check") want.push_back(std::format("{} {} {}", fn, n["check"].i32(), n["notify"].i32()));
        else if (fn == "pet_window_notification")
            want.push_back(std::format("{} {} {} {}", fn, n["sender_is_button"].i32(), n["message"].i32(), n["data"].i32()));
        else want.push_back(fn);
    }
    return want;
}

class RightClick final : public HotButtonRightClickQueries {
public:
    std::vector<std::string> calls;
    bool aa_activated = false;
    std::optional<std::pair<std::int32_t, std::int32_t>> point;
    void social_stop(std::int32_t s, bool keep) override { calls.push_back(std::format("social_stop {} {}", s, keep ? 1 : 0)); }
    void open_social_editor(std::int32_t s) override { calls.push_back(std::format("open_social_editor {}", s)); }
    void aa_window_show(std::int32_t g) override {
        aa_activated = true;
        calls.push_back(std::format("aa_window_show_ability {}", g));
    }
    void inv_slot_right_click(std::int32_t x, std::int32_t y) override {
        calls.push_back("inv_slot_right_click");
        point = {x, y};
    }
    void spell_gem_right_click(std::int32_t gem) override { calls.push_back(std::format("spell_gem_right_click {}", gem)); }
};

TrackEntry entry(const Json& j) {
    TrackEntry e;
    e.spawn_id = j["spawn_id"].i32();
    e.name = j["name"].text;
    e.sort_name = e.name; // trimName is mocked to return the name unchanged
    e.level = static_cast<std::uint8_t>(j["level"].i32());
    e.distance = f32_of(j["distance"]);
    e.position = j["position"].i32();
    e.flags = j["flags"].u32();
    return e;
}

std::string identity(const std::string& s) { return s; }

} // namespace

TEST_CASE("vectors: CHotButtonWnd_DoHotButton") {
    hailstone::test::each_vector("CHotButtonWnd_DoHotButton.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        const Json& out = rec["out"];
        HotButtonGlobals g;
        if (!in["stand_state"].is_null()) g.stand_state = static_cast<std::uint8_t>(in["stand_state"].i32());
        g.item_pending = in["item_pending"].i32();
        g.range_attack_ready = static_cast<std::uint8_t>(in["range_attack_ready"].i32());
        g.group_invited = static_cast<std::uint8_t>(in["group_invited"].i32());
        g.no_who = static_cast<std::uint8_t>(in["no_who"].i32());
        g.run_walk = static_cast<std::uint8_t>(in["run_walk"].i32());
        g.toggle_89c = static_cast<std::uint8_t>(in["toggle_89c"].i32());
        if (!in["controlled_speed"].is_null()) g.controlled_speed = f32_of(in["controlled_speed"]);
        HotButtonRecord r;
        r.type = static_cast<std::uint8_t>(in["record"]["type"].i32());
        r.value = in["record"]["value"].i32();
        if (in["record"].has("item") && !in["record"]["item"].is_null())
            r.item = HotButtonRecord::Item{static_cast<std::int16_t>(in["record"]["item"]["slot"].i32()),
                                           static_cast<std::int16_t>(in["record"]["item"]["bag_slot"].i32())};
        Press q(in, out);
        do_hot_button(g, r, in["mouse"].i32(), q);
        if (auto e = diff("calls", q.calls, wanted_calls(out["calls"])); !e.empty()) return e;
        if (auto e = diff("notes", q.notes, wanted_notes(out["notes"])); !e.empty()) return e;
        if (out.has("right_click_point") != q.point.has_value()) return "right click point presence";
        if (q.point && (q.point->first != out["right_click_point"]["x"].i32() || q.point->second != out["right_click_point"]["y"].i32()))
            return "right click point";
        if (g.range_attack_ready != out["range_attack_ready"].i32()) return "range_attack_ready";
        if (g.run_walk != out["run_walk"].i32()) return "run_walk";
        if (g.toggle_89c != out["toggle_89c"].i32()) return "toggle_89c";
        return {};
    });
}

TEST_CASE("vectors: CHotButton_RightClick (0x54fc10)") {
    hailstone::test::each_vector("CHotButton_RightClick.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        const Json& out = rec["out"];
        HotButtonRightClick c;
        c.type = static_cast<std::uint8_t>(in["type"].i32());
        c.value = in["value"].i32();
        c.current_social = in["current_social"].i32();
        c.social_cooldown_length = in["social_cooldown_length"].i32();
        if (in["slot_window"].truthy() && in["slot"].truthy())
            c.slot = HotButtonSlot{true, in["rect"]["left"].i32(), in["rect"]["top"].i32()};
        c.gem_window = in["gem_window"].truthy();
        c.no_spell_info = in["no_spell_info"].i32();
        RightClick q;
        bool result = hot_button_right_click(c, q);
        std::vector<std::string> want;
        for (const Json& x : out["calls"].items) {
            const std::string& fn = x["fn"].text;
            if (fn == "social_stop") want.push_back(std::format("{} {} {}", fn, x["social"].i32(), x["keep_queued"].i32()));
            else if (fn == "open_social_editor") want.push_back(std::format("{} {}", fn, x["social"].i32()));
            else if (fn == "aa_window_show_ability") want.push_back(std::format("{} {}", fn, x["group"].i32()));
            else if (fn == "spell_gem_right_click") want.push_back(std::format("{} {}", fn, x["gem"].i32()));
            else want.push_back(fn);
        }
        if (auto e = diff("calls", q.calls, want); !e.empty()) return e;
        if (q.aa_activated != out["aa_window_activated"].truthy()) return "aa_window_activated";
        if (out.has("right_click_point") != q.point.has_value()) return "right click point presence";
        if (q.point && (q.point->first != out["right_click_point"]["x"].i32() || q.point->second != out["right_click_point"]["y"].i32()))
            return "right click point";
        return result == out["result"].truthy() ? std::string() : std::string("result");
    });
}

TEST_CASE("vectors: Social_Stop (0x6143b0)") {
    hailstone::test::each_vector("Social_Stop.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        const Json& out = rec["out"];
        SocialQueue s;
        s.current = in["current_social"].i32();
        s.pause = in["pause"].i32();
        s.line = in["line"].i32();
        auto cleared = social_stop(s, in["social"].i32(), in["keep_queued"].truthy());
        if (s.current != out["current_social"].i32() || s.pause != out["pause"].i32() || s.line != out["line"].i32())
            return std::format("state {} {} {}", s.current, s.pause, s.line);
        std::vector<std::string> got, want;
        if (cleared) got.push_back(std::to_string(*cleared));
        for (const Json& c : out["cleared"].items) want.push_back(std::to_string(c.i32()));
        return diff("cleared", got, want);
    });
}

TEST_CASE("vectors: TrackSort (the seven tracking sort predicates)") {
    hailstone::test::each_vector("TrackSort.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        bool less = track_less(in["order"].i32(), entry(in["a"]), entry(in["b"]));
        return less == rec["out"]["a_before_b"].truthy() ? std::string() : std::string("a_before_b");
    });
}

TEST_CASE("vectors: CTrackingWnd_GetTrackColor") {
    hailstone::test::each_vector("CTrackingWnd_GetTrackColor.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        TrackEntry e;
        e.spawn_id = in["spawn_id"].i32();
        e.flags = in["flags"].u32();
        std::vector<std::int32_t> group;
        for (const Json& m : in["group"].items)
            if (!m.is_null()) group.push_back(m.i32());
        std::uint32_t c = track_color(e, group);
        return c == rec["out"]["color"].u32() ? std::string() : std::format("color {}", c);
    });
}

TEST_CASE("vectors: CTrackingWnd_OnProcessFrame") {
    hailstone::test::each_vector("CTrackingWnd_OnProcessFrame.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        const Json& out = rec["out"];
        bool active = false;
        std::int32_t result = 0;
        for (const Json& c : out["calls"].items) {
            if (c["fn"].text == "is_active") active = c["ret"].truthy();
            if (c["fn"].text == "cxwnd_on_process_frame") result = c["ret"].i32();
        }
        std::uint32_t next_controls = in["next_controls_update"].u32(), next_track = in["next_track_request"].u32();
        TrackFrame f = tracking_frame(active, in["now"].u32(), next_controls, next_track, in["auto_update"].truthy());
        std::vector<std::string> got{"is_active"}, want;
        if (f.refresh_controls) got.push_back("update_tracking_controls");
        if (f.send_track) got.push_back("send_op_track");
        got.push_back("cxwnd_on_process_frame");
        for (const Json& c : out["calls"].items) want.push_back(c["fn"].text);
        if (auto e = diff("calls", got, want); !e.empty()) return e;
        if (next_controls != out["next_controls_update"].u32() || next_track != out["next_track_request"].u32())
            return "next times";
        return result == out["result"].i32() ? std::string() : std::string("result");
    });
}

TEST_CASE("vectors: CTrackingWnd_SetTrackingList (the OP_Track reply and its sort)") {
    hailstone::test::each_vector("CTrackingWnd_SetTrackingList.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        const Json& out = rec["out"];
        std::vector<std::uint8_t> data;
        const std::string& hex = in["data"].text;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
            data.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        if (data.size() != in["length"].u32()) return "length";
        static TrackList list;
        for (auto& e : list) {
            e = TrackEntry{};
            e.level = 0xee; // the generator's fill
            e.position = static_cast<std::int32_t>(0xeeeeeeeeu);
            e.spawn_id = static_cast<std::int32_t>(0xeeeeeeeeu);
        }
        bool ranger = !in["class"].is_null() && in["class"].i32() == 4;
        std::vector<std::byte> raw;
        for (std::uint8_t b : data) raw.push_back(static_cast<std::byte>(b));
        auto reply = hailstone::net::decode_track_reply(
            {hailstone::net::rof2::to_wire(hailstone::net::rof2::Op::Track), std::move(raw)});
        if (!reply || reply->timer) return "track reply codec";
        for (auto& e : list) {
            e.spawn_id = 0;
            e.distance = -1.0f;
            e.name.clear();
            e.sort_name.clear();
            e.position = -1;
            e.flags = 0;
        }
        for (std::size_t k = 0; k < reply->entries.size() && k < list.size(); ++k) {
            list[k] = reply->entries[k];
            list[k].sort_name = identity(list[k].name);
        }
        const auto sort_order = ranger ? in["sort_order"].u32() : 0u;
        if (sort_order <= 6) track_sort(list, static_cast<int>(sort_order));
        std::vector<std::string> got, want;
        for (int n = 0; n < kTrackEntries; ++n) {
            const TrackEntry& e = list[static_cast<std::size_t>(n)];
            if (e.position == -1) continue;
            got.push_back(std::format("{}:{}:{}:{}:{:08x}:{}:{}", n, e.spawn_id, e.name, e.level,
                                      std::bit_cast<std::uint32_t>(e.distance), e.position, e.flags));
        }
        for (const Json& e : out["entries"].items)
            want.push_back(std::format("{}:{}:{}:{}:{:08x}:{}:{}", e["slot"].i32(), e["spawn_id"].i32(), e["name"].text,
                                       e["level"].i32(), f32_bits(e["distance"]), e["position"].i32(), e["flags"].u32()));
        if (auto e = diff("entries", got, want); !e.empty()) return e;
        std::vector<std::string> calls, wcalls;
        for (const Json& c : out["calls"].items)
            wcalls.push_back(c["fn"].text + " " + std::to_string(c.has("refresh") ? c["refresh"].i32() : c["keep_selection"].i32()));
        calls = {"do_track_filter 0", "add_to_list 1"};
        // A ranger's name sorts (orders 1 and 4) mock trimName twice per comparison: the generator's call log fills
        // up and drops the two calls after the sort in some records, so a prefix of them is what was recorded.
        std::uint32_t order = in["sort_order"].u32();
        if (ranger && (order == 1 || order == 4) && wcalls.size() < calls.size()) calls.resize(wcalls.size());
        return diff("calls", calls, wcalls);
    });
}
