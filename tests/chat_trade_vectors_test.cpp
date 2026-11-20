// The original's vectors (recorded from eqgame.exe): the chat input mode (game/chat_input.h), con levels
// under the PvP rule sets (game/combat.h), region name flags (game/zone_lines.h), IsOkToTransact and
// Handle_OP_TradeRequest (game/trade_rules.h) and Handle_OP_GroupInvite2 (game/group_rules.h).
#include <doctest/doctest.h>

#include <format>
#include <string>
#include <vector>

#include "game/chat_input.h"
#include "game/combat.h"
#include "game/group_rules.h"
#include "game/trade_rules.h"
#include "game/zone_lines.h"
#include "net/protocol/rof2/opcode_map.h"
#include "vector_files.h"

using namespace hailstone::game;
using hailstone::test::Json;

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

// ---- chat input mode

ChatInputWorld chat_world(const Json& in) {
    ChatInputWorld w;
    w.mode = in["mode"].i32();
    w.chat_manager = in["chat_manager"].truthy();
    w.active_chat_window = in["active_chat_window"].truthy();
    w.forward = in["forward"].i32();
    w.backward = in["backward"].i32();
    w.turn_left = in["turn_left"].i32();
    w.turn_right = in["turn_right"].i32();
    w.held_extra = static_cast<std::uint8_t>(in["held_extra"].i32());
    return w;
}

std::string check_chat_calls(const ChatInputWorld& w, const std::vector<ChatInputCall>& calls, const Json& out) {
    if (w.mode != out["mode"].i32()) return std::format("mode {} want {}", w.mode, out["mode"].i32());
    std::vector<std::string> got, want;
    for (const auto& c : calls) {
        switch (c.fn) {
        case ChatInputCall::Fn::GetActiveChatWindow: got.push_back(std::format("window {}", c.ret_nonnull)); break;
        case ChatInputCall::Fn::InputSetFocus: got.push_back("set_focus"); break;
        case ChatInputCall::Fn::InputClearFocus: got.push_back("clear_focus"); break;
        case ChatInputCall::Fn::ExecuteCmd: got.push_back(std::format("execute 0 1 {}", c.held_mask)); break;
        }
    }
    for (const Json& c : out["calls"].items) {
        const std::string& fn = c["fn"].text;
        if (fn == "get_active_chat_window") want.push_back(std::format("window {}", c["ret_nonnull"].truthy()));
        else if (fn == "input_set_focus") want.push_back("set_focus");
        else if (fn == "input_clear_focus") want.push_back("clear_focus");
        else if (fn == "execute_cmd")
            want.push_back(std::format("execute {} {} {}", c["command"].i32(), c["key_down"].i32(), c["held_mask"].u32()));
        else want.push_back(fn);
    }
    return diff("calls", got, want);
}

// ---- trade

TransactState transact_state(const Json& in) {
    TransactState s;
    s.vehicle = in["vehicle"].truthy();
    s.vehicle_allows = in["vehicle_allows"].truthy();
    s.respawn_timer = in["respawn_timer"].u32();
    s.busy_ddf8a0 = in["busy_ddf8a0"].u32();
    s.currently_interacting = in["currently_interacting"].i32();
    s.item_pending = in["item_pending"].i32();
    s.container_manager = in["container_manager"].truthy();
    s.container_open = in["container_manager_a4"].u32();
    static const char* const kNames[] = {"spellbook", "train",    "bazaar", "barter",          "trade",
                                         "bank",      "merchant", "loot",   "barter_merchant", "guild_bank"};
    for (int k = 0; k < kTransactWindows; ++k) {
        const Json& w = in["windows"][kNames[k]];
        if (!w.is_null()) s.windows[static_cast<std::size_t>(k)] = w.truthy();
    }
    return s;
}

} // namespace

TEST_CASE("vectors: CEverQuest_SetChatInputMode (0x518230)") {
    hailstone::test::each_vector("CEverQuest_SetChatInputMode.jsonl", [](const Json& rec) {
        ChatInputWorld w = chat_world(rec["in"]);
        std::vector<ChatInputCall> calls;
        set_chat_input_mode(w, rec["in"]["new_mode"].i32(), calls);
        return check_chat_calls(w, calls, rec["out"]);
    });
}

TEST_CASE("vectors: CEverQuest_ToggleChatInputMode (0x51ebf0, the Enter key)") {
    hailstone::test::each_vector("CEverQuest_ToggleChatInputMode.jsonl", [](const Json& rec) {
        ChatInputWorld w = chat_world(rec["in"]);
        std::vector<ChatInputCall> calls;
        toggle_chat_input_mode(w, calls);
        return check_chat_calls(w, calls, rec["out"]);
    });
}

TEST_CASE("vectors: PcClient_GetConLevel under the PvP rule sets") {
    hailstone::test::each_vector("PcClient_GetConLevel.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        ConLevelInput c;
        c.pvp_rules = in["pvp_rules"].i32();
        c.my_level = in["my_level"].i32();
        c.their_level = in["their_level"].i32();
        c.standard = in["standard_color"].u32();
        c.spawn_type = static_cast<std::uint8_t>(in["spawn_type"].i32());
        c.virtual_50 = in["spawn_virtual_50"].truthy();
        c.virtual_98 = in["spawn_virtual_98"].truthy();
        c.field_1e60 = in["spawn_1e60"].i32();
        c.field_122a = in["spawn_122a"].truthy();
        // The game's own level table, which in.standard_color recorded.
        auto standard = static_cast<std::uint32_t>(standard_con(c.their_level, c.my_level));
        if (standard != c.standard) return std::format("standard_con({}, {}) = {} want {}", c.their_level, c.my_level, standard, c.standard);
        std::uint32_t v = con_level_value(c);
        if (v != rec["out"]["value"].u32()) return std::format("value {} want {}", v, rec["out"]["value"].u32());
        return {};
    });
}

TEST_CASE("vectors: ParseRegionNameFlags") {
    hailstone::test::each_vector("ParseRegionNameFlags.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        const Json& out = rec["out"];
        RegionNameFlags r = parse_region_name_flags(in["name"].is_null() ? nullptr : in["name"].text.c_str(), in["flags"].u32());
        if (r.flags != out["value"].u32()) return std::format("flags {:08x} want {:08x}", r.flags, out["value"].u32());
        const Json& t = out["teleport_name"];
        if (t.is_null() != !r.teleport_name.has_value()) return "teleport name presence";
        if (r.teleport_name && *r.teleport_name != t.text) return "teleport name " + *r.teleport_name + " want " + t.text;
        return {};
    });
}

TEST_CASE("vectors: CEverQuest_IsOkToTransact") {
    hailstone::test::each_vector("CEverQuest_IsOkToTransact.jsonl", [](const Json& rec) -> std::string {
        bool ok = is_ok_to_transact(transact_state(rec["in"]));
        return ok == rec["out"]["value"].truthy() ? std::string() : std::string("value");
    });
}

TEST_CASE("vectors: Handle_OP_TradeRequest") {
    hailstone::test::each_vector("Handle_OP_TradeRequest.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        const Json& out = rec["out"];
        TradeRequestInput t;
        t.requester_id = in["requester_id"].u32();
        t.requester_spawn = in["requester_spawn"].truthy();
        t.my_spawn_id = in["my_spawn_id"].u32();
        t.setting = in["trade_setting"].i32();
        t.pc = in["pc"].truthy();
        t.group = in["group"].truthy();
        t.requester_in_group = in["requester_in_group"].truthy();
        t.ok_to_transact = in["is_ok_to_transact"].i32() != 0;
        t.attacking = in["attacking"].truthy();
        t.world_connection = in["world_connection"].truthy();
        t.send_ok = in["send_ok"].truthy();
        t.quiet_debug = in["quiet_debug"].truthy();
        TradeRequestResult r = handle_trade_request(t);
        using F = TradeRequestCall::Fn;
        std::vector<std::string> got, want;
        for (const auto& c : r.calls) {
            switch (c.fn) {
            case F::GroupMemberLookup: got.push_back("group_member_lookup"); break;
            case F::IsOkToTransact: got.push_back(std::format("is_ok_to_transact {}", c.value)); break;
            case F::PendingRequestReport: got.push_back(std::format("pending_request_report {}", c.value)); break;
            case F::TradeWindowActivate: got.push_back("trade_window_activate 1 0"); break;
            case F::Send: got.push_back(std::format("send 4 {} {}", c.value, t.send_ok)); break;
            case F::CloseWorldConnection: got.push_back("close_world_connection"); break;
            case F::DebugLog: got.push_back(std::format("debug_log {}", c.value)); break;
            case F::FormatMessage: got.push_back(std::format("format_message {} 1 0", c.value)); break;
            case F::Chat: got.push_back(std::format("chat {} 1 1", c.value)); break;
            }
        }
        for (const Json& c : out["calls"].items) {
            const std::string& fn = c["fn"].text;
            if (fn == "group_member_lookup") {
                // Its "ret" is the low byte of the mock's 0x5eed3000: always 0 (in.requester_in_group has the answer).
                if (!c["requester"].truthy()) return "lookup of another spawn";
                want.push_back(fn);
            } else if (fn == "is_ok_to_transact") want.push_back(std::format("{} {}", fn, c["ret"].i32() != 0 ? 1 : 0));
            else if (fn == "pending_request_report") want.push_back(std::format("{} {}", fn, c["code"].i32()));
            else if (fn == "trade_window_activate") want.push_back(std::format("{} {} {}", fn, c["with_requester"].i32(), c["arg2"].i32()));
            else if (fn == "send") want.push_back(std::format("send {} {} {}", c["channel"].i32(), c["length"].i32(), c["ret"].i32() != 0));
            else if (fn == "debug_log") want.push_back(std::format("{} {}", fn, c["opcode"].i32()));
            else if (fn == "format_message")
                want.push_back(std::format("{} {} {} {}", fn, c["string_id"].i32(), c["with_requester_name"].i32(), c["with_inviter_name"].i32()));
            else if (fn == "chat") want.push_back(std::format("chat {} {} {}", c["color"].i32(), c["arg2"].i32(), c["arg3"].i32()));
            else want.push_back(fn);
        }
        if (auto e = diff("calls", got, want); !e.empty()) return e;
        const Json& reply = out["reply"];
        bool sent = r.refusal && t.world_connection;
        if (reply.is_null() != !sent) return "reply presence";
        if (sent) {
            if (reply["opcode"].u32() != kOpTradeRequestAck || reply["length"].i32() != 14) return "reply opcode / length";
            if (reply["requester_id"].u32() != r.refusal->requester_id || reply["my_spawn_id"].u32() != r.refusal->my_spawn_id)
                return "reply ids";
            if (reply["reason"].i32() != r.refusal->reason) return std::format("reason {} want {}", r.refusal->reason, reply["reason"].i32());
        }
        return {};
    });
}

TEST_CASE("vectors: Handle_OP_GroupInvite2") {
    hailstone::test::each_vector("Handle_OP_GroupInvite2.jsonl", [](const Json& rec) -> std::string {
        const Json& in = rec["in"];
        const Json& out = rec["out"];
        GroupInviteState st;
        st.invite_pending = in["invite_pending"].truthy();
        st.follow_with_group_follow = in["follow_with_group_follow"].truthy();
        st.pending_word_148 = 0x5eedface; // the generator's untouched marker
        st.pending_inviter = "";
        GroupInviteInput g;
        g.first_invite_opcode = in["opcode"].u32() ==
                                hailstone::net::rof2::to_wire(hailstone::net::rof2::Op::GroupInvite);
        g.inviter = in["inviter"].text;
        g.my_name = in["my_name"].text;
        g.word_148 = in["word_148"].u32();
        g.player_invited = in["player_invited"].truthy();
        g.raid_invited = in["raid_invited"].truthy();
        g.in_raid = in["in_raid"].truthy();
        g.inviter_raid_member = in["inviter_raid_member"].truthy();
        g.raid_group_leader = in["raid_group_leader"].truthy();
        g.world_connection = in["world_connection"].truthy();
        g.send_ok = in["send_ok"].truthy();
        g.quiet_debug = in["quiet_debug"].truthy();
        GroupInviteResult r = handle_group_invite(st, g);
        using F = GroupInviteCall::Fn;
        std::vector<std::string> got, want;
        for (const auto& c : r.calls) {
            switch (c.fn) {
            case F::FormatMessage: got.push_back(std::format("format_message {} 0 1", c.value)); break;
            case F::Chat: got.push_back(std::format("chat {} 1 1", c.value)); break;
            case F::String: got.push_back(std::format("string {}", c.value)); break;
            case F::PlayerSetInvited: got.push_back(std::format("player_set_invited {}", c.value)); break;
            case F::Send: got.push_back(std::format("send 4 {} {}", c.value, g.send_ok)); break;
            case F::CloseWorldConnection: got.push_back("close_world_connection"); break;
            case F::DebugLog: got.push_back(std::format("debug_log {}", c.value)); break;
            }
        }
        for (const Json& c : out["calls"].items) {
            const std::string& fn = c["fn"].text;
            if (fn == "format_message")
                want.push_back(std::format("{} {} {} {}", fn, c["string_id"].i32(), c["with_requester_name"].i32(), c["with_inviter_name"].i32()));
            else if (fn == "chat") want.push_back(std::format("chat {} {} {}", c["color"].i32(), c["arg2"].i32(), c["arg3"].i32()));
            else if (fn == "string") want.push_back(std::format("string {}", c["string_id"].i32()));
            else if (fn == "player_set_invited") want.push_back(std::format("{} {}", fn, c["value"].i32()));
            else if (fn == "send") want.push_back(std::format("send {} {} {}", c["channel"].i32(), c["length"].i32(), c["ret"].i32() != 0));
            else if (fn == "debug_log") want.push_back(std::format("{} {}", fn, c["opcode"].i32()));
            else want.push_back(fn);
        }
        if (auto e = diff("calls", got, want); !e.empty()) return e;
        const Json& cancel = out["cancel"];
        bool sent = r.cancel && g.world_connection;
        if (cancel.is_null() != !sent) return "cancel presence";
        if (sent) {
            if (cancel["opcode"].u32() != kOpCancelInvite || cancel["length"].i32() != 154) return "cancel opcode / length";
            if (cancel["inviter"].text != r.cancel->inviter || cancel["me"].text != r.cancel->me) return "cancel names";
            if (cancel["reason"].i32() != r.cancel->reason) return std::format("reason {} want {}", r.cancel->reason, cancel["reason"].i32());
        }
        if (st.invite_pending != out["invite_pending"].truthy()) return "invite_pending";
        if (st.follow_with_group_follow != out["follow_with_group_follow"].truthy()) return "follow_with_group_follow";
        if (st.pending_word_148 != out["pending_word_148"].u32()) return "pending_word_148";
        if (st.pending_inviter != out["pending_inviter"].text) return "pending_inviter";
        return {};
    });
}
