#pragma once

// The server's zone connections for the map's zone finder (game/zone_routes.h, docs/navigation.md "Zone atlas"),
// from Hailstone's server script (server/README.md). Not in EQEmu's opcode table: a stock server never answers.
// OP_HsZoneLinksRequest 0x1438, client -> zone, when the map's World view first opens in a session and the zone has
//   answered OP_HailstoneSettingsRequest (the script is there): {u32 magic 'HSZL'; u16 version 1}.
// OP_HsZoneLinks 0x1439, zone -> client: {u32 magic; u16 version 1; u16 flags (none defined); u32 count;
//   count x {u16 from zone id; u16 to zone id}}: the zone_points rows (version 0 or -1) the server's current
//   expansion and content flags allow, one per pair of zones, the zone itself and zone 0 left out; then, from a script
//   that knows them: {i16 the server's current expansion (Expansion:CurrentExpansion, -1 all); u32 zone count;
//   count x u16 zone id}: the zones (version 0) its expansion and content flags allow. A reply without that part is
//   still whole (an older script); a reader ignores bytes after what it knows.

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "net/wire/app_packet.h"
#include "net/messages/hailstone.h"

namespace hailstone::net {

inline constexpr std::uint32_t kZoneLinksMagic = 0x4c5a5348; // "HSZL" on the wire
inline constexpr std::uint16_t kOpHsZoneLinksRequest = 0x1438;
inline constexpr std::uint16_t kOpHsZoneLinks = 0x1439;
inline constexpr std::uint16_t kZoneLinksVersion = 1;
// More than any server has (EQEmu's zone ids are below 1000); a larger count is a bad packet.
inline constexpr std::uint32_t kZoneLinksMax = 32768;


AppPacket encode_zone_links_request();
// nullopt for a wrong opcode, magic or version, a count over kZoneLinksMax, or fewer links or zones than a count says.
std::optional<ZoneLinks> decode_zone_links(const AppPacket& p);
AppPacket encode(const ZoneLinks& v); // server direction, for tests and the mock server

} // namespace hailstone::net
