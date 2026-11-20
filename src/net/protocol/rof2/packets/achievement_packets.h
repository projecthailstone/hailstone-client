#pragma once

// Stock RoF2 achievement catalog, primary progress, comparisons and links. The catalog
// vector order is part of the protocol: state updates and View Reward use a definition
// index; earned notifications, links and counters use an achievement ID.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "net/wire/app_packet.h"

#include "net/messages/achievement.h"

namespace hailstone::net {

inline constexpr std::uint16_t kOpAchievementDefinitions = 0xdab0, kOpAchievementPrimary = 0x618f;
inline constexpr std::uint16_t kOpAchievementProgress = 0x2b42, kOpAchievementSnapshot = 0x059d;
inline constexpr std::uint16_t kOpAchievementInspect = 0x28e0, kOpAchievementEarned = 0x17f8;
inline constexpr std::uint16_t kOpAchievementWindow = 0x171e, kOpAchievementWindowRequest = 0x47cb;
inline constexpr std::uint16_t kOpAchievementLinkRequest = 0x675d;

// Resource ceilings are host policy, not claims about native container capacities.
inline constexpr std::size_t kMaxAchievementPacketBytes = 8u << 20;
inline constexpr std::size_t kMaxAchievementCatalogBytes = 64u << 20;


std::optional<AchievementCatalog> decode_achievement_catalog(const AppPacket& p);
std::optional<AchievementPrimary> decode_achievement_primary(const AppPacket& p,
                                                           const AchievementCatalog* catalog = nullptr);
std::optional<std::vector<AchievementProgress>> decode_achievement_progress(const AppPacket& p);
std::optional<std::vector<AchievementState>> decode_achievement_snapshot(const AppPacket& p,
                                                                       const AchievementCatalog* catalog = nullptr);
std::optional<AchievementInspect> decode_achievement_inspect(const AppPacket& p,
                                                           const AchievementCatalog* catalog = nullptr);
std::optional<AchievementEarned> decode_achievement_earned(const AppPacket& p,
                                                         const AchievementCatalog* catalog = nullptr);
std::optional<AchievementGuild> decode_achievement_guild(const AppPacket& p,
                                                        const AchievementCatalog* catalog = nullptr);
std::optional<AchievementWindow> decode_achievement_window(const AppPacket& p);

AppPacket encode_achievement_window_request(); // the native sender uses the one byte 0x30
AppPacket encode_achievement_link_request(const AchievementLink& link);

} // namespace hailstone::net
