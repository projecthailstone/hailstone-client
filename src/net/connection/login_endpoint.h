#pragma once

// Service endpoint: packet ownership and sequence correlation are below the
// LoginClient. The caller exchanges only typed commands and events.
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "net/messages/login.h"
#include "net/session/session.h"

namespace hailstone::net {
namespace login_codec { class Codec; }

class LoginEndpoint {
public:
    explicit LoginEndpoint(std::unique_ptr<ISession> session);
    ~LoginEndpoint();
    void connect(const std::string& host, std::uint16_t port);
    void update();
    bool send(const messages::LoginCommand& command);
    std::optional<messages::LoginEvent> receive();
    std::optional<messages::LoginCodecIssue> receive_issue();
    void cancel(std::uint64_t request_id);
    void disconnect();
    SessionState state() const;
    std::string error() const;
    std::uint32_t sequence() const;
private:
    std::unique_ptr<ISession> session_;
    std::unique_ptr<login_codec::Codec> codec_;
    std::deque<messages::LoginEvent> received_;
    std::deque<messages::LoginCodecIssue> issues_;
};

} // namespace hailstone::net
