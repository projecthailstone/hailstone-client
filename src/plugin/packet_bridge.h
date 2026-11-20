#pragma once
// Optional plugin wire hooks live at the endpoint, not in GameConnection.
#include <functional>
#include "net/protocol/observation.h"
#include "plugin/hooks.h"
namespace hailstone::plugin {
class PacketBridge final : public net::protocol::WireInterceptor, public net::protocol::EndpointObserver {
public:
    PacketBridge(Hooks* hooks, std::function<void(const net::AppPacket&)> zone_tap = {})
        : hooks_(hooks), zone_tap_(std::move(zone_tap)) {}
    bool allow_packet(net::messages::ServiceScope scope, net::protocol::ObserveDirection dir,
                      net::AppPacket& packet) override;
    void on_application_packet(net::messages::ServiceScope scope, net::protocol::ObserveDirection dir,
                               const net::AppPacket& packet) override;
private:
    Hooks* hooks_ = nullptr;
    std::function<void(const net::AppPacket&)> zone_tap_;
};
} // namespace hailstone::plugin
