#pragma once

// RoF2-private application wire opcode lookup for world and zone packets. The names are
// EQEmu's (OP_ prefix dropped); the wire values come from stock EQEmu's
// utils/patches/patch_RoF2.conf via tools/gen_opcodes.py → opcodes_rof2.inc. The login
// server uses its own numbering and is not in this table.

#include <cstdint>

namespace hailstone::net::rof2 {

enum class Op : std::uint16_t {
#define HAILSTONE_OP(name, value) name,
#include "net/protocol/rof2/opcodes.inc"
#include "net/protocol/rof2/nonstandard_opcodes.inc"
#undef HAILSTONE_OP
    Count_
};

// RoF2 wire value of an opcode; 0 when RoF2 has none (the conf lists 0x0000).
std::uint16_t to_wire(Op op);

// Name for a wire value; Op::Unknown when the value is 0 or unmapped. When
// several names share a value, the first one in the conf wins.
Op from_wire(std::uint16_t wire);

// "SendLoginInfo" etc.; "Unknown" for out-of-range values.
const char* op_name(Op op);

} // namespace hailstone::net::rof2
