#pragma once
// Hailstone server script extension; NOT part of stock RoF2.
#include <cstdint>
#include <optional>
#include "net/wire/app_packet.h"
#include "net/messages/social.h"
namespace hailstone::net {
// Hailstone's world-wide guild invitations (server/README.md): EQEmu only finds an OP_GuildInvite's target in the
// inviter's zone, and its OP_GuildInviteAccept wants both in one zone. A server running server/'s Lua quest script
// carries an invitation across zones on two opcodes of this client's own that no opcode table lists, so the zone
// hands them to the script (event_unhandled_opcode) and the script answers with raw packets. Both are 80 bytes:
// {u32 magic 'NMSG'; u16 version 1; u16 action; char[64] name; u32 guild id; u32 value}.
//   OP_HailstoneGuild 0x1430, client → zone: action 0 hello (after every zone-in); 1 invite (name: whom, guild: the
//     inviter's, value: the rank offered, 8); 2 answer (name: the inviter, guild: the one offered, value: the rank
//     to accept, or kGuildInviteDecline / kGuildInviteAutoDecline).
//   OP_HailstoneGuildReply 0x1431, zone → client: action 0 the hello's answer (value: kHailstoneGuildWorldInvites when the
//     script takes invitations to other zones); 1 an invitation (name: the inviter, guild, value: the rank).
// Without the hello's answer the client keeps to the stock packets.
inline constexpr std::uint16_t kOpHailstoneGuild = 0x1430;
inline constexpr std::uint16_t kOpHailstoneGuildReply = 0x1431;
inline constexpr std::size_t kHailstoneGuildSize = 80;
AppPacket encode_hailstone_guild(const HailstoneGuild& v, bool reply = false); // reply: OP_HailstoneGuildReply (tests, the server)
// nullopt for another opcode, a wrong magic or version, or a short packet.
std::optional<HailstoneGuild> decode_hailstone_guild(const AppPacket& p, bool reply = true);

}
