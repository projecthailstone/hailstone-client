#pragma once
// Findable NPC values, independent of the source protocol.
#include <cstdint>
#include <string>
namespace hailstone::net {
struct FindableNpc {
    bool remove = false;
    std::uint32_t spawn_id = 0;
    std::string name, last_name;
    std::uint32_t race = 0;
    std::uint8_t class_id = 0;
};
}
