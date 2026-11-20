#pragma once

// Transport/session independent contract. A session carries opaque application
// packets; it does not know GameOpcode or the game's typed values. The
// implementation supplies handshake, reliable channels and datagram framing.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "net/wire/app_packet.h"

namespace hailstone::net {

enum class SessionState : std::uint8_t {
    Connecting,   // SessionRequest sent, waiting for SessionResponse
    Connected,
    Disconnecting,
    Disconnected, // closed by either side, or timed out; see ISession::error()
};

// The session's traffic since it connected (the performance window; the same counters as the closing report).
struct SessionStats {
    std::uint64_t datagrams_sent = 0, datagrams_received = 0;
    std::uint64_t bytes_sent = 0, bytes_received = 0; // datagrams on the wire (after the encode passes and CRC)
    std::uint64_t acks_sent = 0, duplicates = 0, early = 0, bad = 0;
    std::uint64_t keepalives_sent = 0;
    std::int64_t timeout_resends = 0, fast_resends = 0;
    std::int32_t rtt_ms = 0;
    std::int32_t queued_packets = 0;  // reliable packets waiting for a slot
    std::int32_t oldest_unacked_ms = 0; // age since its first send; zero when no sent packet remains
    std::int64_t longest_stall_ms = 0; // the longest gap between the owner's update() calls
    // The SessionStatRequest round trips (the server's SessionStatResponse echoes the request's 16-bit time): the
    // last, the average, the lowest and the highest; 0 before the first reply. /netstats (Cmd_netstats) shows them
    // as UdpConnection::GetStats' ping times.
    std::int32_t ping_last = 0, ping_average = 0, ping_low = 0, ping_high = 0;
    std::int32_t ping_count = 0;
    // At the last reply: our datagram counts then, and the server's own as it carried them.
    std::uint64_t sync_our_sent = 0, sync_our_received = 0, sync_their_sent = 0, sync_their_received = 0;
};

class ISession {
public:
    virtual ~ISession() = default;

    // Starts connecting; host may be a name or an IPv4/IPv6 address.
    virtual void connect(const std::string& host, std::uint16_t port) = 0;
    virtual void update() = 0;

    virtual SessionState state() const = 0;
    virtual std::string error() const = 0;

    // Queues a packet; reliable packets are sequenced, acked and resent,
    // fragmented when larger than one datagram, and may be combined.
    // True means admitted to this session's send queue (or dispatched to
    // its transport), not acknowledged or accepted by the remote server.
    virtual bool send(const AppPacket& packet, bool reliable = true, bool allow_coalescing = true) = 0;
    virtual std::optional<AppPacket> receive() = 0;

    virtual void disconnect() = 0;

    virtual SessionStats stats() const { return {}; }

    // A bulk download (game/server_file_sync.h): the small packets out, acks and requests, go at the next service pass
    // instead of waiting out the multi-buffer's hold_time, so the server's pieces are asked for and acknowledged
    // at once. Off: the original's hold.
    virtual void set_no_hold(bool) {}

    // Testing (app/input_script.h's droplink): from now on nothing goes out or comes in, as when the network goes
    // away, so the link dies by the idle timeout as a real link death would.
    virtual void simulate_link_loss() {}
    // Testing: force a socket failure on the next service pass.
    virtual void simulate_socket_loss() {}
};

} // namespace hailstone::net
