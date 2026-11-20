#include "net/connection/protocol_endpoint.h"
#include "net/protocol/codec.h"
#include "net/protocol/registry.h"
#include "net/protocol/observation.h"
#include "net/session/session.h"

#include <utility>

#include "net/session/factory.h"

namespace hailstone::net {

ProtocolEndpoint::~ProtocolEndpoint() = default;

ProtocolEndpoint::ProtocolEndpoint(protocol::ProtocolProfile profile,
                                   std::unique_ptr<protocol::IGameCodec> codec,
                                   std::unique_ptr<ISession> session,
                                   std::vector<std::pair<std::string, std::unique_ptr<protocol::IGameCodec>>> extensions)
    : profile_(std::move(profile)), codec_(std::move(codec)), session_(std::move(session)),
      extensions_(std::move(extensions)) {}

bool ProtocolEndpoint::has_extension(std::string_view id) const {
    for (const auto& [name, codec] : extensions_) if (id == name) return true;
    return false;
}

ProtocolEndpoint::Creation ProtocolEndpoint::create(protocol::ProtocolProfile profile,
                                                     const protocol::CodecRegistry& codecs) {
    auto codec = codecs.create(profile.game_codec);
    if (!codec) return {nullptr, "application codec is not registered: " + profile.game_codec};
    if (!codec->capabilities().accepts(profile.service))
        return {nullptr, "application codec does not implement requested service"};
    session::SessionRegistry sessions;
    auto session = sessions.create(profile.session_protocol, profile.session_config);
    if (!session) return {nullptr, "session protocol is unavailable or rejected its options: " + profile.session_protocol};
    std::vector<std::pair<std::string, std::unique_ptr<protocol::IGameCodec>>> extensions;
    for (const auto& id : profile.extension_codecs) {
        if (id == profile.game_codec) return {nullptr, "extension cannot replace the selected game codec: " + id};
        auto ext = codecs.create_extension(id);
        if (!ext) return {nullptr, "extension codec is not registered: " + id};
        if (!ext->capabilities().accepts(profile.service))
            return {nullptr, "extension codec does not implement this endpoint service: " + id};
        for (const auto& [existing, instance] : extensions)
            if (existing == id) return {nullptr, "extension codec selected twice: " + id};
        extensions.emplace_back(id, std::move(ext));
    }
    return {std::unique_ptr<ProtocolEndpoint>(
        new ProtocolEndpoint(std::move(profile), std::move(codec), std::move(session), std::move(extensions))), {}};
}

void ProtocolEndpoint::connect(const std::string& host, std::uint16_t port) {
    received_.clear();
    errors_.clear();
    // A reused endpoint must never retain server-specific, one-zone state.
    invalidate_transient_requests();
    session_->connect(host, port);
}

void ProtocolEndpoint::update() {
    session_->update();
    while (auto packet = session_->receive()) {
        if (wire_interceptor_ && !wire_interceptor_->allow_packet(profile_.service, protocol::ObserveDirection::Inbound, *packet)) continue;
        if (observer_) observer_->on_application_packet(profile_.service, protocol::ObserveDirection::Inbound, *packet);
        auto decoded = codec_->decode(profile_.service, *packet);
        // Extensions are overlays, not replacements for a successfully
        // decoded stock message. Collision with a stock opcode must not
        // reinterpret the base packet as Hailstone custom data.
        if (!decoded && decoded.error()->kind == messages::CodecErrorKind::UnsupportedOpcode) {
            for (auto& [id, extension] : extensions_) {
                auto extension_result = extension->decode(profile_.service, *packet);
                if (extension_result || extension_result.error()->kind != messages::CodecErrorKind::UnsupportedOpcode) {
                    decoded = std::move(extension_result);
                    break;
                }
            }
        }
        if (!decoded) {
            record_decode_error(*decoded.error(), *packet);
            continue;
        }
        for (auto& message : *decoded.value()) {
            if (observer_) observer_->on_message(profile_.service, protocol::ObserveDirection::Inbound, message);
            if (received_.size() == kMaxQueuedMessages) {
                record_decode_error({messages::CodecErrorKind::ResourceLimit,
                                     "semantic receive queue exceeded its capacity"}, *packet);
                disconnect();
                return;
            }
            received_.push_back(std::move(message));
        }
    }
}

void ProtocolEndpoint::record_decode_error(messages::CodecError issue, const AppPacket& packet) {
    issue.service = profile_.service;
    issue.wire_opcode = packet.opcode;
    issue.payload_size = packet.data.size();
    // Only identify source-confirmed, unambiguous stock mappings. Do not
    // fabricate a stock opcode identity for an extension-only packet.
    issue.opcode = codec_->identify_wire(profile_.service, packet.opcode);
    if (errors_.size() >= kMaxQueuedErrors) errors_.pop_front();
    errors_.push_back(std::move(issue));
}

messages::CodecResult<std::size_t> ProtocolEndpoint::send(const messages::OutgoingGameMessage& outgoing,
        const std::function<bool()>& still_current) {
    if (session_->state() != SessionState::Connected)
        return messages::CodecResult<std::size_t>::failure({
            messages::CodecErrorKind::DeliveryRejected, "session is not connected"});
    // The stock codec owns supported operations in both directions. Extensions
    // may encode commands only when the stock codec lacks that operation.
    protocol::IGameCodec* selected = codec_.get();
    if (!codec_->supports(profile_.service, outgoing.message.opcode, protocol::CodecDirection::Outbound)) {
        for (auto& [id, extension] : extensions_)
            if (extension->supports(profile_.service, outgoing.message.opcode, protocol::CodecDirection::Outbound)) {
                selected = extension.get();
                break;
            }
    }
    auto encoded = selected->encode(profile_.service, outgoing.message);
    if (!encoded) return messages::CodecResult<std::size_t>::failure(*encoded.error());
    if (encoded.value()->empty())
        return messages::CodecResult<std::size_t>::failure({
            messages::CodecErrorKind::DeliveryRejected, "outbound codec produced no application packets"});

    std::size_t count = 0;
    const bool reliable = outgoing.delivery.reliability == messages::Reliability::ReliableOrdered;
    for (auto packet : *encoded.value()) {
        if (wire_interceptor_ && !wire_interceptor_->allow_packet(profile_.service, protocol::ObserveDirection::Outbound, packet)) continue;
        if (still_current && !still_current())
            return messages::CodecResult<std::size_t>::failure({
                messages::CodecErrorKind::DeliveryRejected, "outbound action retired during packet interception"});
        if (session_->state() != SessionState::Connected)
            return messages::CodecResult<std::size_t>::failure({
                messages::CodecErrorKind::DeliveryRejected, "session disconnected before packet admission"});
        if (!session_->send(packet, reliable, outgoing.delivery.allow_coalescing))
            return messages::CodecResult<std::size_t>::failure({
                messages::CodecErrorKind::DeliveryRejected,
                "session rejected an encoded packet before admission"});
        if (observer_) observer_->on_application_packet(profile_.service, protocol::ObserveDirection::Outbound, packet);
        ++count;
    }
    if (!count)
        return messages::CodecResult<std::size_t>::failure({
            messages::CodecErrorKind::DeliveryRejected, "outgoing application packets were filtered"});
    selected->on_send_admitted(profile_.service, outgoing.message);
    if (observer_) observer_->on_message(profile_.service, protocol::ObserveDirection::Outbound, outgoing.message);
    return messages::CodecResult<std::size_t>::success(count);
}

// Reserved for capability-checked plugin diagnostics. No application
// message is fabricated, and this entrypoint is never a codec fallback.
messages::CodecResult<std::size_t> ProtocolEndpoint::inject_wire(const AppPacket& source) {
    if (session_->state() != SessionState::Connected)
        return messages::CodecResult<std::size_t>::failure({messages::CodecErrorKind::InvalidValue, "diagnostic endpoint disconnected"});
    AppPacket packet = source;
    if (wire_interceptor_ && !wire_interceptor_->allow_packet(profile_.service, protocol::ObserveDirection::Outbound, packet))
        return messages::CodecResult<std::size_t>::success(0);
    if (!session_->send(packet, true, true))
        return messages::CodecResult<std::size_t>::failure({
            messages::CodecErrorKind::DeliveryRejected, "diagnostic session rejected packet"});
    if (observer_) observer_->on_application_packet(profile_.service, protocol::ObserveDirection::Outbound, packet);
    return messages::CodecResult<std::size_t>::success(1);
}

std::optional<messages::GameMessage> ProtocolEndpoint::receive() {
    if (received_.empty()) return std::nullopt;
    auto message = std::move(received_.front());
    received_.pop_front();
    return message;
}

std::optional<messages::CodecError> ProtocolEndpoint::receive_error() {
    if (errors_.empty()) return std::nullopt;
    auto message = std::move(errors_.front());
    errors_.pop_front();
    return message;
}

SessionState ProtocolEndpoint::state() const { return session_->state(); }
std::string ProtocolEndpoint::error() const { return session_->error(); }
SessionStats ProtocolEndpoint::stats() const { return session_->stats(); }
void ProtocolEndpoint::disconnect() {
    invalidate_transient_requests();
    session_->disconnect();
}
void ProtocolEndpoint::simulate_link_loss() { session_->simulate_link_loss(); }
void ProtocolEndpoint::set_no_hold(bool on) { session_->set_no_hold(on); }
void ProtocolEndpoint::invalidate_transient_requests() {
    codec_->invalidate_transient_requests();
    for (auto& [name, extension] : extensions_) extension->invalidate_transient_requests();
}

} // namespace hailstone::net

namespace hailstone::net {

std::optional<Item> ProtocolEndpoint::inspect_item_wire(std::span<const std::byte> bytes) const {
    return codec_ ? codec_->inspect_item_wire(bytes) : std::nullopt;
}

} // namespace hailstone::net
