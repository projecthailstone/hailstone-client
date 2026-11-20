#include "net/connection/login_endpoint.h"

#include <exception>
#include <stdexcept>
#include <utility>

#include "net/protocol/login/codec.h"

namespace hailstone::net {

LoginEndpoint::LoginEndpoint(std::unique_ptr<ISession> session)
    : session_(std::move(session)), codec_(std::make_unique<login_codec::Codec>()) {}
LoginEndpoint::~LoginEndpoint() = default;

void LoginEndpoint::connect(const std::string& host, std::uint16_t port) { session_->connect(host, port); }
void LoginEndpoint::update() {
    session_->update();
    while (auto packet = session_->receive()) {
        auto result = codec_->decode(*packet);
        if (auto* event = std::get_if<messages::LoginEvent>(&result)) {
            if (received_.size() >= 2048) {
                if (issues_.size() >= 32) issues_.pop_front();
                issues_.push_back({messages::LoginCodecIssue::Kind::Malformed, event->request_id,
                                   "login semantic event queue full"});
                continue;
            }
            received_.push_back(std::move(*event));
        } else if (auto* issue = std::get_if<messages::LoginCodecIssue>(&result)) {
            if (issues_.size() >= 32) issues_.pop_front();
            issues_.push_back(std::move(*issue));
        }
    }
}

bool LoginEndpoint::send(const messages::LoginCommand& command) {
    try {
        auto packet = codec_->encode(command);
        if (session_->send(packet)) return true;
        codec_->cancel(command.id);
        return false;
    } catch (const std::exception& e) {
        codec_->cancel(command.id);
        if (issues_.size() >= 32) issues_.pop_front();
        issues_.push_back({messages::LoginCodecIssue::Kind::Malformed, command.id, e.what()});
        return false;
    }
}

std::optional<messages::LoginEvent> LoginEndpoint::receive() {
    if (received_.empty()) return std::nullopt;
    auto event = std::move(received_.front());
    received_.pop_front();
    return event;
}
std::optional<messages::LoginCodecIssue> LoginEndpoint::receive_issue() {
    if (issues_.empty()) return std::nullopt;
    auto issue = std::move(issues_.front());
    issues_.pop_front();
    return issue;
}
void LoginEndpoint::cancel(std::uint64_t id) { codec_->cancel(id); }
void LoginEndpoint::disconnect() { session_->disconnect(); }
SessionState LoginEndpoint::state() const { return session_->state(); }
std::string LoginEndpoint::error() const { return session_->error(); }
std::uint32_t LoginEndpoint::sequence() const { return codec_->last_sequence(); }

} // namespace hailstone::net
