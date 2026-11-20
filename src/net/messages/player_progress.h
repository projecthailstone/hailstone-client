#pragma once
// Shared game data decoded from progress, veteran and PvP messages.
// Wire size, ordering and opcode mapping belong to protocol codecs.
#include <cstdint>
#include <string>
#include <vector>

namespace hailstone::net {
inline constexpr std::uint32_t kVetClaimList = 0xffffffff;

struct PvPEntry {
    std::string name;
    std::uint32_t level = 0, race = 0, class_ = 0, zone = 0, time = 0, points = 0;
};

struct PvPStats {
    std::uint32_t kills = 0, deaths = 0, points = 0, career_points = 0, best_streak = 0, worst_streak = 0,
                  current_streak = 0, infamy = 0, vitality = 0;
    PvPEntry last_death, last_kill;
    std::vector<PvPEntry> recent_kills; // the named ones
};

struct PvPLeader {
    std::string name;
    std::uint32_t kills = 0, deaths = 0, points = 0, infamy = 0;
};

struct PvPLeaderboard {
    std::uint32_t kills = 0, points = 0, rank = 0, deaths = 0, infamy = 0;
    std::vector<PvPLeader> rows; // the named ones
};

struct CrystalCounts {
    std::uint32_t radiant = 0, radiant_career = 0, ebon = 0, ebon_career = 0;
};

struct VetRewardItem {
    std::string name;
    std::uint32_t item_id = 0, charges = 0;
};

struct VetReward {
    std::uint32_t claim_id = 0, available = 0;
    bool enabled = false;
    std::vector<VetRewardItem> items;
};

struct VetClaimReply {
    std::uint32_t claim_id = 0;
    bool claimed = false;
};

} // namespace hailstone::net
