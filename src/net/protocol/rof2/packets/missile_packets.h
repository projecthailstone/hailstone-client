#pragma once
#include <cstddef>
#include <optional>
#include <span>
#include "game/missiles.h"

namespace hailstone::net::rof2 {
// RoF2's 116-byte missile event is decoded at the application codec boundary.
std::optional<game::MissilePacket> decode_missile_packet(std::span<const std::byte> bytes);
}
