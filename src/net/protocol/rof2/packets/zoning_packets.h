#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include "game/zoning.h"
namespace hailstone::net::rof2 {
// Wire decoding belongs to RoF2; game::zone_player_to_bind is only gameplay.
std::optional<game::ZoneToBindPacket> decode_zone_to_bind(std::span<const std::uint8_t> payload);
}
