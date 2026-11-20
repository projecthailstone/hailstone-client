#pragma once

// The server end of one SOE ("daybreak") session on loopback UDP, for the offline integration tests. It is the
// peer of net::Session: it answers the SessionRequest, decodes datagrams with the client's own codec
// (net/session/soe/framing.h), acknowledges what it receives the way EQEmu does (docs/protocol/session.md, "Receiving":
// a cumulative Ack for an in-order packet, an OutOfOrderAck for an early one, the cumulative Ack again for a
// duplicate), and sends reliable and unreliable application packets.
//
// It is single threaded and does nothing by itself: the test calls poll() (usually through MockEqServer::poll()).
// Packet loss is scripted, never random: the fate of the next datagrams is queued with script_outgoing() and
// script_incoming(), so every run behaves the same. Resends by the server are explicit too (resend_unacked()).

#include <array>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "net/wire/app_packet.h"
#include "net/session/soe/framing.h"
#include "platform/udp_socket.h"

namespace hailstone::test {

namespace soe = ::hailstone::net::soe;

// What the network does with a datagram.
enum class Fate : std::uint8_t {
    Deliver,
    Drop,      // never arrives
    Duplicate, // arrives twice
    Hold,      // kept back until release_held()
};

// One acknowledgement received from the client, in arrival order.
struct AckEvent {
    enum class Kind : std::uint8_t { Ack, OutOfOrder } kind = Kind::Ack;
    std::uint16_t sequence = 0;
};

class SoeEndpoint {
public:
    // EQEmu's negotiation: 2 CRC bytes, no encode passes, 512 bytes (login and world). The zone server may add
    // compression ("Network:CompressZoneStream"): set passes = {Compression, Xor} to cover both passes.
    static soe::SessionParams default_params();

    explicit SoeEndpoint(soe::SessionParams params = default_params(), int opcode_bytes = 2);

    std::uint16_t port() const { return socket_.local_endpoint()->port; }
    // Receives and handles every datagram that is waiting.
    void poll();

    // ---- the session ----
    bool connected() const { return connected_; }
    // SessionRequests accepted so far (a new one starts a new session and forgets the old state).
    int sessions() const { return sessions_; }
    // The client sent SessionDisconnect for the current session.
    bool client_disconnected() const { return client_disconnected_; }
    std::uint32_t requested_max_packet() const { return requested_max_packet_; }
    const soe::SessionParams& params() const { return params_; }
    // Applies to sessions started after the call.
    void set_params(const soe::SessionParams& params) { params_ = params; }

    // ---- from the client ----
    // Application packets in delivery order: reliable ones once in order and complete, unreliable ones as they come.
    std::deque<net::AppPacket> inbox;
    std::optional<net::AppPacket> take();

    // Times each client reliable sequence arrived (a resend counts again), counted before the fate below.
    std::map<std::uint16_t, int> reliable_arrivals;
    // Scripted fate of the client's next datagrams that carry a reliable packet (acks alone do not use it up).
    void script_incoming(std::initializer_list<Fate> fates) { incoming_script_.insert(incoming_script_.end(), fates); }

    // ---- to the client ----
    // Sends one application payload as OP_Packet or OP_Fragment datagrams; returns the first sequence used.
    std::uint16_t send_reliable(const net::AppPacket& packet);
    // Several small reliable packets in one OP_Combined datagram (each must fit one OP_Packet and be < 256 bytes).
    void send_reliable_combined(const std::vector<net::AppPacket>& packets);
    void send_unreliable(const net::AppPacket& packet);
    void send_keepalive();
    void send_disconnect();     // SessionDisconnect
    void send_out_of_session(); // OP_OutOfSession
    // A datagram exactly as given: no encode passes, no CRC (garbage, or a packet built by hand).
    void send_datagram(const soe::Bytes& datagram) { send_raw(datagram); }

    // Scripted fate of the next datagrams carrying application data (reliable pieces, combined, unreliable).
    // Fragments are datagrams too: one entry each.
    void script_outgoing(std::initializer_list<Fate> fates) { outgoing_script_.insert(outgoing_script_.end(), fates); }
    // The next n acknowledgements the server would send are lost.
    void drop_next_acks(int n) { acks_to_drop_ += n; }
    // Releases what Fate::Hold kept back, in the order it was sent or reversed.
    void release_held(bool reverse = false);
    std::size_t held() const { return held_.size(); }

    // Sends every reliable packet the client has not acknowledged yet, as EQEmu does on any ack. Returns how many.
    std::size_t resend_unacked();
    std::size_t unacked() const;

    // ---- the client's acknowledgements of our reliable packets ----
    std::vector<AckEvent> ack_log;
    // The highest cumulative Ack seen (the sequence it names), if any.
    std::optional<std::uint16_t> last_cumulative_ack() const { return last_ack_; }
    bool is_acked(std::uint16_t sequence) const;

    struct Stats {
        int datagrams_in = 0;
        int keepalives_in = 0;   // 00 06 from the client
        int datagrams_out = 0;
        int bad_datagrams = 0;   // failed CRC or decode
        int dropped_in = 0;      // by script_incoming
        int duplicates_in = 0;   // reliable packets that arrived again (past sequence)
        int early_in = 0;        // reliable packets that arrived ahead of the expected one
        int stat_requests = 0;
        int resent = 0;
    } stats;

    // The reliable sequence the next send_reliable() will use.
    std::uint16_t next_sequence() const { return next_out_; }

private:
    void on_datagram(soe::ByteSpan datagram, const platform::Endpoint& from);
    void start_session(soe::ByteSpan request, const platform::Endpoint& from);
    void process(soe::ByteSpan packet, int depth);
    bool carries_reliable(soe::ByteSpan packet) const;
    void send_ack(std::uint8_t opcode, std::uint16_t sequence);
    // Applies the encode passes and sends (or not, per fate).
    void transmit(const soe::Bytes& packet, Fate fate);
    void send_raw(const soe::Bytes& datagram);
    Fate next_outgoing_fate();

    platform::UdpSocket socket_;
    platform::Endpoint client_;
    soe::SessionParams params_;
    int opcode_bytes_;

    bool connected_ = false;
    bool client_disconnected_ = false;
    int sessions_ = 0;
    std::uint32_t connect_code_ = 0;
    std::uint32_t requested_max_packet_ = 0;

    soe::InboundStream inbound_;
    std::uint16_t next_out_ = 0;
    struct Sent {
        soe::Bytes packet; // the protocol packet before encoding
        bool acked = false;
    };
    std::map<std::uint16_t, Sent> sent_;
    std::optional<std::uint16_t> last_ack_;

    std::deque<Fate> outgoing_script_, incoming_script_;
    int acks_to_drop_ = 0;
    std::vector<soe::Bytes> held_;
};

} // namespace hailstone::test
