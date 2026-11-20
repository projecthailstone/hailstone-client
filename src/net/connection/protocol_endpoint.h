#pragma once

#include <deque>
#include <functional>
#include <span>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include "net/messages/item.h"
#include "net/messages/message.h"
#include "net/messages/result.h"
#include "net/protocol/profile.h"
#include "net/protocol/diagnostic_port.h"

namespace hailstone::net::protocol {
class CodecRegistry;
class IGameCodec;
class EndpointObserver;
class WireInterceptor;
}

namespace hailstone::net {
class ISession;
enum class SessionState : std::uint8_t;
struct SessionStats;

// A world/zone connection's sole codec/session junction. Game code never
// handles AppPacket or an SOE opcode; the endpoint can use independently
// selected codecs and session flavors. There is no raw packet fallback.
class ProtocolEndpoint final : public protocol::WireDiagnosticPort {
public:
    ~ProtocolEndpoint();
    struct Creation {
        std::unique_ptr<ProtocolEndpoint> endpoint;
        std::string error;
        explicit operator bool() const { return static_cast<bool>(endpoint); }
    };

    static Creation create(protocol::ProtocolProfile profile, const protocol::CodecRegistry& codecs);

    void connect(const std::string& host, std::uint16_t port);
    void update();
    SessionState state() const;
    std::string error() const;
    SessionStats stats() const;
    void disconnect();
    void simulate_link_loss();
    void set_no_hold(bool on);
    void invalidate_transient_requests();

    // `still_current` is evaluated after each synchronous outbound wire hook,
    // immediately before admitting the packet to the session.
    messages::CodecResult<std::size_t> send(const messages::OutgoingGameMessage& message,
                                            const std::function<bool()>& still_current = {});
    messages::CodecResult<std::size_t> inject_wire(const AppPacket& packet) override;
    // Diagnostic decode uses the selected protocol codec, never a hard-coded RoF2 parser.
    std::optional<Item> inspect_item_wire(std::span<const std::byte> bytes) const;
    std::optional<messages::GameMessage> receive();
    std::optional<messages::CodecError> receive_error();

    const protocol::ProtocolProfile& profile() const { return profile_; }
    bool has_extension(std::string_view id) const;
    void set_observer(std::shared_ptr<protocol::EndpointObserver> observer) { observer_ = std::move(observer); }
    void set_wire_interceptor(std::shared_ptr<protocol::WireInterceptor> interceptor) { wire_interceptor_ = std::move(interceptor); }

private:
    ProtocolEndpoint(protocol::ProtocolProfile profile,
                     std::unique_ptr<protocol::IGameCodec> codec,
                     std::unique_ptr<ISession> session,
                     std::vector<std::pair<std::string, std::unique_ptr<protocol::IGameCodec>>> extensions);

    protocol::ProtocolProfile profile_;
    std::unique_ptr<protocol::IGameCodec> codec_;
    std::unique_ptr<ISession> session_;
    std::vector<std::pair<std::string, std::unique_ptr<protocol::IGameCodec>>> extensions_;
    std::shared_ptr<protocol::EndpointObserver> observer_;
    std::shared_ptr<protocol::WireInterceptor> wire_interceptor_;
    std::deque<messages::GameMessage> received_;
    std::deque<messages::CodecError> errors_;
    static constexpr std::size_t kMaxQueuedMessages = 8192;
    static constexpr std::size_t kMaxQueuedErrors = 256;
    // Consistent bounded error records from the selected base or overlay
    // codec; callers never need to interpret an application wire opcode.
    void record_decode_error(messages::CodecError error, const AppPacket& packet);
};

} // namespace hailstone::net
