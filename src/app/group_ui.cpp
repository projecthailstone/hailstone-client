#include "app/group_ui.h"
#include "ui/lua/lua_host.h"

#include <algorithm>
#include <format>

#include "core/ascii_case.h"
#include "game/eq_names.h"
#include "game/spells.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "net/messages/group.h"
#include "net/messages/pet.h"

namespace hailstone::app {

std::string pet_buff_time_left(const game::BuffSlot& b, const game::SpellData* spell, bool zone_suspends_buffs,
                               std::int64_t now_ms) {
    if (b.tics <= 0) return {}; // none for a permanent buff (a negative count)
    const auto left_ms = game::buff_ms_left(b, spell, false, zone_suspends_buffs, now_ms);
    if (left_ms == 0) return {};
    const auto s = static_cast<std::int64_t>(left_ms / 1000);
    return s >= 60 ? std::format("{}m{:02}s", s / 60, s % 60) : std::format("{}s", s);
}

bool group_member_is(std::string_view member, std::string_view spawn) {
    // Group lists carry clean names; spawn names may carry EQEmu's trailing digits.
    std::string_view s = spawn;
    while (!s.empty() && s.back() >= '0' && s.back() <= '9') s.remove_suffix(1);
    return iequals(member, spawn) || iequals(member, s);
}

const game::Spawn* GroupMemberSpawns::find(const game::WorldState& world, const std::string& name,
                                           std::int64_t now_ms) {
    auto it = members_.find(name);
    if (it == members_.end()) it = members_.emplace(name, Entry{}).first;
    Entry& e = it->second;
    e.frame = frame_;
    if (e.id) {
        if (auto s = world.spawns.find(e.id);
            s != world.spawns.end() && !s->second.is_corpse && group_member_is(name, s->second.name))
            return &s->second;
        e.id = 0;
        e.looked = false; // gone (or died, or another spawn took the id): searched for again now
    }
    if (e.looked && e.spawns == world.spawns.size() && now_ms - e.looked_ms < 1000) return nullptr;
    ++scans_;
    e.looked = true;
    e.spawns = world.spawns.size();
    e.looked_ms = now_ms;
    for (const auto& [id, s] : world.spawns)
        if (!s.is_corpse && group_member_is(name, s.name)) {
            e.id = id;
            return &s;
        }
    return nullptr;
}

void GroupMemberSpawns::end() {
    std::erase_if(members_, [&](const auto& kv) { return kv.second.frame != frame_; });
}

void GroupUi::set_testing(const std::string& commands, bool accept_invites) {
    auto_commands_.clear();
    std::size_t start = 0;
    while (start <= commands.size() && !commands.empty()) {
        std::size_t end = commands.find(';', start);
        std::string line = commands.substr(start, end == std::string::npos ? std::string::npos : end - start);
        while (!line.empty() && line.front() == ' ') line.erase(line.begin());
        if (!line.empty()) auto_commands_.push_back(line);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    auto_accept_ = accept_invites;
}

void GroupUi::draw(net::GameConnection& conn) {
    const auto& world = conn.world();
    const auto& g = conn.group();

    // Testing aids.
    auto now = std::chrono::steady_clock::now();
    if (conn.state() != net::ConnectionState::InZone || world.player_id == 0) {
        in_zone_since_ = {};
    } else {
        if (in_zone_since_ == std::chrono::steady_clock::time_point{}) in_zone_since_ = now;
        if (auto_next_ < auto_commands_.size() && now - in_zone_since_ > std::chrono::seconds(5) &&
            now - last_command_ > std::chrono::seconds(3)) {
            conn.send_command(auto_commands_[auto_next_++]);
            last_command_ = now;
        }
        if (auto_accept_ && !g.inviter.empty()) {
            if (invite_seen_ == std::chrono::steady_clock::time_point{}) invite_seen_ = now;
            else if (now - invite_seen_ > std::chrono::seconds(2)) {
                conn.accept_group_invite();
                invite_seen_ = {};
            }
        }
    }

    // ---- group window (its members only while this window shows: the Lua GroupWindow reads the connection itself)
    group_.in_group = g.active;
    group_.inviter = g.inviter;
    group_.members.clear();
    const bool group_shown = group_.open && g.active && !ui::lua::replaces("GroupWindow");
    if (group_shown) {
        member_spawns_.begin();
        const std::int64_t now_ms = net::GameConnection::now_ms();
        for (const auto& m : g.slots) {
            if (m.name.empty()) continue;
            ui::GroupMemberView v;
            v.name = m.name;
            v.leader = g.is_leader(m.name);
            v.offline = m.offline;
            const game::Spawn* s = member_spawns_.find(world, m.name, now_ms);
            if (s) {
                v.spawn_id = s->id;
                v.level = s->level;
                v.self = s->id == world.player_id;
                v.targeted = s->id == world.target_id && world.target_id != 0;
                if (v.self) {
                    const auto& ps = conn.player_status();
                    const auto own = conn.own_hp(); // the items' HP included, as the player window's
                    v.hp_percent = own.max > 0 ? static_cast<int>(std::clamp<std::int64_t>(own.cur * 100 / own.max, 0, 100))
                                               : s->hp_percent;
                    if (ps.max_mana > 0) v.mana_percent = static_cast<int>(ps.mana * 100 / ps.max_mana);
                    if (ps.max_endurance > 0) v.endurance_percent = static_cast<int>(ps.endurance * 100 / ps.max_endurance);
                } else {
                    v.hp_percent = s->hp_percent;
                    if (auto it = g.mana_percent.find(s->id); it != g.mana_percent.end()) v.mana_percent = it->second;
                    if (auto it = g.endurance_percent.find(s->id); it != g.endurance_percent.end())
                        v.endurance_percent = it->second;
                }
            }
            group_.members.push_back(std::move(v));
        }
        member_spawns_.end();
    } else {
        member_spawns_.clear();
    }

    // ---- pet window (built only while it shows, as the group's)
    const auto& pets = conn.pets();
    const bool pet_shown = pet_.open && !ui::lua::replaces("PetInfoWindow");
    pet_.has_pet = false;
    pet_.pets.clear();
    pet_.buttons.clear();
    pet_.buffs.clear();
    if (auto it = world.spawns.find(pets.focused); pet_shown && pets.focused && it != world.spawns.end()) {
        const auto& p = it->second;
        pet_.has_pet = true;
        pet_.pet_id = p.id;
        pet_.name = game::display_name(p.name);
        pet_.level = p.level;
        pet_.hp_percent = p.hp_percent;
        pet_.target.clear();
        pet_.target_hp = -1;
        if (auto t = world.spawns.find(pets.pet_target); pets.pet_target && t != world.spawns.end()) {
            pet_.target = game::display_name(t->second.name);
            pet_.target_hp = t->second.hp_percent;
        }
        // The stock pet window's buttons; toggles show the server's
        // OP_PetCommandState (EQEmu PET_BUTTON_*).
        const auto& st = conn.pet_toggles();
        pet_.buttons = {
            {"Attack", net::kPetAttack},       {"Back off", net::kPetBackOff},       {"Health", net::kPetHealthReport},
            {"Follow", net::kPetFollow, st[3]}, {"Guard", net::kPetGuard, st[4]},     {"Sit", net::kPetSit, st[0]},
            {"Taunt", net::kPetTaunt, st[5]},  {"Hold", net::kPetHold, st[6]},       {"Focus", net::kPetFocus, st[8]},
            {"No cast", net::kPetSpellHold, st[9]}, {"Regroup", net::kPetRegroup, st[2]}, {"Get lost", net::kPetGetLost},
        };
        for (const auto& [slot, b] : conn.pet_buffs()) {
            const auto* sp = conn.spell_table().get(b.spell);
            pet_.buffs.push_back({sp ? sp->name : std::format("spell {}", b.spell),
                                  pet_buff_time_left(b, sp, conn.spell_state().zone_suspends_buffs,
                                                     net::GameConnection::now_ms())});
        }
    }
    for (const auto& e : pets.list) {
        if (!pet_shown) break;
        ui::PetListRowView row;
        row.spawn_id = e.spawn_id;
        if (auto it = world.spawns.find(e.spawn_id); it != world.spawns.end()) {
            row.name = game::display_name(it->second.name);
            row.hp_percent = it->second.hp_percent;
        }
        if (e.class_id) row.klass = game::class_name(e.class_id);
        row.focused = e.spawn_id == pets.focused;
        pet_.pets.push_back(std::move(row));
    }

    ui::GroupActions actions;
    if (group_shown) ui::group_window(group_, actions); // off while the Lua window runs
    // The modern pack's Group Invite popup (ui/modern/groupinvite.lua) claims "GroupInvite"; a pack without one
    // (gamepad) keeps this prompt, which its group window gives the pad's focus.
    if (!ui::lua::replaces("GroupInvite")) ui::invite_prompt(group_, actions);
    if (pet_shown) ui::pet_window(pet_, actions); // off while the Lua window runs

    if (actions.target_spawn) conn.set_target(*actions.target_spawn);
    if (actions.accept) conn.accept_group_invite();
    if (actions.invite) conn.group_invite();
    if (actions.decline) conn.decline_group_invite();
    if (actions.disband) conn.group_disband();
    if (actions.focus_pet) conn.focus_pet(*actions.focus_pet);
    if (actions.pet_command) {
        // The window's Attack sends the current target, as /pet attack does.
        if (*actions.pet_command == net::kPetAttack) conn.pet_command_words("attack");
        else conn.pet_command(*actions.pet_command);
    }
}

} // namespace hailstone::app
