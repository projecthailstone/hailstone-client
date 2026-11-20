// The original's vectors (recorded from eqgame.exe): the
// client mercenary manager's handlers, senders and upkeep timer (game/mercenary.h). Each record's mocked calls
// (chat lines, events, packets, window lookups, reports) are compared in order with what the client reports through
// MercHooks; the senders' network outcome (no connection: the "void connection" log unless quieted; a failed send
// closes the connection) is modelled here as net::Send does it.
#include <doctest/doctest.h>

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "game/mercenary.h"
#include "net/protocol/rof2/packets/merc_packets.h"
#include "net/protocol/rof2/opcode_map.h"
#include "vector_files.h"

using namespace hailstone::game;
using hailstone::test::Json;
using hailstone::net::messages::MercenaryAction;
using hailstone::net::messages::MercenaryNotice;
using hailstone::net::messages::MercenaryNoticeKind;
using hailstone::net::messages::MercenaryRequest;
namespace rof2 = hailstone::net::rof2;

namespace {

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : " ; ") + x;
    return s;
}

std::string diff(const char* what, const std::vector<std::string>& got, const std::vector<std::string>& want) {
    if (got == want) return {};
    return std::string(what) + ": got [" + join(got) + "] want [" + join(want) + "]";
}

bool carries_argument(std::uint32_t id) { return id == 0x10 || id == 0x15 || id == 0x17 || id == 0x19; }

// MercHooks that records the calls as the vectors name them.
struct Recorder : MercHooks {
    std::vector<std::string> calls;
    bool sender = false; // model the network (connection, quiet, send result) as the senders' records do
    bool connected = true, quiet = false, send_ok = true;
    bool window_found = true;
    std::uint32_t ticks = 0, fast = 0;
    std::optional<std::pair<std::uint16_t, std::vector<std::uint8_t>>> sent; // what reached UdpConnection::Send

    void chat(const std::string& text, std::int32_t color) override {
        calls.push_back(color == kMercChatDefault ? "chat|" + text : std::format("chat|{}|{}", text, color));
    }
    void event(std::uint32_t id, std::int32_t argument) override {
        calls.push_back(carries_argument(id) ? std::format("event|{}|{}", id, argument) : std::format("event|{}", id));
    }
    void send(const MercenaryRequest& command) override {
        auto packet = rof2::encode_mercenary_request(command);
        if (!packet) { calls.push_back("unencodable mercenary command"); return; }
        const auto opcode = packet->opcode;
        std::vector<std::uint8_t> payload;
        for (std::byte b : packet->data) payload.push_back(std::to_integer<std::uint8_t>(b));
        std::size_t length = payload.size() + 2;
        if (!sender) {
            calls.push_back(std::format("send|{}|{}", opcode, length));
            return;
        }
        if (!connected) {
            if (!quiet) calls.push_back("debug_log|Attempt to send message %d on a void connection.");
            return;
        }
        calls.push_back(std::format("send|{}|{}|{}", opcode, length, send_ok ? 1 : 0));
        sent = std::make_pair(opcode, std::move(payload));
        if (!send_ok) calls.push_back("close_world_connection");
    }
    void send_stance(std::uint32_t command, std::uint32_t stance) override {
        MercenaryRequest request;
        request.action = MercenaryAction::SetStance;
        request.command = command;
        request.stance = stance;
        send(request);
    }
    void debug_log(const std::string& text) override { calls.push_back("debug_log|" + text); }
    bool find_window(const char* screen) override {
        calls.push_back(std::format("find_window|{}|{}", screen, window_found ? 1 : 0));
        return window_found;
    }
    void hide_window(const char*) override { calls.push_back("show_window|0|1|1"); }
    void store_window_visibility(const char*) override { calls.push_back("store_window_visibility"); }
    void report_upkeep_charged(std::uint8_t alt) override { calls.push_back(std::format("report_upkeep_charged|{}", alt)); }
    void report_upkeep_waived() override { calls.push_back("report_upkeep_waived"); }
    std::uint32_t tick_count() override { return ticks; }
    std::uint32_t fast_time() override { return fast; }
    void confirm(std::uint32_t eqstr, std::int32_t dialog, std::int32_t ms) override {
        calls.push_back(std::format("eqstr|{}", eqstr));
        calls.push_back(std::format("confirmation_dialog|{}|{}|1", dialog, ms));
    }
};

// The vectors' calls in the recorder's form. The switch handler's duration message comes as format_duration
// (GetBuffTimerTextString's seconds), format (the format) and chat (the mock's text, the duration alone): it becomes
// the format with buff_timer_text(seconds) in place of %s.
std::vector<std::string> want_calls(const Json& calls) {
    std::vector<std::string> want;
    std::int32_t seconds = 0;
    std::string format_text;
    for (const Json& c : calls.items) {
        const std::string& fn = c["fn"].text;
        if (fn == "chat") {
            std::string text = c["text"].text;
            if (!format_text.empty()) {
                std::size_t at = format_text.find("%s");
                text = format_text.substr(0, at) + buff_timer_text(seconds) + format_text.substr(at + 2);
                format_text.clear();
            }
            want.push_back(c.has("color") ? std::format("chat|{}|{}", text, c["color"].i32()) : "chat|" + text);
        } else if (fn == "event") {
            want.push_back(c.has("argument") ? std::format("event|{}|{}", c["id"].i32(), c["argument"].i32())
                                             : std::format("event|{}", c["id"].i32()));
        } else if (fn == "send") {
            want.push_back(std::format("send|{}|{}|{}", c["opcode"].i32(), c["length"].i32(), c["ok"].i32()));
        } else if (fn == "debug_log") {
            want.push_back("debug_log|" + c["text"].text);
        } else if (fn == "find_window") {
            want.push_back(std::format("find_window|{}|{}", c["text"].text, c["found"].i32()));
        } else if (fn == "show_window") {
            want.push_back(std::format("show_window|{}|{}|{}", c["show"].i32(), c["arg2"].i32(), c["arg3"].i32()));
        } else if (fn == "report_upkeep_charged") {
            want.push_back(std::format("report_upkeep_charged|{}", c["in_alt_currency"].i32()));
        } else if (fn == "send_data_update_request") {
            want.push_back(std::format("send|{}|2", rof2::to_wire(rof2::Op::MercenaryDataUpdateRequest))); // Send_OP_MercenaryDataUpdateRequest
        } else if (fn == "format_duration") {
            seconds = c["seconds"].i32();
        } else if (fn == "format") {
            format_text = c["text"].text;
        } else if (fn == "eqstr") {
            want.push_back(std::format("eqstr|{}", c["id"].i32()));
        } else if (fn == "confirmation_dialog") {
            want.push_back(std::format("confirmation_dialog|{}|{}|{}", c["dialog"].i32(), c["milliseconds"].i32(),
                                       c["data_is_manager_0x248"].i32()));
        } else if (fn == "set_row_color") {
            want.push_back(std::format("set_row_color|{}|{}|{}", c["row"].i32(), c["column"].i32(), c["color"].i32()));
        } else {
            want.push_back(fn);
        }
    }
    return want;
}

// VmWriteManager's fields.
void load_manager(MercManager& m, const Json& in) {
    m.has_mercenary = in["has_mercenary"].truthy() ? 1 : 0;
    m.state = in["state"].u32();
    m.suspend_time = in["suspend_time"].u32();
    m.active.purchase_cost = in["purchase_cost"].i32();
    m.active.upkeep_cost = in["upkeep_cost"].i32();
    m.active.alt_purchase_cost = in["alt_purchase_cost"].i32();
    m.active.alt_upkeep_cost = in["alt_upkeep_cost"].i32();
    m.active.time_left_ms = in["time_left_ms"].u32();
    m.active.stance = in["stance"].u32();
    m.active.active_since = in["active_since"].u32();
    m.active.assist_mode = in["assist_mode"].truthy() ? 1 : 0;
    m.timer_interval = in["timer_interval"].u32();
    m.timer_dword_8 = in["timer_dword_8"].u32();
    m.last_tick = in["last_tick"].u32();
    m.current_index = in["current_index"].i32();
    m.owned.assign(static_cast<std::size_t>(in["owned_count"].i32()), MercRecord{});
}

std::string check_manager(const MercManager& m, const Json& out) {
    auto field = [&](const char* k, std::int64_t got) -> std::string {
        std::int64_t want = out[k].type == Json::Type::Bool ? (out[k].b ? 1 : 0) : out[k].i32();
        return got == want ? std::string() : std::format("{}: got {} want {}", k, got, want);
    };
    for (const std::string& e :
         {field("has_mercenary", m.has_mercenary != 0), field("state", static_cast<std::int32_t>(m.state)),
          field("suspend_time", static_cast<std::int32_t>(m.suspend_time)), field("purchase_cost", m.active.purchase_cost),
          field("upkeep_cost", m.active.upkeep_cost), field("alt_purchase_cost", m.active.alt_purchase_cost),
          field("alt_upkeep_cost", m.active.alt_upkeep_cost),
          field("time_left_ms", static_cast<std::int32_t>(m.active.time_left_ms)),
          field("stance", static_cast<std::int32_t>(m.active.stance)),
          field("active_since", static_cast<std::int32_t>(m.active.active_since)),
          field("assist_mode", m.active.assist_mode != 0),
          field("timer_interval", static_cast<std::int32_t>(m.timer_interval)),
          field("timer_dword_8", static_cast<std::int32_t>(m.timer_dword_8)),
          field("last_tick", static_cast<std::int32_t>(m.last_tick)), field("current_index", m.current_index),
          field("owned_count", static_cast<std::int32_t>(m.owned.size()))})
        if (!e.empty()) return e;
    return {};
}

// VmWriteRecord's fields.
std::string check_record(const MercRecord& r, const Json& j) {
    auto f = [&](const char* k, std::int64_t got) -> std::string {
        std::int64_t want = j[k].type == Json::Type::Bool ? (j[k].b ? 1 : 0) : j[k].i32();
        return got == want ? std::string() : std::format("{}: got {} want {}", k, got, want);
    };
    for (const std::string& e :
         {f("id", static_cast<std::int32_t>(r.id)), f("type", static_cast<std::int32_t>(r.type)),
          f("subtype", static_cast<std::int32_t>(r.subtype)), f("purchase_cost", r.purchase_cost),
          f("upkeep_cost", r.upkeep_cost), f("alt_purchase_cost", r.alt_purchase_cost),
          f("alt_upkeep_cost", r.alt_upkeep_cost), f("alt_currency", static_cast<std::int32_t>(r.alt_currency)),
          f("byte_0x20", r.byte_0x20), f("time_left_ms", static_cast<std::int32_t>(r.time_left_ms)),
          f("command", static_cast<std::int32_t>(r.command)), f("stance", static_cast<std::int32_t>(r.stance)),
          f("stance_count", static_cast<std::int32_t>(r.stance_count)),
          f("dword_0x34", static_cast<std::int32_t>(r.active_since)), f("assist_mode", r.assist_mode != 0),
          f("required_membership", static_cast<std::int32_t>(r.required_membership))})
        if (!e.empty()) return e;
    if (r.name != j["name"].text) return "name: got '" + r.name + "' want '" + j["name"].text + "'";
    const auto& st = j["stances"].items;
    if (st.size() != r.stances.size()) return std::format("stances: got {} want {}", r.stances.size(), st.size());
    for (std::size_t k = 0; k < st.size(); ++k)
        if (st[k]["index"].i32() != r.stances[k].index || st[k]["stance"].i32() != r.stances[k].stance)
            return std::format("stance {}", k);
    return {};
}

std::string check_list(const std::vector<MercRecord>& got, const Json& want, const char* what) {
    if (got.size() != want.items.size()) return std::format("{}: got {} want {}", what, got.size(), want.items.size());
    for (std::size_t k = 0; k < got.size(); ++k)
        if (auto e = check_record(got[k], want.items[k]); !e.empty()) return std::format("{}[{}]: {}", what, k, e);
    return {};
}

std::vector<std::uint8_t> hex_bytes(const std::string& hex, std::size_t size) {
    std::vector<std::uint8_t> b;
    for (std::size_t k = 0; k + 1 < hex.size() && b.size() < size; k += 2)
        b.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(k, 2), nullptr, 16)));
    return b;
}

// ---- the list handlers

std::optional<MercenaryNotice> decoded_notice(rof2::Op op, const std::vector<std::uint8_t>& bytes) {
    std::vector<std::byte> wire;
    wire.reserve(bytes.size());
    for (auto b : bytes) wire.push_back(static_cast<std::byte>(b));
    return rof2::decode_mercenary_notice({rof2::to_wire(op), std::move(wire)});
}

std::string check_data_response(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    m.liaison_id = in["merchant_id"].u32();
    Recorder h;
    h.window_found = in["window_found"].truthy();
    auto bytes = hex_bytes(in["packet"].text, static_cast<std::size_t>(in["size"].i32()));
    auto notice = decoded_notice(rof2::Op::MercenaryDataResponse, bytes);
    if (!notice) return "merchant-list codec";
    merc_handle_data_response(m, *notice, h);
    std::vector<std::string> types, want_types;
    for (std::uint32_t t : m.merc_types) types.push_back(std::to_string(static_cast<std::int32_t>(t)));
    for (const Json& t : out["merc_types"].items) want_types.push_back(std::to_string(t.i32()));
    if (auto e = diff("merc_types", types, want_types); !e.empty()) return e;
    if (auto e = check_list(m.merchant_list, out["mercs"], "mercs"); !e.empty()) return e;
    return diff("calls", h.calls, want_calls(out["calls"]));
}

std::string check_data_update(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m; // the generator's manager is zero apart from these
    m.state = in["state"].u32();
    m.slot_count = in["slot_count"].i32();
    m.current_index = 0;
    m.active.active_since = 0;
    Recorder h;
    h.window_found = in["window_found"].truthy();
    h.ticks = in["tick_count"].u32();
    auto bytes = hex_bytes(in["packet"].text, static_cast<std::size_t>(in["size"].i32()));
    auto notice = decoded_notice(rof2::Op::MercenaryDataUpdate, bytes);
    if (!notice) return "owned-list codec";
    merc_handle_data_update(m, *notice, h);
    if (m.current_index != out["current_index"].i32()) return std::format("current_index {}", m.current_index);
    if (m.slot_count != out["slot_count"].i32()) return std::format("slot_count {}", m.slot_count);
    if (auto e = check_list(m.owned, out["owned"], "owned"); !e.empty()) return e;
    if (auto e = check_record(m.active, out["active"]); !e.empty()) return "active: " + e;
    if (static_cast<std::int32_t>(m.stance_pointers) != out["stance_pointers"].i32()) return "stance_pointers";
    if (static_cast<std::int32_t>(m.last_tick) != out["last_tick"].i32()) return "last_tick";
    return diff("calls", h.calls, want_calls(out["calls"]));
}

// ---- the small handlers (VmHandler: the manager in and out, the packet as dwords or null)

using Handler = void (*)(MercManager&, const MercenaryNotice&, MercHooks&);

std::string check_handler(const Json& rec, Handler fn) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    load_manager(m, in);
    Recorder h;
    h.ticks = in["tick_count"].u32();
    h.fast = in["fast_time"].u32();
    MercenaryNotice notice;
    // The original vectors supply eight little-endian dwords. Decode them as the
    // same semantic fields that the RoF2 codec presents to the game handlers.
    const auto value = [&](std::size_t index) -> std::uint32_t {
        return index < in["packet"].items.size() ? in["packet"].items[index].u32() : 0u;
    };
    notice.result = value(0);
    notice.flag = static_cast<std::uint8_t>(value(0));
    notice.timer_interval = value(1);
    notice.timer_tag = value(2);
    notice.state = value(3);
    notice.suspend_time = fn == merc_handle_timer ? value(4) : value(0);
    notice.time_left = value(0);
    notice.seconds = value(1);
    notice.purchase_cost = static_cast<std::int32_t>(value(0));
    notice.alt_purchase_cost = static_cast<std::int32_t>(value(1));
    notice.upkeep_cost = static_cast<std::int32_t>(value(2));
    notice.alt_upkeep_cost = static_cast<std::int32_t>(value(3));
    fn(m, notice, h);
    if (auto e = check_manager(m, out); !e.empty()) return e;
    return diff("calls", h.calls, want_calls(out["calls"]));
}

// ---- the senders

void load_connection(Recorder& h, const Json& in) {
    h.sender = true;
    h.connected = in["world_connection"].truthy();
    h.quiet = in["quiet_debug"].truthy();
    h.send_ok = in["send_ok"].truthy();
}

// VmWriteSent: the opcode, length, `dwords` payload dwords and `bytes` bytes after them, or null.
std::string check_sent(const Recorder& h, const Json& sent) {
    if (sent.is_null()) return h.sent ? "sent: unexpected" : std::string();
    if (!h.sent) return "sent: missing";
    const auto& [op, p] = *h.sent;
    if (op != sent["opcode"].i32()) return "sent: opcode";
    if (static_cast<std::int32_t>(p.size() + 2) != sent["length"].i32()) return "sent: length";
    std::size_t at = 0;
    auto u32 = [&](std::size_t o) {
        std::uint32_t v = 0;
        for (int b = 0; b < 4; ++b) v |= static_cast<std::uint32_t>(o + static_cast<std::size_t>(b) < p.size() ? p[o + static_cast<std::size_t>(b)] : 0) << (8 * b);
        return static_cast<std::int32_t>(v);
    };
    for (const Json& d : sent["dwords"].items) {
        if (u32(at) != d.i32()) return std::format("sent: dword at {}", at);
        at += 4;
    }
    for (const Json& b : sent["bytes"].items) {
        if (at >= p.size() || p[at] != b.i32()) return std::format("sent: byte at {}", at);
        ++at;
    }
    return {};
}

std::string check_send_dismiss(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    load_manager(m, in);
    Recorder h;
    load_connection(h, in);
    merc_send_dismiss(m, in["confirm"].truthy(), in["send"].truthy(), h);
    if (auto e = check_sent(h, out["sent"]); !e.empty()) return e;
    if (auto e = check_manager(m, out); !e.empty()) return e;
    return diff("calls", h.calls, want_calls(out["calls"]));
}

std::string check_timer_and_data(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    load_manager(m, in);
    m.slot_count = 7;
    m.liaison_id = 99;
    Recorder h;
    load_connection(h, in);
    h.fast = in["fast_time"].u32();
    merc_request_timer_and_data(m, h);
    if (auto e = check_manager(m, out); !e.empty()) return e;
    if (m.slot_count != out["slot_count"].i32()) return "slot_count";
    if (static_cast<std::int32_t>(m.liaison_id) != out["merchant_id"].i32()) return "merchant_id";
    return diff("calls", h.calls, want_calls(out["calls"]));
}

std::string check_tick(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    m.state = in["state"].u32();
    m.active.time_left_ms = in["time_left_ms"].u32();
    m.last_tick = in["last_tick"].u32();
    merc_tick_upkeep(m, in["tick_count"].u32());
    if (static_cast<std::int32_t>(m.active.time_left_ms) != out["time_left_ms"].i32()) return "time_left_ms";
    if (static_cast<std::int32_t>(m.last_tick) != out["last_tick"].i32()) return "last_tick";
    return {};
}

std::string check_hire(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    m.selected.id = in["selected_entry_id"].u32();
    m.liaison_id = in["merchant_id"].u32();
    Recorder h;
    load_connection(h, in);
    merc_send_hire(m, in["merc_id"].u32(), h);
    if (auto e = check_sent(h, out["sent"]); !e.empty()) return e;
    return diff("calls", h.calls, want_calls(out["calls"]));
}

std::string check_assist_mode(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    m.active.assist_mode = static_cast<std::uint8_t>(in["assist_mode"].i32());
    Recorder h;
    load_connection(h, in);
    merc_send_assist_mode(m, static_cast<std::uint8_t>(in["assist"].i32()), h);
    if (m.active.assist_mode != out["assist_mode"].i32()) return "assist_mode";
    if (auto e = check_sent(h, out["sent"]); !e.empty()) return e;
    return diff("calls", h.calls, want_calls(out["calls"]));
}

std::string check_set_owner(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    m.has_mercenary = in["has_mercenary"].truthy() ? 1 : 0;
    const std::string& t = in["target"].text;
    Recorder h;
    load_connection(h, in);
    merc_request_set_owner(m, t == "none" ? 0 : t == "self" ? 1 : 2, in["my_spawn_id"].u32(), in["target_spawn_id"].u32(), h);
    if (auto e = check_sent(h, out["sent"]); !e.empty()) return e;
    return diff("calls", h.calls, want_calls(out["calls"]));
}

std::string check_send_switch(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    m.owned.assign(static_cast<std::size_t>(in["owned_count"].i32()), MercRecord{});
    m.current_index = in["current_index"].i32();
    Recorder h;
    load_connection(h, in);
    merc_send_switch(m, in["index"].i32(), h);
    if (auto e = check_sent(h, out["sent"]); !e.empty()) return e;
    return diff("calls", h.calls, want_calls(out["calls"]));
}

std::string check_send_command(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    MercManager m;
    m.active.stance = in["stance"].u32();
    m.active.command = in["command"].u32();
    std::vector<std::uint32_t> rows;
    for (const Json& r : in["row_stances"].items) rows.push_back(r.u32());
    Recorder h;
    load_connection(h, in);
    // A row outside the list: the generator's CListWnd::GetItemData mock answered from its own table, which only
    // the result shows.
    merc_send_command(m, rows, out["stance"].u32(), in["row"].i32(),
                      [&](std::int32_t row, std::int32_t column, std::uint32_t color) {
                          h.calls.push_back(std::format("set_row_color|{}|{}|{}", row, column, static_cast<std::int32_t>(color)));
                      },
                      h);
    if (static_cast<std::int32_t>(m.active.stance) != out["stance"].i32()) return "stance";
    if (auto e = check_sent(h, out["sent"]); !e.empty()) return e;
    return diff("calls", h.calls, want_calls(out["calls"]));
}

void run(const char* file, Handler fn) {
    hailstone::test::each_vector(file, [fn](const Json& r) { return check_handler(r, fn); });
}

} // namespace

TEST_CASE("vectors: Handle_OP_MercenaryDataResponse") {
    hailstone::test::each_vector("Handle_OP_MercenaryDataResponse.jsonl", check_data_response);
}
TEST_CASE("vectors: Handle_OP_MercenaryDataUpdate") {
    hailstone::test::each_vector("Handle_OP_MercenaryDataUpdate.jsonl", check_data_update);
}
TEST_CASE("vectors: Handle_OP_MercenaryHire") { run("Handle_OP_MercenaryHire.jsonl", merc_handle_hire); }
TEST_CASE("vectors: Handle_OP_MercenaryTimer") { run("Handle_OP_MercenaryTimer.jsonl", merc_handle_timer); }
TEST_CASE("vectors: Handle_OP_MercenarySuspendResponse") {
    run("Handle_OP_MercenarySuspendResponse.jsonl", merc_handle_suspend_response);
}
TEST_CASE("vectors: Handle_OP_MercenaryUnsuspendResponse") {
    run("Handle_OP_MercenaryUnsuspendResponse.jsonl", merc_handle_unsuspend_response);
}
TEST_CASE("vectors: Handle_Opcode_0x038f") { run("Handle_Opcode_0x038f.jsonl", merc_handle_died); }
TEST_CASE("vectors: Merc_HandleSuspended") { run("Merc_HandleSuspended.jsonl", merc_handle_suspended); }
TEST_CASE("vectors: Handle_OP_MercenaryCommand") { run("Handle_OP_MercenaryCommand.jsonl", merc_handle_command); }
TEST_CASE("vectors: Handle_OP_MercenarySwitch") { run("Handle_OP_MercenarySwitch.jsonl", merc_handle_switch); }
TEST_CASE("vectors: Handle_OP_MercenaryDismiss") { run("Handle_OP_MercenaryDismiss.jsonl", merc_handle_dismiss); }
TEST_CASE("vectors: Handle_OP_MercenaryUnknown1") { run("Handle_OP_MercenaryUnknown1.jsonl", merc_handle_unknown1); }
TEST_CASE("vectors: Handle_Opcode_0x2b4f") { run("Handle_Opcode_0x2b4f.jsonl", merc_handle_time_left); }
TEST_CASE("vectors: Merc_HandleUpkeepCharged") { run("Merc_HandleUpkeepCharged.jsonl", merc_handle_upkeep_charged); }
TEST_CASE("vectors: Handle_Opcode_0x3cbc") { run("Handle_Opcode_0x3cbc.jsonl", merc_handle_costs); }
TEST_CASE("vectors: Handle_Opcode_0x2749") { run("Handle_Opcode_0x2749.jsonl", merc_handle_timer_cleared); }
TEST_CASE("vectors: Handle_Opcode_0x20b9") { run("Handle_Opcode_0x20b9.jsonl", merc_handle_event7); }
TEST_CASE("vectors: Send_OP_MercenaryDismiss") { hailstone::test::each_vector("Send_OP_MercenaryDismiss.jsonl", check_send_dismiss); }
TEST_CASE("vectors: Merc_RequestTimerAndData") {
    hailstone::test::each_vector("Merc_RequestTimerAndData.jsonl", check_timer_and_data);
}
TEST_CASE("vectors: Merc_TickUpkeepTimer") { hailstone::test::each_vector("Merc_TickUpkeepTimer.jsonl", check_tick); }
TEST_CASE("vectors: Merc_SendHireRequest") { hailstone::test::each_vector("Merc_SendHireRequest.jsonl", check_hire); }
TEST_CASE("vectors: Send_Opcode_0x0b72") { hailstone::test::each_vector("Send_Opcode_0x0b72.jsonl", check_assist_mode); }
TEST_CASE("vectors: Merc_RequestSetOwner") { hailstone::test::each_vector("Merc_RequestSetOwner.jsonl", check_set_owner); }
TEST_CASE("vectors: Send_OP_MercenarySwitch") { hailstone::test::each_vector("Send_OP_MercenarySwitch.jsonl", check_send_switch); }
TEST_CASE("vectors: Send_OP_MercenaryCommand") {
    hailstone::test::each_vector("Send_OP_MercenaryCommand.jsonl", check_send_command);
}

// ---- unit checks of the texts and guards

TEST_CASE("mercenary: durations") {
    CHECK(merc_duration_short(0) == "0m");
    CHECK(merc_duration_short(65000) == "1m 5s");
    CHECK(merc_duration_short(900000) == "15m");
    CHECK(merc_duration_short(3600000) == "1h");
    CHECK(merc_duration_short(3720000) == "1h 2m");
    CHECK(merc_duration_long(900000) == "15 minutes");
    CHECK(merc_duration_long(3605000) == "1 hours and 5 seconds");
    CHECK(merc_duration_long(3725000) == "1 hours, 2 minutes, and 5 seconds");
    CHECK(merc_duration_long(125000) == "2 minutes and 5 seconds");
    CHECK(buff_timer_text(74591) == "20h:43m:11s");
    CHECK(buff_timer_text(59) == "59s");
    CHECK(buff_timer_text(3600) == "1h");
}

TEST_CASE("mercenary: time left text and upkeep tooltip") {
    MercManager m;
    m.state = kMercActive;
    m.active.time_left_ms = 65000;
    CHECK(merc_time_left_text(m) == "1m 5s");
    m.active.time_left_ms = 0;
    CHECK(merc_time_left_text(m) == "0m 0s");
    m.state = kMercSuspended;
    m.active.time_left_ms = 65000;
    CHECK(merc_time_left_text(m) == "0m 0s");
    m.state = kMercActive;
    m.active.upkeep_cost = 123;
    CHECK(merc_upkeep_tooltip(m) == "Time left until you get charged upkeep cost: 12p 3g.");
    m.active.upkeep_cost = 0;
    CHECK(merc_upkeep_tooltip(m) == "This mercenary's upkeep cost is 0.");
    m.state = kMercDead;
    CHECK(merc_upkeep_tooltip(m) == "Your mercenary upkeep timer will start after you revive your mercenary.");
}

TEST_CASE("mercenary: description texts and the owned list") {
    const std::string d = "Race: Guktan<br>Type: Tank<br>Confidence: Low<br>Proficiency: Apprentice, Tier I<br><br>Description: x";
    CHECK(merc_description_summary(d) == "Race: Guktan Type: Tank Confidence: Low Proficiency: Apprentice, Tier I");
    CHECK(merc_race_and_type(d) == "Guktan - Tank");
    MercManager m;
    m.owned.resize(1);
    m.slot_count = 2;
    m.current_index = 0;
    auto rows = merc_owned_rows(m, [&](std::uint32_t) { return d; });
    REQUIRE(rows.size() == 6);
    CHECK(rows[0].marker == "X");
    CHECK(rows[0].text == "Guktan - Tank");
    CHECK(rows[0].data == 1);
    CHECK(rows[1].text == "Empty");
    CHECK(rows[1].tooltip == "Empty Mercenary Slot");
    CHECK(rows[2].text == "Locked");
    CHECK(rows[2].data == -1);
}

TEST_CASE("mercenary: more than ten stance pairs stay inside the record") {
    std::vector<std::uint8_t> p;
    auto u32 = [&](std::uint32_t v) {
        for (int k = 0; k < 4; ++k) p.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    for (int k = 0; k < 9; ++k) u32(static_cast<std::uint32_t>(k + 1)); // id .. alt currency
    p.push_back(0);
    u32(1000); // time left
    u32(3);    // command
    u32(2);    // stance
    u32(12);   // stance count: two more than the record holds
    u32(0);
    p.push_back(1);
    for (char c : std::string("Lerwa")) p.push_back(static_cast<std::uint8_t>(c));
    p.push_back(0);
    for (int k = 0; k < 12; ++k) {
        u32(static_cast<std::uint32_t>(k));
        u32(static_cast<std::uint32_t>(k + 100));
    }
    u32(0xabcd); // what follows the entry
    std::vector<std::uint8_t> payload;
    auto append32 = [&](std::uint32_t v) {
        for (int b = 0; b < 4; ++b) payload.push_back(static_cast<std::uint8_t>(v >> (8 * b)));
    };
    append32(0); // current owned index
    append32(1); // one record
    payload.insert(payload.end(), p.begin(), p.end());
    auto notice = decoded_notice(rof2::Op::MercenaryDataUpdate, payload);
    REQUIRE(notice);
    REQUIRE(notice->mercenaries.size() == 1);
    const auto& r = notice->mercenaries.front();
    CHECK(r.stance_count == 12);
    CHECK(r.stances.size() == kMercMaxStances);
    CHECK(r.required_membership == 6);
    CHECK(r.name == "Lerwa");
    CHECK(notice->slot_count == 0xabcd); // following field was not consumed as another stance
}

TEST_CASE("mercenary: the timer's has-mercenary flag is the low byte") {
    struct Quiet : MercHooks {
        std::vector<std::uint16_t> sent;
        void chat(const std::string&, std::int32_t) override {}
        void event(std::uint32_t, std::int32_t) override {}
        void send(const MercenaryRequest& command) override {
            auto packet = rof2::encode_mercenary_request(command);
            if (packet) sent.push_back(packet->opcode);
        }
        void send_stance(std::uint32_t command, std::uint32_t stance) override {
            MercenaryRequest request;
            request.action = MercenaryAction::SetStance;
            request.command = command;
            request.stance = stance;
            send(request);
        }
        void debug_log(const std::string&) override {}
        bool find_window(const char*) override { return false; }
        void hide_window(const char*) override {}
        void store_window_visibility(const char*) override {}
        void report_upkeep_charged(std::uint8_t) override {}
        void report_upkeep_waived() override {}
        std::uint32_t tick_count() override { return 0; }
        std::uint32_t fast_time() override { return 0; }
        void confirm(std::uint32_t, std::int32_t, std::int32_t) override {}
    } h;
    MercManager m;
    std::uint8_t p[20] = {0x00, 0x01, 0, 0}; // entity id 0x100: low byte 0
    p[12] = 5;
    auto notice = decoded_notice(rof2::Op::MercenaryTimer, {p, p + sizeof(p)});
    REQUIRE(notice);
    merc_handle_timer(m, *notice, h);
    CHECK(m.has_mercenary == 0);
    CHECK(h.sent.empty());
    p[0] = 0x01;
    notice = decoded_notice(rof2::Op::MercenaryTimer, {p, p + sizeof(p)});
    REQUIRE(notice);
    merc_handle_timer(m, *notice, h);
    CHECK(m.has_mercenary == 1);
    CHECK(h.sent == std::vector<std::uint16_t>{rof2::to_wire(rof2::Op::MercenaryDataUpdateRequest)});
}

TEST_CASE("mercenary lists: a huge count over a short tail stops where the bytes do") {
    auto u32 = [](std::vector<std::uint8_t>& b, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    {
        // OP_MercenaryDataResponse FF FF FF FF 00: 2^32 - 1 types and one byte. It used to push a zero per count.
        MercManager m;
        Recorder h;
        std::vector<std::uint8_t> d = {0xff, 0xff, 0xff, 0xff, 0x00};
        auto notice = decoded_notice(rof2::Op::MercenaryDataResponse, d);
        REQUIRE(notice);
        merc_handle_data_response(m, *notice, h);
        CHECK(m.merc_types.empty());
        // Two whole types, then a short tail: both kept.
        std::vector<std::uint8_t> two;
        u32(two, 0xffffffffu);
        u32(two, 7);
        u32(two, 9);
        two.push_back(1);
        auto notice2 = decoded_notice(rof2::Op::MercenaryDataResponse, two);
        REQUIRE(notice2);
        merc_handle_data_response(m, *notice2, h);
        CHECK(m.merc_types == std::vector<std::uint32_t>{7, 9});
    }
    {
        // OP_MercenaryDataUpdate: one record with 2^32 - 1 stance pairs, two of them there, then one byte. The loop
        // used to spin through the whole count.
        MercManager m;
        Recorder h;
        std::vector<std::uint8_t> d;
        u32(d, 0);        // current index
        u32(d, 1);        // one record
        for (int i = 0; i < 9; ++i) u32(d, 0);
        d.push_back(0);   // +0x20
        u32(d, 0);        // time left
        u32(d, 0);        // command
        u32(d, 0);        // stance
        u32(d, 0xffffffffu); // stance count
        u32(d, 0);        // active since
        d.push_back(0);   // assist mode
        d.push_back(0);   // empty name
        u32(d, 1); u32(d, 11); // pair 0
        u32(d, 2); u32(d, 12); // pair 1
        d.push_back(5);
        auto notice = decoded_notice(rof2::Op::MercenaryDataUpdate, d);
        REQUIRE(notice);
        merc_handle_data_update(m, *notice, h);
        REQUIRE(m.owned.size() == 1);
        CHECK(m.owned[0].stance_count == 0xffffffffu);
        REQUIRE(m.owned[0].stances.size() >= 2);
        CHECK(m.owned[0].stances[0].stance == 11);
        CHECK(m.owned[0].stances[1].stance == 12);
    }
}
