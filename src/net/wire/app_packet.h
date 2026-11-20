#pragma once

// Application packets: what the SOE session layer delivers and accepts.
// The opcode is the raw 16-bit value on the wire (RoF2 numbering for world
// and zone, login-server numbering for login); the selected application codec maps it to
// names. The session layer handles the opcode's wire encoding (including the
// extra 0x00 before opcodes whose low byte is zero).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace hailstone::net {

struct AppPacket {
    std::uint16_t opcode = 0;
    std::vector<std::byte> data;

    AppPacket() = default;
    AppPacket(std::uint16_t op, std::vector<std::byte> payload = {}) : opcode(op), data(std::move(payload)) {}
};

} // namespace hailstone::net
