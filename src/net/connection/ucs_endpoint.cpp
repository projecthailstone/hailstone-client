#include "net/connection/ucs_endpoint.h"

#include <exception>
#include <utility>

#include "core/log.h"
#include "net/protocol/ucs/codec.h"
#include "net/protocol/ucs/opcodes.h"

namespace hailstone::net {
UcsEndpoint::UcsEndpoint(std::unique_ptr<ISession> session)
    : session_(std::move(session)), codec_(std::make_unique<ucs::Codec>()) {}
UcsEndpoint::~UcsEndpoint() = default;

void UcsEndpoint::connect(const std::string& host, std::uint16_t port) {
    received_.clear();
    issues_.clear();
    session_->connect(host, port);
}
void UcsEndpoint::update() {
    session_->update();
    while (auto packet = session_->receive()) {
        auto result = codec_->decode(*packet);
        if (!result) {
            if (issues_.size() >= kMaxDecodeIssues) issues_.pop_front();
            const bool login_reply = packet->opcode == ucs::kMailLogin;
            issues_.push_back({result.error()->kind, login_reply, result.error()->detail});
            // Do not allow a flood of following optional notices to evict a
            // malformed mandatory login reply from the bounded error queue.
            if (login_reply && result.error()->kind != messages::CodecErrorKind::UnsupportedOpcode)
                break;
            continue;
        }
        if (received_.size() >= 2048) {
            log::warn("UCS semantic event queue exceeded its capacity");
            session_->disconnect();
            return;
        }
        received_.push_back(std::move(*result.value()));
    }
}
bool UcsEndpoint::send(const messages::UcsCommand& command) {
    if (session_->state() != SessionState::Connected) return false;
    try {
        return session_->send(codec_->encode(command));
    } catch (const std::exception& e) {
        log::warn("UCS codec rejected command: {}", e.what());
        return false;
    }
}
std::optional<std::size_t> UcsEndpoint::wire_cost(const messages::UcsCommand& command) {
    try { return ucs::Codec{}.wire_cost(command); }
    catch (const std::exception&) { return std::nullopt; }
}
std::optional<ChatServerEvent> UcsEndpoint::receive() {
    if (received_.empty()) return std::nullopt;
    auto e = std::move(received_.front());
    received_.pop_front();
    return e;
}
std::optional<UcsEndpoint::DecodeIssue> UcsEndpoint::receive_issue() {
    if (issues_.empty()) return std::nullopt;
    auto issue = std::move(issues_.front());
    issues_.pop_front();
    return issue;
}
void UcsEndpoint::disconnect() {
    session_->disconnect();
    received_.clear();
    issues_.clear();
}
void UcsEndpoint::simulate_link_loss() { session_->simulate_link_loss(); }
void UcsEndpoint::simulate_socket_loss() { session_->simulate_socket_loss(); }
SessionState UcsEndpoint::state() const { return session_->state(); }
SessionStats UcsEndpoint::stats() const { return session_->stats(); }
std::string UcsEndpoint::error() const { return session_->error(); }
} // namespace hailstone::net
