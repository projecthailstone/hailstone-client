#pragma once

// Helpers shared by the typed packet encoders and decoders
// (world_packets.cpp, zone_packets.cpp): opcode-tagged packet building,
// NUL-terminated string writing, exception-to-optional decoding, bitfield
// access and the EQ CRC.

#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "core/byte_io.h"
#include "net/wire/byte_codec.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/wire/app_packet.h"

namespace hailstone::net {

inline AppPacket make_packet(rof2::Op op, ByteWriter&& w) { return AppPacket(rof2::to_wire(op), w.take()); }

// A packet with no payload (OP_ReqClientSpawn, OP_ClientReady, OP_Logout, ...).
inline AppPacket make_empty(rof2::Op op) { return AppPacket(rof2::to_wire(op)); }

// True when the packet carries this opcode.
inline bool is_op(const AppPacket& p, rof2::Op op) { return rof2::to_wire(op) != 0 && p.opcode == rof2::to_wire(op); }

// The EQ CRC32: the standard reflected table (polynomial 0xedb88320), start
// value 0xffffffff and no final inversion, i.e. ~zlib_crc32(data). The client
// uses it as __MemChecker0 @005643f0 (file checksums, OP_ZoneEntry's name
// hash) and EQEmu as CRC32::SetEQChecksum (OP_PlayerProfile).
std::uint32_t eq_crc32(std::span<const std::byte> data);


} // namespace hailstone::net
