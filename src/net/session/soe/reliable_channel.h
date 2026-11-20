#pragma once

// The original client's reliable channel timer and acknowledgements
// (UdpLibrary's UdpReliableChannel, linked into eqgame.exe):
// its GiveTime and send paths, reimplemented from their observed behaviour and
// checked against UdpReliableChannel_GiveTime.jsonl.
//
// The resend timeout is T = min(percent * rtt / 100 + adjust, cap); the
// client keeps the defaults 125 / 300 / 8000, so T = min(1.25 rtt + 300,
// 8000) ms with rtt starting at 800. Each acknowledged packet sent only once
// blends its round trip in ((elapsed + 3 rtt) / 4); a duplicate ack-all adds
// 400 and a timeout resend 100 (once per rtt), both capped. A slot is sent
// again once its last send is before max(now - T, the first send of the
// newest acknowledged packet). Sends stop at min(congestion window, 100000)
// bytes in flight; a timeout resets the window to its start. A packet first
// sent more than 180 s ago drops the connection (reason 8).
//
// Times are a 64-bit millisecond clock; sequences are 64-bit (the wire
// carries the low 16 bits). All arithmetic wraps as the original's does.

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace hailstone::net {

struct ReliableSlot {
    bool used = false;          // +0x18 data non-null
    std::int64_t first_send = 0; // +0 (0: never sent)
    std::int64_t last_send = 0;  // +8
    std::int32_t send_count = 0; // +0x10
    std::int32_t packet = 0;     // +0x14: the logical packet (an index the owner understands)
    std::int32_t offset = 0;     // +0x18 less the packet's data start
    std::int32_t length = 0;     // +0x1c
};

struct ReliableChannel {
    // config (Params +0xac.., copied by the channel's constructor)
    std::int32_t max_outstanding_bytes = 100000; // +0 (Params 0x2b)
    std::int32_t max_bytes_per_call = 0;         // +0x10
    std::int32_t trickle_rate = 0;               // +0x14
    std::int32_t resend_delay_adjust = 300;      // +0x18
    std::int32_t resend_delay_percent = 125;     // +0x1c
    std::int32_t resend_delay_cap = 8000;        // +0x20
    std::int32_t loss_tolerance = 0;             // +0x2c
    std::uint8_t channel = 0;                    // +0x58

    std::int32_t rtt = 800;               // +0x84
    std::int32_t fragment_size = 0;       // +0x88
    std::int32_t queued_packets = 0;      // +0xa4
    bool coalescing = false;              // +0xcc non-null
    std::int32_t start_window = 0;        // +0xa8
    std::int32_t congestion_window = 0;   // +0xac
    std::int32_t threshold = 0;           // +0xb0
    std::int32_t min_window = 0;          // +0xb4
    bool window_full = false;             // +0xb8
    std::int64_t next_seq = 0;            // +0x60
    std::int64_t oldest_unacked_seq = 0;  // +0x68
    std::int64_t wake_time = 0;           // +0x48
    std::int64_t last_send_time = 0;      // +0x40
    std::int64_t last_congestion_time = 0; // +0x50
    std::int64_t last_acked_first_send = 0; // +0x38
    std::int32_t fast_resends = 0;        // +0xec
    std::int32_t timeout_resends = 0;     // +0xf0
    std::int64_t connection_fast_resends = 0, connection_timeout_resends = 0; // connection +0xd8 / +0xe0
    std::int64_t manager_fast_resends = 0, manager_timeout_resends = 0;       // manager +0x320 / +0x328

    std::vector<ReliableSlot> slots; // the physical ring (config +4 entries)
    // GiveTime's resend list (the original's is on its stack): kept here so a pass every frame allocates nothing.
    // Scratch only; the channel's owner already serialises GiveTime (the session mutex), and no hook re-enters it.
    std::vector<std::int64_t> resend_list;

    ReliableSlot& slot(std::int64_t sequence) {
        return slots[static_cast<std::size_t>(sequence % static_cast<std::int64_t>(slots.size()))];
    }
};

// What GiveTime calls out to.
struct ReliableChannelHooks {
    bool has_manager = true;          // the connection's UdpManager (its clock, age limit and counters)
    std::int32_t max_unacked_age = 180000; // manager +0x5c (Params 0x13)
    // FillPhysicalSlots(bytes): fills the slot of next_seq and advances it; returns whether it did.
    std::function<bool(ReliableChannel&, std::int32_t bytes)> fill;
    // The logical packet's whole length (its virtual 7).
    std::function<std::int32_t(std::int32_t packet)> packet_length;
    // BufferedSend: the 4- or 8-byte header and the slot's data.
    std::function<void(std::span<const std::uint8_t> header, const ReliableSlot& slot)> send;
    // UdpConnection_InternalDisconnect(0, reason).
    std::function<void(std::uint32_t reason)> disconnect;
};

// UdpReliableChannel_GiveTime @0x8cd2b0: returns the ms until it wants to run again.
std::int32_t reliable_give_time(ReliableChannel& ch, std::int64_t now, const ReliableChannelHooks& hooks);

// UdpReliableChannel_AckOne @0x8ccca0: `elapsed` is the round trip of the
// slot's first send (UdpConnection_GetClockElapsed).
void reliable_ack_one(ReliableChannel& ch, std::int64_t sequence, std::int64_t now);
// UdpReliableChannel_AckAll @0x8cdf50: the 16-bit sequence of an 00 15.
void reliable_ack_all(ReliableChannel& ch, std::uint16_t sequence, std::int64_t now);
// The 16-bit wire sequence widened next to next_seq (as AckAll does).
std::int64_t reliable_widen(const ReliableChannel& ch, std::uint16_t sequence);

// The original's clamp of a 64-bit time difference to an int.
std::int32_t reliable_elapsed(std::int64_t a, std::int64_t b);

} // namespace hailstone::net
