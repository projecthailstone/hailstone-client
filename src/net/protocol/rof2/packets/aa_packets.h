#pragma once

// Alternate advancement packets. Layouts: the original's readers
// (AltAbility_Unserialize @0x4a2de0, Handle_OP_RespondAA @0x4bec60,
// Handle_OP_AAExpUpdate @0x4b70c0, Handle_OP_AAAction @0x4b5bd0) and senders
// (cmd_alternateadv @0x4f73e0, Send_OP_AAAction_* @0x607c40 / @0x608480),
// checked against EQEmu's RoF2 translator (ENCODE(OP_SendAATable),
// ENCODE(OP_RespondAA)) and zone/aa.cpp. docs/protocol/rof2-packets.md
// "Alternate advancement".

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "net/messages/progression.h"
#include "net/wire/app_packet.h"

namespace hailstone::net {

// OP_SendAATable (Z→C): one rank (game::AltAbility), variable size. The
// prerequisite and timer arrays are counted; EQEmu writes one zero
// prerequisite when there are none and always one timer (spell_type).
std::optional<game::AltAbility> decode_send_aa_table(const AppPacket& p);
AppPacket encode_send_aa_table(const game::AltAbility& a); // server direction, for tests

// OP_RespondAA (Z→C, AATable_Struct, 24 + 300 × 12 bytes): points spent,
// five assigned values (EQEmu: the spent count, then four zeros), then the
// owned ranks {id, value, charges}. Empty slots (id 0) are dropped. Every slot
// in the packet is read, not only the stock 300 (game::kOwnedAbilities).
std::optional<RespondAA> decode_respond_aa(const AppPacket& p);
AppPacket encode(const RespondAA& v); // server direction, for tests (pads to 300 slots, more when owned has more)

// OP_AAExpUpdate (Z→C, 12 bytes): AA experience (0..330), unspent points, the
// percentage of experience going to AAs (a byte; EQEmu's AltAdvStats_Struct
// has the points as a u16 and a u16 zero, the same dword).
std::optional<AAExpUpdate> decode_aa_exp_update(const AppPacket& p);
AppPacket encode(const AAExpUpdate& v); // server direction, for tests

// OP_AAAction, client → zone (16 bytes, AA_Action): action, ability, target,
// value. The original's actions: 0 activate (cmd_alternateadv "activate":
// the owned rank's id and the target), 1 set the AA experience percent
// (value), 2 AA experience off, 3 train a rank (ability = rank id).
inline constexpr std::uint32_t kAAActivate = 0, kAASetPercent = 1, kAAExpOff = 2, kAATrain = 3;
inline constexpr std::uint32_t kAATrainAll = 4; // the NMS patch's "Train All" (EQEmu NMS: buy every rank)
struct AAAction {
    std::uint32_t action = 0;
    std::uint32_t ability = 0;
    std::uint32_t target = 0;
    std::uint32_t value = 0;
};
AppPacket encode(const AAAction& v);
std::optional<AAAction> decode_aa_action(const AppPacket& p); // tests

// OP_AAAction, zone → client (12 bytes, UseAA_Struct): a reuse timer started.
// begin: when it started on the server's clock (0 for "now"), timer: the timer
// index (0..99), server_now: the server's clock when sent.
std::optional<AATimer> decode_aa_timer(const AppPacket& p);
AppPacket encode(const AATimer& v); // server direction, for tests

} // namespace hailstone::net
