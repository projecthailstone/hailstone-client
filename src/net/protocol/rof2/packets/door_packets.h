#pragma once

// Typed RoF2 packets for doors ("switches" in the original client: EQSwitch)
// and ground objects (dropped items, tradeskill containers and other world
// objects). Layouts follow the NMS server's RoF2 translator
// (common/patches/rof2.cpp ENCODE(OP_SpawnDoor) / ENCODE(OP_GroundSpawn) /
// ENCODE(OP_ClickObjectAction) / DECODE(OP_TradeSkillCombine),
// rof2_structs.h) and the original client: the OP_SpawnDoor record is read
// field by field by EQSwitch::LoadSwitchSounds @0x511990 (the switch
// constructor from a packet), OP_ClickDoor is built by EQSwitch::UseSwitch
// @0x511d70 and OP_ClickObject by CEverQuest::LMouseUp @0x5384d0.
// Per-packet notes are in docs/protocol/rof2-packets.md "Doors and objects".
//
// Positions are server coordinates as on the wire (game/coords.h converts).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/protocol/rof2/packets/item_packets.h"
#include "net/wire/app_packet.h"

#include "net/messages/door.h"

namespace hailstone::net {

inline constexpr std::size_t kDoorRecordSize = 100;

// OP_SpawnDoor, zone → client: size / 100 records (HandleWorldMessage case
// 0x7291 reads exactly that many; a short tail is ignored).
std::optional<std::vector<DoorRecord>> decode_spawn_doors(const AppPacket& p);
AppPacket encode_spawn_doors(const std::vector<DoorRecord>& doors); // tests and tools

inline constexpr std::size_t kClickDoorSize = 16;
AppPacket encode(const ClickDoor& v);
std::optional<ClickDoor> decode_click_door(const AppPacket& p);

AppPacket encode(const MoveDoor& v);
std::optional<MoveDoor> decode_move_door(const AppPacket& p);

AppPacket encode(const GroundObject& v); // tests and tools
std::optional<GroundObject> decode_ground_object(const AppPacket& p);

inline constexpr std::size_t kClickObjectSize = 8;
AppPacket encode(const ClickObject& v);
std::optional<ClickObject> decode_click_object(const AppPacket& p);

inline constexpr std::size_t kClickObjectActionSize = 92;
AppPacket encode(const ClickObjectAction& v); // tests and tools
std::optional<ClickObjectAction> decode_click_object_action(const AppPacket& p);
AppPacket encode_close_object();              // the empty client → zone packet

inline constexpr std::size_t kClearObjectSize = 8;
AppPacket encode(const ClearObject& v); // server direction, for tests
std::optional<ClearObject> decode_clear_object(const AppPacket& p);

inline constexpr std::size_t kTradeSkillCombineSize = 24;
AppPacket encode(const TradeSkillCombine& v);

// ---------------------------------------------------------------- recipes (the tradeskill window)
//
// EQEmu's RoF2 layouts (rof2_structs.h, zone/tradeskills.cpp); the original's CTradeskillWnd senders are not decoded.
// A recipe request names the container: its type (a bag's bag type, a world container's type) and, for a bag, its
// item id (0 for a world container).

// OP_RecipesFavorite (2008 bytes, TradeskillFavorites_Struct): u32 type, u32 item id, 500 × u32 recipe ids (0 =
// none). OP_RecipesSearch (80 bytes, RecipesSearch_Struct): u32 type, u32 item id, u32 min trivial, u32 max trivial,
// char[56] partial name (EQEmu: a regular expression), u32 0x300, u32. Both are answered by one OP_RecipeReply each.
inline constexpr std::size_t kRecipeFavoritesSize = 2008, kRecipesSearchSize = 80;
AppPacket encode(const RecipeFavorites& v);
AppPacket encode(const RecipesSearch& v);

// OP_RecipeReply (84 bytes, RecipeReply_Struct): u32 type, u32 item id, u32 component count, u32 recipe id, u32
// trivial, char[64] name.
std::optional<RecipeReply> decode_recipe_reply(const AppPacket& p);
AppPacket encode(const RecipeReply& v); // server direction, for tests

// OP_RecipeDetails: C→Z {u32 recipe id}; Z→C (SendTradeskillDetails) u32 recipe id **big-endian**, (10 − n) × u32
// 0xffffffff, n × {u32 item id, u32 icon (both big-endian), name}: the components, one entry per item needed. The
// client reads ten entries (an unused one anywhere) and nothing after them; EQEmu since #5100 (2026-06) appends a
// big-endian u32 trivial, which is skipped.
AppPacket encode_recipe_details_request(std::uint32_t recipe_id);
std::optional<RecipeDetails> decode_recipe_details(const AppPacket& p);
AppPacket encode(const RecipeDetails& v); // server direction, for tests

// OP_RecipeAutoCombine, both ways (40 bytes, RoF2 RecipeAutoCombine_Struct): u32 type, u32 item id, the container's
// slot (12 B; echoed), 12 B slot, u32 recipe id, u32 reply code (the server's: 0 made, 0xfffffff5 failed). The
// server takes the components from the inventory and combines.
inline constexpr std::size_t kRecipeAutoCombineSize = 40;
AppPacket encode(const RecipeAutoCombine& v);
std::optional<RecipeAutoCombine> decode_recipe_auto_combine(const AppPacket& p);

} // namespace hailstone::net
