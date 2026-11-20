#include "plugin/packet_bridge.h"
namespace hailstone::plugin {
namespace {
std::optional<Stream> stream_for(net::messages::ServiceScope scope) {
    using net::messages::ServiceScope;
    if (scope == ServiceScope::World) return Stream::World;
    if (scope == ServiceScope::Zone) return Stream::Zone;
    return std::nullopt;
}
}
bool PacketBridge::allow_packet(net::messages::ServiceScope scope, net::protocol::ObserveDirection dir,
                                net::AppPacket& packet) {
    const auto stream = stream_for(scope);
    if (!hooks_ || !stream) return true;
    const auto direction = dir == net::protocol::ObserveDirection::Inbound ? Direction::In : Direction::Out;
    return hooks_->packet(*stream, direction, packet) != Flow::Drop;
}
void PacketBridge::on_application_packet(net::messages::ServiceScope scope, net::protocol::ObserveDirection dir,
                                         const net::AppPacket& packet) {
    if (scope == net::messages::ServiceScope::Zone && dir == net::protocol::ObserveDirection::Inbound && zone_tap_)
        zone_tap_(packet);
}
} // namespace hailstone::plugin
