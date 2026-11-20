#pragma once

// Protocol-independent gameplay values extracted from the RoF2 packet declarations.
// Application codecs translate these values to their own wire layouts.
// Serial lengths, packet opcodes and binary read/write remain in protocol/rof2/.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hailstone::net {

inline constexpr std::uint32_t kAchievementRootCategory = 0xffffffffu;

// Upper bound for achievement metadata embedded in chat links (independent of wire framing).
inline constexpr std::size_t kMaxAchievementLinkBytes = 65536;

inline constexpr std::size_t kMaxAchievementCategories = 8192, kMaxAchievementDefinitions = 65536;

inline constexpr std::size_t kMaxAchievementComponents = 262144, kMaxAchievementComponentsPerDefinition = 4096;

inline constexpr std::size_t kMaxAchievementAssociations = 262144, kMaxAchievementRequirements = 262144;

enum class AchievementStatus : std::int16_t { Completed = 0, Open = 1, Locked = 2, Hidden = 3 };

struct AchievementAssociation {
    std::uint32_t achievement_id = 0;
    std::string text;
    std::uint32_t display_order = 0;
};

struct AchievementCategory {
    std::uint32_t id = 0, parent_id = kAchievementRootCategory;
    std::string name, description, icon;
    std::uint32_t display_order = 0;
    std::vector<AchievementAssociation> associations;
    std::vector<std::uint32_t> child_ids;
};

struct AchievementComponent {
    std::uint32_t id = 0;
    std::uint8_t type = 0;
    std::uint32_t required_count = 0;
    std::string name, description;
    // This byte is presentation only. Progress carries a separate full-width requirement ID.
    std::uint8_t display_order = 0;
};

struct AchievementDefinition {
    std::uint32_t id = 0;
    std::string name, description;
    std::uint32_t icon_id = 0, version = 0;
    bool persistent = false;
    std::array<std::vector<AchievementComponent>, 4> components;
    std::array<std::unordered_map<std::uint32_t, std::size_t>, 4> component_indices;
    std::uint32_t points = 0;
    bool has_reward = false;
};

struct AchievementCatalog {
    std::vector<AchievementCategory> categories;
    std::vector<AchievementDefinition> definitions;
    std::unordered_map<std::uint32_t, std::size_t> definition_indices, category_indices;
    const AchievementDefinition* find(std::uint32_t id) const;
};

struct AchievementState {
    AchievementStatus status = AchievementStatus::Open;
    // Bucket 3 is definition-only and remains unavailable. The runtime wire order is 1,2,0.
    std::array<std::vector<std::uint8_t>, 4> satisfied;
    std::optional<std::uint32_t> completed_at;
    // Primary/snapshot packets do not carry counts. A later progress initialization or
    // an inspect/link snapshot supplies them; satisfaction bits never imply a count.
    std::array<std::vector<std::optional<std::uint64_t>>, 4> counts;
};

struct AchievementStateUpdate {
    std::uint32_t definition_index = 0;
    AchievementState state;
};

struct AchievementPrimary {
    std::uint32_t serial = 0;
    bool dense = false;
    std::vector<AchievementStateUpdate> updates;
};

struct AchievementProgress {
    std::uint32_t achievement_id = 0, component_id = 0, requirement_id = 0, requirement_type = 0;
    std::uint32_t current_count = 0;
};

struct AchievementInspect {
    std::string player_name;
    std::uint32_t achievement_id = 0;
    AchievementState state;
    std::uint8_t trailing = 0;
};

struct AchievementLink {
    std::string player_name;
    std::uint32_t achievement_id = 0;
    AchievementState state;
    std::string metadata; // received caret-delimited values, ending in '^', no chat envelope
};

struct AchievementEarned {
    std::uint32_t spawn_id = 0, achievement_id = 0, sound_id = 0;
    AchievementLink link;
};

struct AchievementGuild {
    std::string player_name;
    std::uint32_t achievement_id = 0;
    AchievementLink link;
};

struct AchievementWindow {};
struct AchievementCompareRequest { std::uint32_t target = 0, player = 0; };
struct AchievementRewardView { std::uint32_t definition_index = 0; };

// Achievement chat-link metadata is semantic data, not an application packet.
// Keep parsing and presentation independent of the selected network protocol.
bool valid_achievement_player_name(std::string_view value);
std::optional<AchievementLink> parse_achievement_link(std::string_view metadata,
                                                       const AchievementCatalog& catalog);
std::optional<AchievementLink> make_achievement_link(std::string_view player_name,
                                                    const AchievementDefinition& definition,
                                                    const AchievementState& state);


} // namespace hailstone::net
