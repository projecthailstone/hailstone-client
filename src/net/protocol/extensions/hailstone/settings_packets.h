#pragma once

// The server's settings for the client (server/README.md), from Hailstone's server script. What plugins and scripts
// may do is not here: that is the hailstone.cfg the server sends with its files (plugin/policy.h).
// OP_HailstoneSettingsRequest 0x1432, client -> zone, after every zone-in: {u32 magic 'NMSP'; u16 version 1}.
// OP_HailstoneSettings 0x1433, zone -> client: {u32 magic; u16 version 1; u32 flags}; later fields go after these, and a
//   reader ignores what it does not know. No flag is defined now: bit 0 hid the world's player count on the server
//   list, which no UI shows any more, so it is ignored.

#include <cstdint>
#include <optional>

#include "net/wire/app_packet.h"
#include "net/messages/hailstone.h"

namespace hailstone::net {

inline constexpr std::uint32_t kHailstoneSettingsMagic = 0x50534d4e; // "NMSP" on the wire
inline constexpr std::uint16_t kOpHailstoneSettingsRequest = 0x1432;
inline constexpr std::uint16_t kOpHailstoneSettings = 0x1433;
inline constexpr std::uint16_t kHailstoneSettingsVersion = 1;

AppPacket encode_hailstone_settings_request();
// nullopt for a wrong opcode, magic or version, or a short packet; bytes after the known fields are ignored.
std::optional<HailstoneSettings> decode_hailstone_settings(const AppPacket& p);
AppPacket encode(const HailstoneSettings& v); // server direction, for tests

} // namespace hailstone::net
