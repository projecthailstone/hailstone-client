#pragma once
// Explicit privileged wire-injection capability for packet plugins and tools.
// Never used for game commands; game commands pass typed messages to IGameCodec.
#include "net/messages/result.h"
#include "net/wire/app_packet.h"
namespace hailstone::net::protocol {
class WireDiagnosticPort {
public:
    virtual ~WireDiagnosticPort() = default;
    virtual messages::CodecResult<std::size_t> inject_wire(const AppPacket& packet) = 0;
};
}
