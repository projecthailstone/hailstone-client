// Racial combat abilities follow the player's current form (including illusions).
#pragma once

#include <cstdint>

#include "game/mechanics/character.h"
#include "net/game_connection.h"

namespace hailstone::app {

inline std::uint32_t skill_race(const net::GameConnection& conn, const game::mechanics::Character& ch) {
    const game::Spawn* player = conn.world().player();
    return player && player->race ? player->race : ch.sheet().race;
}

} // namespace hailstone::app
