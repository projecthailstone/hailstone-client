#pragma once
// Values shared with the gameplay layer when an optional server extension is enabled.
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
namespace hailstone::net {
struct HailstoneSettings { std::uint32_t flags = 0; };
inline constexpr std::uint8_t kQuestNpcItems = 1, kQuestNpcTalks = 2;

struct QuestNpc {
    std::string name; // npc_types' name ("Guard_Gehnus")
    std::uint8_t flags = 0;
};

struct ZoneLinks {
    std::uint16_t flags = 0;
    std::vector<std::pair<std::uint16_t, std::uint16_t>> links; // from, to
    bool has_zones = false;          // the allowed zones' part came
    std::int16_t expansion = -1;     // the server's current expansion (-1 all)
    std::vector<std::uint16_t> zones; // the zones it allows
};


}
