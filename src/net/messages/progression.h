#pragma once
// AA status and commands are gameplay values. Binary action numbers, rank
// layouts, and timer packet framing belong exclusively to the selected codec.
#include <array>
#include <cstdint>
#include <vector>
#include "game/alt_adv.h"
namespace hailstone::net {
struct RespondAA {
    std::uint32_t spent = 0;
    std::array<std::uint32_t, 5> assigned{};
    std::vector<game::OwnedAbility> owned;
};

struct AAExpUpdate {
    std::uint32_t exp = 0;
    std::uint32_t points = 0;
    std::uint8_t percent = 0;
};

struct AATimer {
    std::uint32_t begin = 0;
    std::uint32_t timer = 0;
    std::uint32_t server_now = 0;
};

enum class AAOperation : std::uint8_t { Activate, SetExperiencePercent, DisableExperience, TrainRank, TrainAllRanks };
struct AAIntent {
    AAOperation operation = AAOperation::Activate;
    std::uint32_t rank_id = 0;
    std::uint32_t target_id = 0;
    std::uint32_t percent = 0;
};
} // namespace hailstone::net
