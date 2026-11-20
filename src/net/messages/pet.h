#pragma once
#include <cstdint>

namespace hailstone::net {
// Semantic pet actions used by the gameplay/UI command model. The wire codec
// determines how these action selectors are represented in a given protocol.
// Pet command numbers (EQEmu zone/common.h PET_*; the original's cmd_pet
// table). The server gives them to its focused pet (NMS: the pet the player
// last targeted, Mob::GetActivePet).
enum PetCommand : std::uint32_t {
    kPetHealthReport = 0, kPetLeader = 1, kPetAttack = 2, kPetQAttack = 3, kPetFollow = 4, kPetGuard = 5,
    kPetSit = 6, kPetSitOn = 7, kPetSitOff = 8, kPetStop = 9, kPetStopOn = 10, kPetStopOff = 11,
    kPetTaunt = 12, kPetTauntOn = 13, kPetTauntOff = 14, kPetHold = 15, kPetHoldOn = 16, kPetHoldOff = 17,
    kPetGHold = 18, kPetGHoldOn = 19, kPetGHoldOff = 20, kPetSpellHold = 21, kPetSpellHoldOn = 22,
    kPetSpellHoldOff = 23, kPetFocus = 24, kPetFocusOn = 25, kPetFocusOff = 26, kPetFeign = 27,
    kPetBackOff = 28, kPetGetLost = 29, kPetTarget = 30, kPetRegroup = 31, kPetRegroupOn = 32,
    kPetRegroupOff = 33, kPetNone = 34,
};


// Gameplay intent and pet UI state; no packet-length, padding or wire fields.
struct PetCommandIntent { std::uint32_t command = 0, target = 0; bool from_reissue = false; };
struct PetButtonState { std::uint32_t button = 0; bool enabled = false; };
struct PetTargetState { std::uint32_t target = 0; };
struct TargetOfTargetState { std::uint32_t target = 0; };
struct PetCharmState { std::uint32_t owner = 0, pet = 0; bool charmed = false; };
} // namespace hailstone::net
