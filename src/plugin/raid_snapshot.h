#pragma once

// The raid roster in the shape get_raid_data_bulk and the hailstone Lua module give it. One pass over the zone's
// spawns indexes the players, mercenaries and pets; then one pass over the roster resolves each member, so a plugin
// or script does not run a TLO expression per member and does not scan the zone per member.
//
// The data is the client's own: game::RaidState for the roster and roles, game::GroupState for the mana and
// endurance percentages it receives for group members, and the world's spawns for HP, position and the pet. A
// member outside this zone has no spawn: spawn_id 0 and the percentages -1.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/ascii_case.h"
#include "game/social.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "hailstone_plugin.h"

namespace hailstone::plugin {

struct RaidMemberSnapshot {
    std::string name;
    std::uint32_t spawn_id = 0;
    std::int32_t group = -1;
    std::uint32_t flags = 0;
    std::int32_t level = 0;
    std::int32_t class_id = 0;
    std::int32_t pct_hp = -1;
    std::int32_t pct_mana = -1;
    std::int32_t pct_endurance = -1;
    std::uint32_t pet_spawn_id = 0;
    std::int32_t pet_pct_hp = -1;
    float x = 0, y = 0, z = 0, distance = 0;
};

struct RaidSnapshot {
    bool active = false;
    bool locked = false;
    std::uint32_t loot_type = 1;
    std::optional<std::size_t> leader;
    std::optional<std::size_t> target;
    std::vector<RaidMemberSnapshot> members;
};

namespace detail {

struct NameHash {
    std::size_t operator()(std::string_view name) const noexcept {
        std::size_t h = 1469598103934665603ull;
        for (char c : name) {
            h ^= static_cast<unsigned char>(ascii_lower(c));
            h *= 1099511628211ull;
        }
        return h;
    }
};

struct NameEqual {
    bool operator()(std::string_view a, std::string_view b) const noexcept { return iequals(a, b); }
};

// A raid member is a player or a mercenary, never an ordinary NPC: the name index leaves NPCs (and pets) out, so a
// roster name cannot pick up a guard or a pet with the same name. The pet index is every live spawn with an owner
// except a mercenary and a temporary pet: EQEmu sends a mercenary with its owner in petOwnerId (Mob::FillSpawnStruct,
// Merc::SetMerc), and a swarm pet is not the member's pet either, as the group window's pet rule has it
// (GameConnection's own-pet tracking skips temp_pet too). Without that, a member with a pet and a mercenary up would
// get whichever the map met first.
struct SpawnIndex {
    std::unordered_map<std::string_view, const game::Spawn*, NameHash, NameEqual> by_name;
    std::unordered_map<std::uint32_t, const game::Spawn*> pet_of;
    std::vector<const game::Spawn*> mercenaries;
};

inline SpawnIndex index_spawns(const net::GameConnection& c) {
    SpawnIndex index;
    index.by_name.reserve(c.world().spawns.size());
    index.pet_of.reserve(c.world().spawns.size());
    for (const auto& entry : c.world().spawns) {
        const game::Spawn& s = entry.second;
        if (s.is_corpse) continue;
        if (s.pet_owner_id && !s.mercenary && !s.temp_pet) index.pet_of.try_emplace(s.pet_owner_id, &s);
        if (s.is_npc && !s.mercenary) continue;
        index.by_name.try_emplace(s.name, &s);
        if (s.mercenary) index.mercenaries.push_back(&s);
    }
    return index;
}

inline const game::Spawn* find_raid_spawn(const SpawnIndex& index, std::string_view name) {
    const auto it = index.by_name.find(name);
    if (it != index.by_name.end()) return it->second;
    // A mercenary's roster name is its clean name; only mercenaries get this fallback.
    for (const game::Spawn* m : index.mercenaries)
        if (game::display_name_iequals(m->name, name)) return m;
    return nullptr;
}

inline bool listed_looter(const game::RaidState& r, std::string_view name) {
    for (const auto& n : r.looters)
        if (!n.empty() && iequals(n, name)) return true;
    return false;
}

} // namespace detail

inline RaidSnapshot build_raid_snapshot(const net::GameConnection& c) {
    RaidSnapshot out;
    const game::RaidState& raid = c.raid();
    out.active = raid.active;
    out.locked = raid.locked;
    out.loot_type = raid.loot_type;
    const std::uint32_t player_id = c.world().player_id;
    const detail::SpawnIndex index = detail::index_spawns(c);
    // Before world.player_id is known the player's spawn can already be in the world: find it by the character name.
    const game::Spawn* self = c.world().player();
    if (!self && player_id == 0) {
        const auto it = index.by_name.find(c.character_name());
        if (it != index.by_name.end()) self = it->second;
    }
    out.members.reserve(raid.members.size());
    for (std::size_t i = 0; i < raid.members.size(); ++i) {
        const game::RaidMember& m = raid.members[i];
        RaidMemberSnapshot s;
        s.name = m.name;
        const game::Spawn* spawn = detail::find_raid_spawn(index, m.name);
        // The player's own spawn, or their roster name when the spawn is not in the world yet or world.player_id is
        // not known yet. world.player_id is the same id the extended target read uses.
        const bool is_me = spawn && player_id ? spawn->id == player_id : iequals(m.name, c.character_name());
        if (spawn) {
            s.spawn_id = spawn->id;
            s.x = spawn->position.x;
            s.y = spawn->position.y;
            s.z = spawn->position.z;
            if (self) {
                const float dx = spawn->position.x - self->position.x;
                const float dy = spawn->position.y - self->position.y;
                const float dz = spawn->position.z - self->position.z;
                s.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            // The pet: the pet window's for the player, else the first spawn it owns (one pass built the index).
            if (is_me && c.pets().focused) {
                s.pet_spawn_id = c.pets().focused;
            } else {
                const auto pet = index.pet_of.find(spawn->id);
                if (pet != index.pet_of.end()) s.pet_spawn_id = pet->second->id;
            }
            if (s.pet_spawn_id) {
                const auto pet = c.world().spawns.find(s.pet_spawn_id);
                if (pet != c.world().spawns.end()) s.pet_pct_hp = static_cast<std::int32_t>(pet->second.hp_percent);
            }
        }
        // The player's own HP includes the items' HP (GameConnection::own_hp); others have only the percent the
        // server sent. -1 when even that is unknown.
        if (is_me) {
            const auto hp = c.own_hp();
            s.pct_hp = hp.max > 0 ? static_cast<std::int32_t>(std::max<std::int64_t>(0, hp.cur * 100 / hp.max)) : -1;
        } else if (spawn) {
            s.pct_hp = static_cast<std::int32_t>(spawn->hp_percent);
        }
        // The wire's group number is the server's; 0..11 are the raid groups, anything else is not one.
        s.group = m.group < 12 ? static_cast<std::int32_t>(m.group) : -1;
        s.level = m.level ? static_cast<std::int32_t>(m.level) : (spawn ? static_cast<std::int32_t>(spawn->level) : 0);
        s.class_id = m.class_ ? static_cast<std::int32_t>(m.class_) : (spawn ? static_cast<std::int32_t>(spawn->class_id) : 0);

        // Mana and endurance: the player's own, or the group update the client keeps for a group member. A raid
        // member outside the player's group has neither.
        if (is_me) {
            const auto& ps = c.player_status();
            // In 64 bits, as the HP line: a server past RoF2's limits can send pools where cur * 100 overflows 32.
            const auto percent = [](std::uint32_t cur, std::uint32_t max) {
                return max ? static_cast<std::int32_t>(std::int64_t{cur} * 100 / std::int64_t{max}) : -1;
            };
            s.pct_mana = percent(ps.mana, ps.max_mana);
            s.pct_endurance = percent(ps.endurance, ps.max_endurance);
        } else if (spawn) {
            const auto mana = c.group().mana_percent.find(spawn->id);
            if (mana != c.group().mana_percent.end()) s.pct_mana = mana->second;
            const auto endurance = c.group().endurance_percent.find(spawn->id);
            if (endurance != c.group().endurance_percent.end()) s.pct_endurance = endurance->second;
        }

        const bool leader = iequals(m.name, raid.leader);
        bool main_assist = m.main_assist;
        if (!main_assist)
            for (const auto& name : c.leadership().raid_assist_names)
                if (!name.empty() && iequals(name, m.name)) {
                    main_assist = true;
                    break;
                }
        // The raid window's loot types: 1 Leader Only, 2 Leader and Group Leader, 3 Leader Selected, 4 Entire Raid.
        bool looter = leader || (raid.loot_type == 2 && m.group_leader);
        if (!looter && raid.loot_type == 3) looter = detail::listed_looter(raid, m.name);
        if (raid.loot_type == 4) looter = true;
        if (leader) s.flags |= HS_BULK_RAID_LEADER;
        if (m.group_leader) s.flags |= HS_BULK_RAID_GROUP_LEADER;
        if (main_assist) s.flags |= HS_BULK_RAID_MAIN_ASSIST;
        if (m.marker) s.flags |= HS_BULK_RAID_MARKER;
        if (m.waypointer) s.flags |= HS_BULK_RAID_WAYPOINTER;
        if (looter) s.flags |= HS_BULK_RAID_LOOTER;
        if (spawn) s.flags |= HS_BULK_RAID_PRESENT;
        if (is_me) s.flags |= HS_BULK_RAID_ME;
        out.members.push_back(std::move(s));
    }
    for (std::size_t i = 0; i < raid.members.size(); ++i) {
        if (!out.leader && iequals(raid.members[i].name, raid.leader)) out.leader = i;
        if (!out.target && iequals(raid.members[i].name, raid.target)) out.target = i;
    }
    return out;
}

} // namespace hailstone::plugin
