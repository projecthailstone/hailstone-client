#pragma once
// Shared casting commands/events: neither wire opcodes nor packet buffers.
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "game/mechanics/cast_spell.h"

namespace hailstone::net::messages {
struct AuraUpdate {
    std::uint32_t action = 0, type = 0, spawn_id = 0, icon = 0;
    std::string name;
};


struct CastRequest { game::mechanics::cast::Packet cast; };
struct CastStarted {
    std::uint32_t spell_id = 0;
    std::uint16_t caster_id = 0;
    std::uint32_t cast_time = 0;
};
struct CastInterrupted {
    std::uint32_t spawn_id = 0;
    std::uint32_t message_id = 0;
    std::vector<std::string> args;
};
// Semantic spell strike: omit RoF2's unknown fields and inventory-slot bytes.
struct SpellAction {
    std::uint16_t target = 0, source = 0, level = 0;
    float instrument_mod = 1.0f, force = 0, hit_heading = 0, hit_pitch = 0;
    std::uint8_t type = 0;
    std::uint32_t damage = 0, spell_id = 0;
    std::uint16_t unknown31 = 0;
    std::uint8_t spell_level = 0, effect_flag = 0;
};
// A spell gem or book transaction is a typed operation in either direction.
struct MemorizationEvent { std::uint32_t slot = 0, spell_id = 0, scribing = 0, reduction = 0; };
inline constexpr std::uint32_t kSpellMemScribe = 0, kSpellMemMemorize = 1,
    kSpellMemForget = 2, kSpellMemRefresh = 3;
struct SpellBookDelete { std::int16_t book_slot = 0; bool successful = false; };
struct SpellBookSwap { std::uint32_t from_slot = 0, to_slot = 0; };
struct SpellBookSet { std::vector<int> forget_gems; };
struct BuffState {
    std::uint8_t effect_type = 0, level = 0;
    float bard_modifier = 1.0f;
    std::uint32_t spell_id = 0;
    std::int32_t duration = 0;
    std::uint32_t caster_id = 0, num_hits = 0;
    std::array<std::int32_t, 12> slot_data{};
};
struct BuffChange {
    std::uint32_t entity_id = 0;
    BuffState buff;
    std::uint32_t slot = 0, fade = 0;
};
struct BuffWindowEntry {
    std::uint32_t slot = 0, spell_id = 0;
    std::int32_t tics = 0;
    std::uint32_t num_hits = 0;
    std::string caster;
};
struct BuffWindow {
    std::uint32_t entity_id = 0, tic_timer = 0;
    bool all_buffs = false;
    std::vector<BuffWindowEntry> entries;
    std::uint8_t type = 0;
};
struct BuffRemoval { std::uint32_t slot = 0, entity_id = 0; };
struct BlockedSpellList {
    std::vector<std::int32_t> spells;
    bool pet = false, initialise = false;
    std::uint16_t flags = 0;
    std::optional<std::uint32_t> count;
};
struct ClearBlockedSpellList { bool pet = false; };
struct RestCombatStatus { bool in_combat = false; std::uint32_t downtime_seconds = 0; };
struct SpellVisualEffect {
    std::uint32_t effect = 0, caster = 0, target = 0, stage1_ms = 0;
    std::uint32_t stage1_length_ms = 0, stage2_length_ms = 0;
    bool full = false, add = false;
};
struct NimbusVisualEffect { std::uint32_t spawn_id = 0; std::int32_t effect = 0; };
struct SpellText { std::uint32_t color = 0; std::string text; };
struct HungerThirst { std::uint32_t food = 0, water = 0; };
struct LinkedSpellReuse { std::uint32_t timer_id = 0, end_time = 0, start_time = 0; };
struct DisciplineReuse { std::uint32_t timer_id = 0, seconds = 0; };
}

// Shared gameplay limits/actions, not RoF2 wire layouts.
namespace hailstone::net {
inline constexpr int kBlockedBuffSlots = 30;
inline constexpr std::uint32_t kAuraAdd = 0, kAuraRemove = 1, kAuraClear = 2;
} // namespace hailstone::net
