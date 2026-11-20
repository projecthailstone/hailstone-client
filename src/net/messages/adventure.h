#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace hailstone::net {
inline constexpr int kAdventureThemes=6;
inline constexpr int kAdventureLeaderboardRows=100;
struct AdventureData {
    bool assigned = false;
    std::string text;
    std::uint32_t time_to_enter = 0, time_left = 0, risk = 0;
    float x = 0, y = 0, z = 0;
    bool compass = false;
};

struct AdventureCount {
    std::uint32_t current = 0, total = 0;
};

struct AdventureFinish {
    bool won = false;
    std::uint32_t points = 0;
};

struct AdventurePoints {
    std::uint32_t available = 0;
    std::array<std::uint32_t, kAdventureThemes - 1> theme{};
};

struct AdventureStats {
    std::array<std::uint32_t, kAdventureThemes> success{}, failure{}, rank{}, rank_percent{};
};

struct AdventureLeader {
    std::string name;
    std::uint32_t success = 0, failure = 0;
    std::uint32_t position = 0; // one-based wire slot; empty slots still count toward competition ranks
};

struct AdventureLeaderboard {
    std::uint32_t success = 0, failure = 0, rank = 0;
    std::vector<AdventureLeader> rows;
};
struct AdventureRequestIntent { std::uint32_t risk=0,npc=0,type=0; };
struct AdventureLeaderboardIntent { std::uint32_t order=0,theme=0,risk=0; };
} // namespace hailstone::net
