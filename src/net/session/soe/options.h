#pragma once

// Parameters of the SOE/Daybreak session flavor, not of all sessions.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace hailstone::net::soe {

struct Options {
    std::chrono::milliseconds connect_timeout{10000};
    std::chrono::milliseconds request_interval{1000}; // Params 0x21; UCS uses 5000 ms
    // No packet from the server for this long counts as link death. The original's no-data timeout is UdpManager
    // Params 0x12 (manager +0x58 → UdpConnection +0x15c @0x8c4197, checked in UdpConnection_InternalGiveTime
    // @0x8c633c..0x8c6398 against the time since the last receive, +0x1d0: that long or longer ends the link with
    // reason 2, Timeout): NetMgr +0x50 = 180 s for world and zone (NetMgr_Ctor @0x462a8f, NetMgr_CreateUdpManager
    // @0x4620f1), the default here. Zero disables this timer, as in the UCS profile; login keeps its safety limit.
    std::chrono::milliseconds idle_timeout{180000};
    // A KeepAlive (00 06) goes out once nothing has been sent for this long (0: never). The original's is Params 0xe
    // (manager +0x48 → UdpConnection +0x230; InternalGiveTime @0x8c61c4 sends the 2 bytes encoded, at once, when the
    // time since the last datagram sent, +0x1c8, reaches it): 15 s for world and zone, 14.8 s for login, 44 s for
    // the chat server (14.8 s with [Defaults] ChatKeepAlive).
    std::chrono::milliseconds keepalive_interval{15000};
    std::uint32_t max_packet_size = 512;
    // Bytes per application opcode: 2 (login, world, zone) or 1 (the chat server).
    int opcode_bytes = 2;
    // The multi-buffer hold (UdpManager Params 0x15): protocol packets of at
    // most 255 bytes, acks included, are combined into one datagram that is
    // sent once its oldest packet is this old, or at once when it is full.
    // 150 ms for world and zone (NetMgr_CreateUdpManager), 0 for the login
    // server (eqmain.dll's UdpManagers).
    std::chrono::milliseconds hold_time{150};
    // The service thread's sleep between passes (UdpManagerThread, Params
    // +0x88 = 20 ms); 0 runs no thread, update() alone services the session.
    std::chrono::milliseconds service_interval{20};
    // SessionStatRequest interval (Params 0x1a): 30 s for world and zone, 0 (never) for login.
    std::chrono::milliseconds stat_interval{30000};
    // The reliable channel's resend adjust (Params 0x31; T = min(1.25 rtt + adjust, 8000) ms): 300, or 2000
    // for the login server (eqmain's LoginServerAPI::CreateUdpManager).
    std::int32_t resend_delay_adjust = 300;
    // Reliable packets in flight (Params 0x2c): 400, or 100 for the login server.
    std::int32_t window_slots = 400;
    std::int32_t window_bytes = 100000; // Params 0x2b; UCS uses 10000
    std::int32_t reliable_timeout_ms = 180000; // Params 0x13; UCS uses 120000
    // The most the received application packets waiting for receive() may hold (their data plus a packet's own
    // size each). The service thread goes on receiving while the owner is busy, so a stalled owner or a server that
    // floods would otherwise grow the queue without bound. Dropping a reliable packet would leave the game state out
    // of step with the server, so passing the limit ends the session. A zone-in, the largest burst a stock server
    // sends, is a few MiB; 64 MiB is eight of the largest fragmented payloads (soe::kMaxFragmentedPayload). Not the
    // original's (it has no such queue); docs/protocol/session.md.
    std::size_t max_received_bytes = 64u << 20;
    // Tests: the session's clock (connect and idle timeouts, the hold, resends, stat requests) instead of the
    // steady clock, so a timing test does not depend on how promptly a loaded machine schedules it. The service
    // thread still wakes every service_interval of real time.
    std::function<std::chrono::steady_clock::time_point()> clock;
    // Optional packet trace for debugging tools: every protocol packet as
    // sent (before the encode passes and CRC) and as received (after them).
    std::function<void(bool sent, std::span<const std::byte> packet)> trace;
};

} // namespace hailstone::net::soe
