#pragma once
// Server-provided training and skill values, independent of their wire representation.
#include <array>
#include <cstdint>
#include <string>
#include "game/training.h"
namespace hailstone::net {
struct GMTraining {
    std::uint32_t trainer_id = 0;
    std::uint32_t player_id = 0;
    std::array<std::uint32_t, game::kTrainSkills> skills{};
    float greed = 0.0f;
    std::uint8_t success = 0;
    std::array<std::uint8_t, game::kTrainLanguages> languages{};
};
struct GMTrainSkillConfirm {
    std::uint32_t skill = 0;
    std::uint32_t cost = 0;
    std::uint8_t new_skill = 0;
    std::string trainer;
};
struct SkillUpdate {
    std::uint32_t skill = 0;
    std::uint32_t value = 0;
    std::uint8_t announce = 1; // byte 8: print eqstr 12091 (EQEmu always sends 1)
};
struct TrainingSkillRequest { std::uint32_t trainer = 0; bool language = false; std::uint32_t skill = 0; };
struct TrainingCloseRequest { std::uint32_t trainer = 0, player = 0; };
} // namespace hailstone::net
