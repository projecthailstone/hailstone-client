#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/banner_appearance.h"

namespace hailstone::net {

inline constexpr std::uint32_t kNoBannerKit = 0xffffffffu;
inline constexpr std::size_t kMaxOwnedBannerKits = 32;
inline constexpr std::size_t kMaxGuildBannerKits = 1023;
inline constexpr std::uint8_t kGuildBannerKitItemType = 58;
inline constexpr unsigned kLastGuildBannerFilter = 31; // bit index in the category mask

struct GuildBannerStyle {
    std::uint32_t background = 0, emblem = 0;
    std::uint32_t background_color = 0xffffffffu, emblem_color = 0xffffffffu;
    bool operator==(const GuildBannerStyle&) const = default;
};

struct GuildBannerKit {
    std::uint32_t id = 0;
    std::string name;
    std::uint32_t description_id = 0;
    bool available = false;
};

struct GuildBannerKitList {
    std::uint32_t background_count = 0, emblem_count = 0;
    std::vector<GuildBannerKit> kits;
};

struct GuildBannerStatus {
    std::uint32_t kind = 0;
    std::uint32_t kit_id = kNoBannerKit, planted_kit_id = kNoBannerKit;
    std::uint32_t description_id = 0;
    std::int32_t remaining_ms = 0;
    std::string name;
    GuildBannerStyle style;

    bool active() const { return kit_id != kNoBannerKit; }
    bool can_remove() const { return active() && planted_kit_id != kNoBannerKit; }
};

struct GuildBannerModificationToken {
    std::uint32_t item_id = kNoBannerKit;
    std::string item_name;
};

struct GuildBannerState : GuildBannerKitList {
    std::optional<GuildBannerStatus> status;
    GuildBannerModificationToken modification_token;
    bool have_kits = false;

    bool received() const { return have_kits || status.has_value(); }
};

// Server-provided banner visuals applied to an existing NPC spawn.
struct GuildBannerAppearance {
    std::uint32_t spawn_id = 0;
    core::BannerAppearance appearance;
    std::string name;
};

// Outgoing banner operations are typed commands, not encoded packets.
struct GuildBannerOpen { std::uint32_t guild_id = 0, character_id = 0; };
struct GuildBannerKitRequest {
    std::uint32_t character_id = 0;
    bool show_all = false;
    unsigned filter = 0;
    std::vector<std::uint32_t> owned;
};
struct GuildBannerStatusRequest { std::uint32_t guild_id = 0, character_id = 0; };
struct GuildBannerSave { std::uint32_t character_id = 0; GuildBannerStyle style; std::uint32_t token_id = 0; };
struct GuildBannerPlant {
    std::uint32_t kit_id = 0;
    bool from_list = false;
    float x = 0, y = 0, z = 0;
    bool reapply_style = false;
};
struct GuildBannerRemove { std::uint32_t guild_id = 0; };

} // namespace hailstone::net
