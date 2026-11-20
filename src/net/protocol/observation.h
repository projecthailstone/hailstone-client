#pragma once

// Diagnostics below the game connection. Wire observers cannot modify,
// inject, cancel or replay packets. Gameplay observers see the *semantic*
// message rather than transport bytes. SOE framing trace remains owned by
// the selected session implementation (soe::Options::trace).

#include "net/messages/message.h"
#include "net/messages/opcode.h"
#include "net/wire/app_packet.h"

namespace hailstone::net::protocol {

enum class ObserveDirection { Outbound, Inbound };

class EndpointObserver {
public:
    virtual ~EndpointObserver() = default;
    virtual void on_message(messages::ServiceScope, ObserveDirection, const messages::GameMessage&) {}
    virtual void on_application_packet(messages::ServiceScope, ObserveDirection, const AppPacket&) {}
};

// Explicit wire interception point for legacy plugin packet hooks. It is
// strictly below application codec dispatch: no gameplay handler receives
// raw bytes. Observers above remain immutable diagnostics.
class WireInterceptor {
public:
    virtual ~WireInterceptor() = default;
    virtual bool allow_packet(messages::ServiceScope, ObserveDirection, AppPacket&) = 0;
};

} // namespace hailstone::net::protocol
