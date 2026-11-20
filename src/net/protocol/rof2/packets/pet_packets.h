#pragma once

// Typed RoF2 pet packets. Stock layouts follow rof2.cpp (DECODE(OP_PetCommands), ENCODE(OP_PetBuffWindow)) and
// eq_packet_structs.h; what the client sends follows the original (cmd_pet @0x4e2150, CEverQuest::IssuePetCommand
// @0x5209b0). A server's list of several pets comes from its plugin (GameConnection::set_pet_list; the NMS plugin's
// OP_PetList). Per-packet notes are in docs/protocol/rof2-packets.md ("Pets").

#include <cstdint>
#include <optional>
#include <vector>

#include "net/wire/app_packet.h"
#include "net/messages/pet.h"

namespace hailstone::net {

// OP_PetCommands (0x0159), client → zone, 12 bytes: u32 command, u32 target
// spawn id (attack, qattack and leader carry one; the rest 0), u8 flag and 3
// bytes (IssuePetCommand leaves them uninitialised; sent as 0). EQEmu reads
// command and target and ignores a command whose target is the pet itself.
struct PetCommandPacket {
    std::uint32_t command = 0;
    std::uint32_t target = 0;
    std::uint8_t flag = 0;
};
inline constexpr std::size_t kPetCommandSize = 12;
AppPacket encode(const PetCommandPacket& v);
std::optional<PetCommandPacket> decode_pet_command(const AppPacket& p);

// OP_PetCommandState (0x1dc8), zone → client, 8 bytes (PetCommandState_Struct,
// passed through): u32 button, u32 state (1 on). Buttons are EQEmu's
// PET_BUTTON_*: 0 sit, 1 stop, 2 regroup, 3 follow, 4 guard, 5 taunt, 6 hold,
// 7 ghold, 8 focus, 9 spell hold.
struct PetCommandState {
    std::uint32_t button = 0;
    std::uint32_t state = 0;
};
inline constexpr int kPetButtons = 10;
AppPacket encode(const PetCommandState& v); // server direction, for tests
std::optional<PetCommandState> decode_pet_command_state(const AppPacket& p);

// OP_PetHoTT (0x794a), zone → client, 4 bytes: the focused pet's target (0 none).
std::optional<std::uint32_t> decode_pet_hott(const AppPacket& p);


// OP_PetBuffWindow (0x5882), zone → client: the focused pet's buffs in the
// OP_BuffCreate layout (ENCODE(OP_PetBuffWindow)); decode it with
// decode_buff_icons (net/protocol/rof2/packets/spell_packets.h). entity_id is the pet.

} // namespace hailstone::net
