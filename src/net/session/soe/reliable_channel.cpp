#include "net/session/soe/reliable_channel.h"

#include "core/wrap_math.h"

// Follows the original's UdpReliableChannel::GiveTime and the
// acknowledgement half of its send path; offsets in the comments are
// the original channel's.

namespace hailstone::net {

namespace {

constexpr std::int32_t kIdleWait = 600000; // nothing due: 10 minutes
constexpr std::size_t kListSize = 1000;    // the original's on-stack resend list

std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
std::int32_t sub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
std::int32_t min32(std::int32_t a, std::int32_t b) { return a < b ? a : b; }

} // namespace

std::int32_t reliable_elapsed(std::int64_t sa, std::int64_t sb) {
    auto a = static_cast<std::uint64_t>(sa), b = static_cast<std::uint64_t>(sb);
    auto alo = static_cast<std::uint32_t>(a), ahi = static_cast<std::uint32_t>(a >> 32);
    auto blo = static_cast<std::uint32_t>(b), bhi = static_cast<std::uint32_t>(b >> 32);
    std::uint32_t lo = alo - blo;
    std::uint32_t hi = ahi - bhi - (alo < blo ? 1u : 0u);
    if (hi >> 31) return static_cast<std::int32_t>(lo);
    bool overflow = ((ahi ^ bhi) & (ahi ^ hi)) >> 31;
    if (hi != 0 && !overflow) return 0x7fffffff;
    return lo > 0x7fffffff ? 0x7fffffff : static_cast<std::int32_t>(lo);
}

std::int32_t reliable_give_time(ReliableChannel& ch, std::int64_t now, const ReliableChannelHooks& hooks) {
    if (now < ch.wake_time) return reliable_elapsed(ch.wake_time, now);
    if (ch.trickle_rate > 0) {
        std::int32_t wait = sub(ch.trickle_rate, reliable_elapsed(now, ch.last_send_time));
        if (wait > 0) return wait;
    }
    std::int32_t timeout = add(
        core::wrapped_div(static_cast<std::uint32_t>(ch.resend_delay_percent) * static_cast<std::uint32_t>(ch.rtt), 100),
        ch.resend_delay_adjust);
    if (timeout >= ch.resend_delay_cap) timeout = ch.resend_delay_cap;
    ch.window_full = false;
    std::int32_t next = kIdleWait;
    const auto window = static_cast<std::int64_t>(ch.slots.size());

    if (!(ch.oldest_unacked_seq < ch.next_seq) && ch.queued_packets == 0 && !ch.coalescing) {
        ch.congestion_window = ch.start_window;
    } else {
        auto cutoff = static_cast<std::int64_t>(static_cast<std::uint64_t>(now) -
                                                static_cast<std::uint64_t>(static_cast<std::int64_t>(timeout)));
        if (cutoff < ch.last_acked_first_send) cutoff = ch.last_acked_first_send;
        std::vector<std::int64_t>& list = ch.resend_list; // the sequences of the listed slots
        for (;;) {
            std::int32_t limit = min32(ch.congestion_window, ch.max_outstanding_bytes);
            bool overflow = false;
            std::int32_t remaining = limit;
            std::int32_t in_flight = 0;
            list.clear();
            for (std::int64_t sequence = ch.oldest_unacked_seq; sequence <= ch.next_seq; ++sequence) {
                if (sequence == ch.next_seq && !hooks.fill(ch, remaining)) break;
                ReliableSlot& s = ch.slot(sequence);
                if (!s.used) continue;
                remaining = sub(remaining, s.length);
                if (s.last_send < cutoff) {
                    if (list.size() < kListSize) list.push_back(sequence);
                    else overflow = true;
                } else {
                    in_flight = add(in_flight, s.length);
                    next = min32(sub(timeout, reliable_elapsed(now, s.last_send)), next);
                }
                if (s.first_send == 0 && remaining <= 0) break;
            }
            bool congested = reliable_elapsed(now, ch.last_congestion_time) > ch.rtt;
            std::int32_t fast = 0;
            std::int32_t sent = 0;
            for (std::size_t at = 0; at < list.size();) {
                if (in_flight >= limit) break;
                std::int64_t sequence = list[at++];
                ReliableSlot& s = ch.slot(sequence);
                std::int32_t total = hooks.packet_length(s.packet);
                bool fragment = s.offset != 0 || s.length != total;
                // The original recovers the sequence from the slot's place in the ring.
                std::int64_t oldest_index = ch.oldest_unacked_seq % window;
                std::int64_t index = sequence % window;
                std::uint32_t wire = static_cast<std::uint32_t>(index >= oldest_index ? index - oldest_index
                                                                                      : window - oldest_index + index);
                wire += static_cast<std::uint32_t>(ch.oldest_unacked_seq);
                std::uint8_t header[8];
                header[0] = 0;
                header[1] = static_cast<std::uint8_t>((fragment ? 0xd : 9) + ch.channel);
                header[2] = static_cast<std::uint8_t>(wire >> 8);
                header[3] = static_cast<std::uint8_t>(wire);
                std::size_t header_size = 4;
                if (fragment && s.offset == 0) {
                    auto t = static_cast<std::uint32_t>(total);
                    header[4] = static_cast<std::uint8_t>(t >> 24);
                    header[5] = static_cast<std::uint8_t>(t >> 16);
                    header[6] = static_cast<std::uint8_t>(t >> 8);
                    header[7] = static_cast<std::uint8_t>(t);
                    header_size = 8;
                }
                hooks.send(std::span<const std::uint8_t>(header, header_size), s);
                if (s.first_send == 0) {
                    s.first_send = now;
                } else {
                    std::int32_t age_limit = hooks.has_manager ? hooks.max_unacked_age : 0;
                    if (age_limit > 0 && reliable_elapsed(now, s.first_send) > age_limit) {
                        hooks.disconnect(8);
                        return 0;
                    }
                    if (s.last_send < ch.last_acked_first_send) {
                        if (congested && fast > ch.loss_tolerance) {
                            ch.last_congestion_time = now;
                            std::int32_t w = core::wrapped_div(static_cast<std::uint32_t>(ch.congestion_window) * 3, 4);
                            congested = false;
                            if (ch.min_window >= w) w = ch.min_window;
                            ch.congestion_window = w;
                            ch.threshold = w;
                            limit = min32(w, ch.max_outstanding_bytes);
                        }
                        ch.fast_resends = add(ch.fast_resends, 1);
                        fast = add(fast, 1);
                        ++ch.connection_fast_resends;
                        if (hooks.has_manager) ++ch.manager_fast_resends;
                    } else {
                        if (congested) {
                            ch.last_congestion_time = now;
                            std::int32_t half = ch.congestion_window / 2;
                            auto twice = static_cast<std::int32_t>(static_cast<std::uint32_t>(ch.fragment_size) * 2);
                            congested = false;
                            ch.threshold = twice < half ? half : twice;
                            ch.congestion_window = ch.start_window;
                            limit = min32(ch.congestion_window, ch.max_outstanding_bytes);
                            ch.rtt = add(ch.rtt, 100);
                            if (ch.rtt >= ch.resend_delay_cap) ch.rtt = ch.resend_delay_cap;
                        }
                        ch.timeout_resends = add(ch.timeout_resends, 1);
                        ++ch.connection_timeout_resends;
                        if (hooks.has_manager) ++ch.manager_timeout_resends;
                    }
                }
                s.send_count = add(s.send_count, 1);
                s.last_send = now;
                next = min32(timeout, next);
                in_flight = add(in_flight, s.length);
                ch.last_send_time = now;
                sent = add(sent, s.length);
                if (ch.max_bytes_per_call != 0 && sent >= ch.max_bytes_per_call) break;
            }
            if (in_flight >= limit) ch.window_full = true;
            if (!overflow || ch.window_full) break;
        }
    }
    std::int32_t wait = sub(ch.trickle_rate, reliable_elapsed(now, ch.last_send_time));
    if (wait < next) wait = next;
    if (wait <= 0) wait = 0;
    ch.wake_time = static_cast<std::int64_t>(static_cast<std::uint64_t>(now) + static_cast<std::uint64_t>(static_cast<std::int64_t>(wait)));
    return wait;
}

void reliable_ack_one(ReliableChannel& ch, std::int64_t sequence, std::int64_t now) {
    if (sequence < ch.oldest_unacked_seq || sequence >= ch.next_seq) return;
    ReliableSlot& s = ch.slot(sequence);
    if (!s.used) return;
    // Deliberate difference: the original (AckOne @0x8ccca0) takes an ack for a slot that holds a packet but has not
    // gone out yet (first_send 0), and its round trip from time 0 clamps to 0x7fffffff ms and wrecks the rtt. A server
    // acknowledges only what it received, so a stock one never sends such an ack (EQEmu's peer likewise ignores an ack
    // for a sequence not in its sent list, ReliableStreamConnection::Ack); it is ignored, and the packet is sent and
    // acknowledged as usual.
    if (s.first_send == 0) return;
    ch.wake_time = 0;
    if (ch.window_full) {
        if (ch.congestion_window < ch.threshold) {
            ch.congestion_window = ch.fragment_size + ch.congestion_window;
        } else {
            auto step = core::wrapped_mul_div(ch.fragment_size, ch.fragment_size, ch.congestion_window);
            ch.congestion_window = (step > 1 ? step : 1) + ch.congestion_window;
        }
    }
    if (s.last_send == s.first_send) {
        std::int32_t elapsed = reliable_elapsed(now, s.first_send);
        ch.rtt = core::wrapped_div(static_cast<std::uint32_t>(elapsed) + static_cast<std::uint32_t>(ch.rtt) * 3, 4);
    }
    ch.last_acked_first_send = s.first_send;
    s = ReliableSlot{};
    while (ch.oldest_unacked_seq < ch.next_seq && !ch.slot(ch.oldest_unacked_seq).used) ++ch.oldest_unacked_seq;
}

std::int64_t reliable_widen(const ReliableChannel& ch, std::uint16_t sequence) {
    auto ack = static_cast<std::int64_t>((static_cast<std::uint64_t>(ch.next_seq) & 0xffffffffffff0000ull) | sequence);
    if (ack > ch.next_seq) ack -= 0x10000;
    return ack;
}

void reliable_ack_all(ReliableChannel& ch, std::uint16_t sequence, std::int64_t now) {
    std::int64_t ack = reliable_widen(ch, sequence);
    if (ch.oldest_unacked_seq > ack) {
        ch.rtt += 400;
        if (ch.rtt >= ch.resend_delay_cap) ch.rtt = ch.resend_delay_cap;
        return;
    }
    for (std::int64_t s = ch.oldest_unacked_seq; s <= ack; ++s) reliable_ack_one(ch, s, now);
}

} // namespace hailstone::net
