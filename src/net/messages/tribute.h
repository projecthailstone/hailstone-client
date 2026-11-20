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
#include "net/messages/item.h"

namespace hailstone::net {

inline constexpr int kTributeSlots = 5;         // RoF2 invtype::TRIBUTE_SIZE

inline constexpr int kTributeTiers = 10;        // MAX_TRIBUTE_TIERS

inline constexpr std::uint32_t kTributeNone = 0xffffffff;

// OP_TributeInfo (zone → client, RoF2 TributeAbility_Struct): u32 id, u32 tier count (both big-endian), 10 × {u32
// level, u32 item id, u32 cost} (big-endian), u32, the name.
struct TributeTier {
    std::uint32_t level = 0, item_id = 0, cost = 0;
};

struct TributeAbility {
    std::uint32_t id = 0;
    std::uint32_t tier_count = 0;
    std::array<TributeTier, kTributeTiers> tiers{};
    std::string name;
    // The tiers the server has: EQEmu sends its tributes table's "unknown" column as the tier count
    // (SendTributes), so the count is the leading tiers with a level, item or cost.
    std::uint32_t tiers_used() const;
};

// OP_TributeUpdate, both ways (48 bytes, TributeInfo_Struct): u32 active, 5 × u32 tribute id (0xffffffff none),
// 5 × u32 tier, u32 tribute master id. The client sends it to change its benefits; the server answers with its own.
struct TributeSettings {
    bool active = false;
    std::array<std::uint32_t, kTributeSlots> tributes{kTributeNone, kTributeNone, kTributeNone, kTributeNone, kTributeNone};
    std::array<std::uint32_t, kTributeSlots> tiers{};
    std::uint32_t master_id = 0;
};

struct TributePoints {
    std::int32_t points = 0, career = 0;
};

// OP_OpenTributeMaster, both ways (12 bytes, StartTribute_Struct): u32 our spawn id, u32 master id, u32 response
// (the server's: 1 open, 0 refused).
struct StartTribute {
    std::uint32_t client_id = 0, master_id = 0, response = 0;
};

struct TributeDescription {
    std::uint32_t tribute_id = 0;
    std::string text;
};

// OP_TributeItem, both ways (24 bytes, RoF2 TributeItem_Struct): the item's slot (12 B), u32 quantity, u32 master
// id, i32 favour (the server's answer: what the donation gave). OP_TributeMoney (12 bytes): u32 platinum, u32
// master id, i32 favour.
struct TributeItem {
    ItemSlot slot = ItemSlot::invalid();
    std::uint32_t quantity = 0, master_id = 0;
    std::int32_t points = 0;
};

struct TributeMoney {
    std::uint32_t platinum = 0, master_id = 0;
    std::int32_t points = 0;
};

// ---- guild tribute (EQEmu zone/tribute.cpp, client_packet.cpp Handle_OP_GuildTribute*, worldserver.cpp; not
// translated for RoF2 but for the item donation). A guild has two benefits.

inline constexpr int kGuildTributeSlots = 2;

// The guild's benefits: OP_GuildSendActiveTributes (at zone-in and on a change) and OP_GuildTributeToggleReply
// (on activation), 40 bytes: u32 guild, u32 favour, u32 timer (ms), u32 on, 8, 2 × u32 tribute id, 2 × u32 tier
// (id 0xffffffff none).
struct GuildTributeActive {
    std::uint32_t favor = 0, timer_ms = 0;
    bool enabled = false;
    std::array<std::uint32_t, kGuildTributeSlots> tributes{kTributeNone, kTributeNone};
    std::array<std::uint32_t, kGuildTributeSlots> tiers{};
    // The leading DWORD: toggle replies identify the guild; SendGuildActiveTributes may leave it zero.
    std::uint32_t guild_id = 0;
};

// OP_GuildTributeFavorAndTimer (Z→C, 16 bytes): u32 guild, u32 favour, u32 timer (ms), u32 trophy timer.
struct GuildTributeFavor {
    std::uint32_t favor = 0, timer_ms = 0;
};

struct GuildTributeOpt {
    std::string member;
    bool on = false, trophy_on = false;
    std::uint32_t donations = 0, time = 0, command = 0;
};

struct GuildTributeDonation {
    ItemSlot slot = ItemSlot::invalid();
    std::uint32_t quantity = 0, favor = 0;
};

struct GuildTributePlatinum {
    std::uint32_t platinum = 0, favor = 0;
};


// Requests are semantic; the RoF2 codec alone selects guild versus personal wire encodings.
struct TributeSelection { std::uint32_t player = 0, tribute = 0, tier = 0; };
struct GuildTributeSave { std::uint32_t master = 0; GuildTributeActive benefits; bool modify = false; };
struct GuildTributeMember { std::uint32_t guild = 0; std::string member; bool participating = false; bool trophy = false; };
struct GuildTributeItem { ItemSlot slot = ItemSlot::invalid(); std::uint32_t quantity = 0, master = 0, guild = 0; };
struct GuildTributeCoins { std::uint32_t platinum = 0, master = 0, guild = 0; };

} // namespace hailstone::net
