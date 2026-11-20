#pragma once

// Typed UCS application endpoint. One selected session carries the packets;
// the UCS codec is solely responsible for opcode and payload translation.
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "net/messages/ucs.h"
#include "net/messages/result.h"
#include "net/session/session.h"

namespace hailstone::net {
namespace ucs { class Codec; }
class UcsEndpoint {
public:
    explicit UcsEndpoint(std::unique_ptr<ISession> session);
    ~UcsEndpoint();
    void connect(const std::string& host, std::uint16_t port);
    void update();
    bool send(const messages::UcsCommand& command);
    static std::optional<std::size_t> wire_cost(const messages::UcsCommand& command);
    std::optional<ChatServerEvent> receive();
    struct DecodeIssue {
        messages::CodecErrorKind kind;
        bool mail_login_reply = false; // semantic classification; no wire opcode leaks to chat
        std::string detail;
    };
    std::optional<DecodeIssue> receive_issue();
    void disconnect();
    // Forward failure simulations to the selected session for UCS tests.
    void simulate_link_loss();
    void simulate_socket_loss();
    SessionState state() const;
    SessionStats stats() const;
    std::string error() const;
private:
    std::unique_ptr<ISession> session_;
    std::unique_ptr<ucs::Codec> codec_;
    std::deque<ChatServerEvent> received_;
    std::deque<DecodeIssue> issues_;
    static constexpr std::size_t kMaxDecodeIssues = 256;
};
} // namespace hailstone::net
