#pragma once
#include "net/messages/findable.h"

// Movement packets the original client sends on its own (game/mechanics/movement.h):
//   OP_Jump         (0x31f4) no payload; DoMainLoop @0x53ecc0 after a jump.
//                   The server takes endurance (Handle_OP_Jump).
//   OP_EnvDamage    (0x51fd) EnvDamage2_Struct (rof2_structs.h, 39 bytes): the
//                   damage the client computed for a fall (type 0xfc),
//                   drowning (0xfb), lava (0xfa) or freezing water (0xf6);
//                   built by ApplyFallDamage @0x45ae2f / ProcessAirSupply and
//                   sent by CharacterZoneClient::ApplyDamage @0x457d60. The
//                   server subtracts it (Handle_OP_EnvDamage).
//   OP_SafeFallSuccess (0x2219) no payload; ApplyFallDamage when Safe Fall reduced a fall.
//   OP_SetRunMode   (0x009f) u32: 1 run, 0 walk (CharacterZoneClient::DoPassageOfTime, when __RunWalkState changes).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "net/messages/tracking.h"
#include "net/wire/app_packet.h"

namespace hailstone::net {

struct EnvDamage {
    std::uint32_t spawn_id = 0; // the client writes its 16-bit spawn id and zeroes the rest
    std::uint32_t damage = 0;
    std::uint8_t type = 0;      // 0xfc falling, 0xfb drowning, 0xfa lava, 0xf6 freezing, 0xf4 fire
};

AppPacket encode(const EnvDamage& d);
std::optional<EnvDamage> decode_env_damage(const AppPacket& p);

AppPacket encode_jump();
AppPacket encode_safe_fall_success();
AppPacket encode_set_run_mode(bool running);

// The find path (game/player_path.h; EQEmu Handle_OP_FindPersonRequest, Client::SendPath). OP_FindPersonRequest
// (C→Z, 40 bytes, RoF2 FindPersonRequest_Struct, Send_OP_FindPersonRequest @0x59a420): u32 the find's kind, u16 the
// spawn (the original copies only the low half of +0x1c; the high half is left as zero here), u32 / u32 the real
// estate ids, the player's position, the point (f32 each in the packet's FindPerson_Point order). EQEmu reads only
// the spawn id. OP_FindPersonReply (Z→C): 12-byte points, the destination first, then the path from the player (EQEmu
// repeats its last point); empty when there is no path. The packet's point order is this client's world order
// (the server's y, x are its x, y), so the points are world coordinates as they come.
AppPacket encode(const FindPersonRequest& r);
// Every point of the reply in order (count = size / 12), as PlayerPath_HandleFindPersonReply takes them.
std::optional<std::vector<glm::vec3>> decode_find_person_reply(const AppPacket& p);
AppPacket encode_find_person_reply(const std::vector<glm::vec3>& path);             // server direction, for tests

// OP_SendFindableNPCs (Z→C, 0x4613, 112 bytes; EQEmu FindableNPC_Struct, sent for each findable NPC at the zone-in by
// EntityList::SendFindableNPCList and again as one spawns or goes, UpdateFindableNPCState). Handle_OP_SendFindableNPCs
// @0x4b8b50 reads: u32 +0 a removal when not 0, u32 +4 the spawn, the name at +8 (64) and the surname at +0x48 (32),
// u32 +0x68 the race and the byte +0x6c the class (the find window's FindPlayerData). The three bytes after it are
// unread (EQEmu writes 0x16 0x06 0x24).
std::optional<FindableNpc> decode_findable_npc(const AppPacket& p);
AppPacket encode_findable_npc(const FindableNpc& n); // server direction, for tests

} // namespace hailstone::net
