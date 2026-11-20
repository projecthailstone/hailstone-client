#pragma once

// The Find window's quest NPCs (docs/navigation.md "Find"), from Hailstone's server script (server/README.md): which
// of the zone's NPCs have quests, by their quest files.
// OP_HsQuestNpcsRequest 0x1436, client -> zone, once a zone when the Find window first lists its NPCs:
//   {u32 magic 'HSQN'; u16 version 1}.
// OP_HsQuestNpcs 0x1437, zone -> client: {u32 magic; u16 version 1; u16 count; count x {u8 flags (1 takes hand-ins,
//   2 talks); u8 length; the npc_types name (no terminator)}}.
// A server without the script never answers; the window then lists no quest givers.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/wire/app_packet.h"
#include "net/messages/hailstone.h"

namespace hailstone::net {

inline constexpr std::uint32_t kHsQuestNpcsMagic = 0x4e515348; // "HSQN" on the wire
inline constexpr std::uint16_t kOpHsQuestNpcsRequest = 0x1436;
inline constexpr std::uint16_t kOpHsQuestNpcs = 0x1437;
inline constexpr std::uint16_t kHsQuestNpcsVersion = 1;

AppPacket encode_quest_npcs_request();
// nullopt for a wrong opcode, magic or version, or a short packet.
std::optional<std::vector<QuestNpc>> decode_quest_npcs(const AppPacket& p);
AppPacket encode_quest_npcs(const std::vector<QuestNpc>& npcs); // server direction, for tests

} // namespace hailstone::net
