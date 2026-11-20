#pragma once
// Server ability notifications used by gameplay and the Actions UI. These
// are values; only RoF2 knows the wire layout of the skill response.
#include <cstdint>
#include "game/mechanics/abilities.h"
namespace hailstone::net {
struct CoinFind { std::uint32_t kind = 0, amount = 0; };
struct BindWoundResult { std::uint16_t outcome = 0; };
}
