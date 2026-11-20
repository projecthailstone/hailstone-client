// UdpReliableChannel_GiveTime.jsonl (recorded from eqgame.exe)
// against net/reliable_channel: the resend timeout, which slots are (re)sent
// with which header, the window, the counters and the wake time.
#include <doctest/doctest.h>

#include <format>
#include <string>
#include <vector>

#include "net/session/soe/reliable_channel.h"
#include "vector_files.h"

using namespace hailstone::net;
using hailstone::test::Json;

namespace {

std::string check_give_time(const Json& rec) {
    const Json& in = rec["in"];
    const Json& out = rec["out"];
    ReliableChannel ch;
    ch.max_outstanding_bytes = in["max_outstanding_bytes"].i32();
    ch.max_bytes_per_call = in["max_bytes_per_call"].i32();
    ch.trickle_rate = in["trickle_rate"].i32();
    ch.resend_delay_adjust = in["resend_delay_adjust"].i32();
    ch.resend_delay_percent = in["resend_delay_percent"].i32();
    ch.resend_delay_cap = in["resend_delay_cap"].i32();
    ch.loss_tolerance = in["loss_tolerance"].i32();
    ch.channel = static_cast<std::uint8_t>(in["channel"].i32());
    ch.rtt = in["rtt"].i32();
    ch.fragment_size = in["fragment_size"].i32();
    ch.queued_packets = in["queued_packets"].i32();
    ch.coalescing = in["coalescing"].truthy();
    ch.start_window = in["start_window"].i32();
    ch.congestion_window = in["congestion_window"].i32();
    ch.threshold = in["threshold"].i32();
    ch.min_window = in["min_window"].i32();
    ch.oldest_unacked_seq = in["oldest_unacked_seq"].i64();
    ch.next_seq = in["next_seq"].i64();
    ch.wake_time = in["wake_time"].i64();
    ch.last_send_time = in["last_send_time"].i64();
    ch.last_congestion_time = in["last_congestion_time"].i64();
    ch.last_acked_first_send = in["last_acked_first_send"].i64();
    ch.fast_resends = in["fast_resends"].i32();
    ch.timeout_resends = in["timeout_resends"].i32();
    ch.connection_fast_resends = in["connection_fast_resends"].i64();
    ch.connection_timeout_resends = in["connection_timeout_resends"].i64();
    bool manager = in["manager"].truthy();
    if (manager) {
        ch.manager_fast_resends = in["manager_fast_resends"].i64();
        ch.manager_timeout_resends = in["manager_timeout_resends"].i64();
    }
    const int window = in["window_slots"].i32();
    ch.slots.resize(static_cast<std::size_t>(window));
    for (int k = 0; k < window; ++k) {
        const Json& s = in["slots"][static_cast<std::size_t>(k)];
        if (s.is_null()) continue;
        auto& d = ch.slots[static_cast<std::size_t>(k)];
        d.used = true;
        d.first_send = s["first_send"].i64();
        d.last_send = s["last_send"].i64();
        d.send_count = s["send_count"].i32();
        d.packet = s["packet"].i32();
        d.offset = s["offset"].i32();
        d.length = s["length"].i32();
    }
    std::int32_t packets[4];
    for (std::size_t n = 0; n < 4; ++n) packets[n] = in["packets"][n].i32();
    std::int32_t fills_left = in["fills_left"].i32();

    // The calls, as the recorder's notes list them.
    std::vector<std::string> notes;
    ReliableChannelHooks hooks;
    hooks.has_manager = manager;
    hooks.max_unacked_age = in["max_unacked_age"].i32();
    hooks.fill = [&](ReliableChannel& c, std::int32_t bytes) {
        bool fills = fills_left != 0 &&
                     static_cast<std::uint64_t>(c.next_seq - c.oldest_unacked_seq) < static_cast<std::uint64_t>(window);
        notes.push_back(std::format("fill_physical_slots {} {}", bytes, fills ? 1 : 0));
        if (!fills) return false;
        --fills_left;
        auto n = static_cast<std::int32_t>(static_cast<std::uint32_t>(fills_left) & 3);
        ReliableSlot& s = c.slot(c.next_seq);
        s = ReliableSlot{};
        s.used = true;
        s.packet = n;
        s.length = packets[n];
        ++c.next_seq;
        return true;
    };
    hooks.packet_length = [&](std::int32_t p) { return packets[p & 3]; };
    hooks.send = [&](std::span<const std::uint8_t> h, const ReliableSlot& s) {
        std::uint32_t word = std::uint32_t(h[0]) << 24 | std::uint32_t(h[1]) << 16 | std::uint32_t(h[2]) << 8 | h[3];
        notes.push_back(std::format("send {} {} {} {}", static_cast<std::int32_t>(word), s.packet, s.offset, s.length));
        if (h.size() == 8) {
            std::uint32_t t = std::uint32_t(h[4]) << 24 | std::uint32_t(h[5]) << 16 | std::uint32_t(h[6]) << 8 | h[7];
            notes.push_back(std::format("send_total_length {}", static_cast<std::int32_t>(t)));
        }
    };
    hooks.disconnect = [&](std::uint32_t reason) { notes.push_back(std::format("disconnect {}", reason)); };

    // The recorder does not write the window-full byte's input; it is only compared when the call got past its two
    // early returns (before the wake time, or within the trickle rate) that leave it alone.
    const std::int64_t now = in["now"].i64();
    bool early = now < ch.wake_time ||
                 (ch.trickle_rate > 0 &&
                  static_cast<std::int32_t>(static_cast<std::uint32_t>(ch.trickle_rate) -
                                            static_cast<std::uint32_t>(reliable_elapsed(now, ch.last_send_time))) > 0);
    std::int32_t r = reliable_give_time(ch, now, hooks);

    std::vector<std::string> want;
    for (const Json& n : out["notes"].items) {
        const std::string& fn = n["fn"].text;
        if (fn == "fill_physical_slots") want.push_back(std::format("fill_physical_slots {} {}", n["bytes"].i32(), n["ret"].i32() != 0 ? 1 : 0));
        else if (fn == "send")
            want.push_back(std::format("send {} {} {} {}", n["header"].i32(), n["packet"].i32(), n["offset"].i32(), n["size"].i32()));
        else if (fn == "send_total_length") want.push_back(std::format("send_total_length {}", n["value"].i32()));
        else if (fn == "disconnect") want.push_back(std::format("disconnect {}", n["reason"].i32()));
        else want.push_back(fn);
    }
    if (notes != want) {
        std::string got, exp;
        for (auto& s : notes) got += s + "; ";
        for (auto& s : want) exp += s + "; ";
        return "calls: got [" + got + "] want [" + exp + "]";
    }
    if (r != out["value"].i32()) return std::format("value {} want {}", r, out["value"].i32());
#define CHK(field, expr, json)                                                                                         \
    if ((expr) != out[json].i64()) return std::format(field " {} want {}", static_cast<std::int64_t>(expr), out[json].i64());
    CHK("wake_time", ch.wake_time, "wake_time")
    CHK("rtt", ch.rtt, "rtt")
    CHK("congestion_window", ch.congestion_window, "congestion_window")
    CHK("threshold", ch.threshold, "threshold")
    if (!early) CHK("window_full", ch.window_full ? 1 : 0, "window_full")
    CHK("last_send_time", ch.last_send_time, "last_send_time")
    CHK("last_congestion_time", ch.last_congestion_time, "last_congestion_time")
    CHK("next_seq", ch.next_seq, "next_seq")
    CHK("fast_resends", ch.fast_resends, "fast_resends")
    CHK("timeout_resends", ch.timeout_resends, "timeout_resends")
    CHK("connection_fast_resends", ch.connection_fast_resends, "connection_fast_resends")
    CHK("connection_timeout_resends", ch.connection_timeout_resends, "connection_timeout_resends")
    if (manager) {
        CHK("manager_fast_resends", ch.manager_fast_resends, "manager_fast_resends")
        CHK("manager_timeout_resends", ch.manager_timeout_resends, "manager_timeout_resends")
    }
#undef CHK
    for (int k = 0; k < window; ++k) {
        const Json& s = out["slots"][static_cast<std::size_t>(k)];
        const auto& d = ch.slots[static_cast<std::size_t>(k)];
        if (s.is_null() != !d.used) return std::format("slot {} used {}", k, d.used);
        if (s.is_null()) continue;
        if (d.first_send != s["first_send"].i64() || d.last_send != s["last_send"].i64() ||
            d.send_count != s["send_count"].i32() || d.length != s["length"].i32())
            return std::format("slot {}: first {} last {} count {} len {}; want {} {} {} {}", k, d.first_send, d.last_send,
                               d.send_count, d.length, s["first_send"].i64(), s["last_send"].i64(),
                               s["send_count"].i32(), s["length"].i32());
    }
    return {};
}

} // namespace

TEST_CASE("vectors: UdpReliableChannel_GiveTime (the resend timer)") {
    hailstone::test::each_vector("UdpReliableChannel_GiveTime.jsonl", check_give_time);
}

TEST_CASE("reliable channel: acknowledgements blend the round trip and grow it on duplicates") {
    ReliableChannel ch;
    ch.slots.resize(8);
    ch.next_seq = 3;
    for (std::int64_t s = 0; s < 3; ++s) {
        auto& d = ch.slot(s);
        d.used = true;
        d.first_send = d.last_send = 1000;
        d.length = 10;
    }
    ch.slot(1).last_send = 1500; // sent twice: no blend
    reliable_ack_all(ch, 1, 1200);
    CHECK(ch.rtt == (200 + 3 * 800) / 4); // seq 0 blended, seq 1 not
    CHECK(ch.oldest_unacked_seq == 2);
    CHECK(ch.last_acked_first_send == 1000);
    std::int32_t before = ch.rtt;
    reliable_ack_all(ch, 0, 1300); // duplicate
    CHECK(ch.rtt == before + 400);
    // T = min(1.25 rtt + 300, 8000): the first resend of a fresh channel comes 1300 ms after the send.
    ReliableChannel fresh;
    CHECK(static_cast<std::int32_t>(125u * static_cast<std::uint32_t>(fresh.rtt)) / 100 + fresh.resend_delay_adjust == 1300);
}

TEST_CASE("reliable channel: an ack for a packet that never went out is ignored") {
    // A slot holds a packet that has not been sent (first_send 0). The original would take the ack and blend a round
    // trip from time 0 (0x7fffffff ms) into the rtt; a deliberate difference ignores it.
    ReliableChannel ch;
    ch.slots.resize(8);
    ch.next_seq = 2;
    ch.slot(0) = {.used = true, .first_send = 1000, .last_send = 1000, .length = 10};
    ch.slot(1) = {.used = true, .length = 10};
    reliable_ack_one(ch, 1, 50000);
    CHECK(ch.rtt == 800);
    CHECK(ch.slot(1).used);
    CHECK(ch.oldest_unacked_seq == 0);
    // A cumulative ack over both takes the sent one only.
    reliable_ack_all(ch, 1, 1200);
    CHECK(ch.rtt == (200 + 3 * 800) / 4);
    CHECK_FALSE(ch.slot(0).used);
    CHECK(ch.slot(1).used);
    CHECK(ch.oldest_unacked_seq == 1);
    // Once it has gone out, its ack counts.
    ch.slot(1).first_send = ch.slot(1).last_send = 1300;
    reliable_ack_one(ch, 1, 1400);
    CHECK_FALSE(ch.slot(1).used);
    CHECK(ch.oldest_unacked_seq == 2);
}
