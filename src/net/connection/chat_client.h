#pragma once

// The Universal Chat Service (UCS) connection: cross-zone chat channels
// (/join, /leave, /list, /1../10), the friends and ignore lists.
//
// The world server names the chat server in OP_SetChatServer2
// ("host,port,shortname.character,<type><key>"); Handle_OP_SetChatServer2
// @0x4b82b0 splits it and CEverQuest::ChatServerConnect @0x5135b0 opens
// its own SOE session. The
// packets follow EQEmu's UCS (ucs/clientlist.cpp, utils/patches/
// mail_opcodes.conf): one-byte opcodes; OP_MailLogin logs in with the
// mailbox "SOE.EQ.<short>.<name>" and the key; commands go as OP_Mail
// text ("join a, b", "leave a", "list", "buddy x", "ignoreplayer x",
// "<n> text" for channel n).
//
// The original sends slash-command text through the zone (OP_ChannelMessage
// channel 20, EQEmu's UCS relay); this client sends the same commands on the
// chat session itself (the original's viaWorld=0 path), which EQEmu's UCS
// handles identically. See docs/unknowns.md.

#include <cstdint>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "net/session/configuration.h"
#include "net/protocol/ucs/session_options.h"
#include "net/messages/world.h"
#include "net/messages/ucs.h"

namespace hailstone::net {

class UcsEndpoint;

// UniversalChatProxy's separate transport profile, independent of world/zone settings.

class ChatServer {
public:
    struct Options {
        session::Configuration session = ucs_session_configuration();
        // The UCS application codec is independent of the selected session.
        std::string session_protocol = "soe";
        // Hailstone safety limits, a deliberate difference: the original never retries by itself
        // (CEverQuest::ChatServerGiveTime @0x5382d0 deletes a disconnected proxy and forgets the server until the next
        // OP_SetChatServer2; reconnect_delay zero does the same), and its chat link has no receive limit (the session
        // profile's 60 s is Hailstone's, so a link gone silent ends and is retried). An acknowledged login with no
        // answer must not wait forever, and a failed transport retries without replaying commands, doubling the delay
        // Application-side deadline; a session protocol owns its own
        // transport handshake timeout in opaque session parameters.
        std::chrono::milliseconds connect_timeout{30000};
        // each time and giving up after a few attempts. A refusal (an explicit one, or stock UCS closing the link
        // before any login reply) waits for new credentials.
        std::chrono::milliseconds login_timeout{30000};
        std::chrono::milliseconds reconnect_delay{5000}; // the first retry's delay; zero disables automatic retry
        std::chrono::milliseconds max_reconnect_delay{60000};
        std::size_t max_reconnect_attempts = 5; // retries in a row without an accepted login
        std::size_t max_pending_packets = 128;
        std::size_t max_pending_bytes = 256u << 10;
        std::size_t max_before_login_events = 256;
        std::size_t max_before_login_bytes = 1u << 20;
    };
    ChatServer();
    explicit ChatServer(Options options);
    ~ChatServer();
    void connect(const ChatServerInfo& info);
    void update();
    bool connected() const;
    bool failed() const;
    std::string_view server_short_name() const { return info_.server_short_name; }
    bool send_command(const std::string& text);
    // A mail command ("[getheaders" ...), see encode_mail_bracket_command.
    bool send_mail_command(const std::string& text);
    bool send_mail_request(const MailRequest& request);
    std::optional<ChatServerEvent> poll();
    // The joined channels in /1../10 order (from the last OP_Mail list).
    const std::vector<std::string>& channels() const { return channels_; }
    void disconnect();

    // The host's name is being looked up (on a thread of its own: a slow or failing DNS must not stop the frame).
    bool resolving() const { return resolving_ != nullptr; }
    // Testing: the chat link goes silent both ways (Session::simulate_link_loss), or its socket is closed under it
    // (Session::simulate_socket_loss).
    void simulate_link_loss();
    void simulate_socket_loss();
    const std::string& error() const { return error_; }

private:
    struct Resolve; // the lookup's result, shared with its thread
    using Clock = std::chrono::steady_clock;
    Clock::time_point now() const;
    void begin_attempt();
    void fail(std::string error, bool retry);
    bool queue_command(messages::UcsCommand command);
    void accept_event(ChatServerEvent event);
    void start_session(const std::string& address, std::uint16_t port);
    Options options_;
    std::unique_ptr<UcsEndpoint> endpoint_;
    std::shared_ptr<Resolve> resolving_;
    ChatServerInfo info_;
    bool active_ = false;
    bool login_sent_ = false;
    bool logged_in_ = false;
    std::string error_;
    Clock::time_point attempt_started_, login_started_, last_probe_, retry_at_;
    bool retry_pending_ = false;
    std::size_t retries_ = 0; // automatic retries since connect() or the last accepted login
    bool channels_until_disconnect_polled_ = false; // undelivered messages still need the old channel numbers
    std::deque<ChatServerEvent> events_;
    std::deque<ChatServerEvent> before_login_; // EQEmu sends friend/ignore lists before its login reply
    std::size_t before_login_bytes_ = 0;
    std::deque<messages::UcsCommand> pending_; // semantic commands held only until this login is accepted
    std::size_t pending_bytes_ = 0;
    std::vector<std::string> channels_;
};

} // namespace hailstone::net
