#pragma once

#include <cstdint>
#include "net/messages/opcode.h"

namespace hailstone::net::messages {

// Identifies a connection endpoint without assuming a wire protocol.
struct EndpointId {
    ServiceScope service = ServiceScope::Zone;
    std::uint64_t instance = 0;
    friend bool operator==(const EndpointId&, const EndpointId&) = default;
};

} // namespace hailstone::net::messages
