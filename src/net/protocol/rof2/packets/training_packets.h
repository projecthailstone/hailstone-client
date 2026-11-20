#pragma once

// Guildmaster training packets: the request the original sends on a right
// click on a guildmaster (the OP_GMTraining sender in @0x536d80), the
// server's answer (Handle_OP_GMTraining @0x4b49c0), training a row
// (Send_OP_GMTrainSkill @0x781530), its confirmation (HandleWorldMessage's
// OP_GMTrainSkillConfirm case @0x4d2747) and closing the window
// (Send_OP_GMEndTraining @0x781180). Checked against EQEmu's
// GMTrainee_Struct, GMSkillChange_Struct, GMTrainSkillConfirm_Struct and
// zone/client_process.cpp OPGMTraining. docs/protocol/rof2-packets.md "Training".

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "net/messages/training.h"
#include "net/wire/app_packet.h"

namespace hailstone::net {

// OP_GMTraining (448 bytes both ways, GMTrainee_Struct): the guildmaster's
// and the player's spawn ids, 100 skill dwords, then (+0x198) the greed
// factor as a float, (+0x19c) a success byte and (+0x19d) 32 language
// bytes. The request has zero skills and greed 0; the answer the
// guildmaster's limits (EQEmu: greed 1.08, success 1, languages 0xc9).
inline constexpr std::size_t kGMTrainingSize = 448;
AppPacket encode(const GMTraining& v);
std::optional<GMTraining> decode_gm_training(const AppPacket& p);

// OP_GMTrainSkill (12 bytes): the guildmaster's spawn id, 1 for a language,
// the skill or language id.
AppPacket encode_gm_train_skill(std::uint32_t trainer_id, bool language, std::uint32_t id);

// OP_GMEndTraining (8 bytes, GMTrainEnd_Struct): guildmaster, player.
AppPacket encode_gm_end_training(std::uint32_t trainer_id, std::uint32_t player_id);

// OP_GMTrainSkillConfirm (Z→C, 76 bytes): skill id (100 + n for language n),
// the cost in copper, 1 when newly learned, the guildmaster's name[64].
std::optional<GMTrainSkillConfirm> decode_gm_train_skill_confirm(const AppPacket& p);
AppPacket encode(const GMTrainSkillConfirm& v); // server direction, for tests

} // namespace hailstone::net
