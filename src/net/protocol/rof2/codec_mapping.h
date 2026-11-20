#pragma once

#include <span>
#include "net/messages/opcode.h"
#include "net/protocol/rof2/opcode_map.h"

namespace hailstone::net::rof2::detail {
struct OpcodeMapping {
    messages::ServiceScope scope;
    messages::GameOpcode semantic;
    Op wire_name;
};

// One canonical RoF2 semantic-to-wire table, instantiated in this translation
// unit rather than independently in each codec entrypoint.
std::span<const OpcodeMapping> mappings() noexcept;
} // namespace hailstone::net::rof2::detail
