#pragma once

// The membership feature refusals (GameFeatureRestrictions in the original): a window or command whose feature the
// membership does not allow (GetGameFeature(feature) == 0) prints the line GameFeatureRestrictions_FormatMessage
// @0x7dbd10 builds, in colour 0x14f (Cmd_buyer @0x4e6f60 with 0xf, CBazaarWnd::AboutToShow @0x63c2a0 with 0xe,
// CMailCompositionWnd::AboutToShow @0x6c1910 with 6).

#include <cstdint>
#include <optional>
#include <string>

#include "net/messages/world.h"

namespace hailstone::game {

inline constexpr std::uint32_t kFeatureRefusedColor = 0x14f;
inline constexpr std::int32_t kFeatureSendMail = 6;       // "ability to send mail"
inline constexpr std::int32_t kFeatureBazaarSell = 0xe;   // "ability to sell in the bazaar"
inline constexpr std::int32_t kFeatureBuyLines = 0xf;     // "ability to create buy lines in the bazaar"

// GameFeatureRestrictions_FindLevel @0x7db9c0 with (feature, 1, true, true): the first membership level 0..2 (free,
// silver, gold) whose setting for the feature is 1, above 1 or -1 (unlimited); -1 when none. Only each level's first
// setting for the feature counts.
std::int32_t feature_membership_level(std::int32_t feature, const std::optional<net::MembershipDetails>& details);

// GameFeatureRestrictions_FormatMessage(feature, 1, 1, 1): "Your <what> has/have been disabled/limited until you
// purchase this feature." without a level that allows it, else "... because you do not have at least a <level>
// membership." (FreeToPlayClient::RestrictionInfo @0xa0aa28: the feature's text, its "have" and "disabled" flags.)
// Empty for a feature past the table.
std::string feature_refused_message(std::int32_t feature, const std::optional<net::MembershipDetails>& details);

} // namespace hailstone::game
