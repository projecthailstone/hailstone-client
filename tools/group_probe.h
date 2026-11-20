#pragma once

// playprobe's group and pet checks (included by tools/playprobe.cpp):
//   --group-accept 1     accept the first group invitation (2 s after it comes)
//   --group-decline 1    decline invitations instead
//   --group-invite NAME  /invite NAME 5 s after zone-in (again every 15 s
//                        until grouped); "nearest-player" targets and invites the
//                        nearest player-type spawn (NMS virtual players), skipping "Nmsc*"
//   --gsay TEXT          once grouped, say TEXT to the group
//   --group-leave S      S seconds after joining, /disband (leave or disband)
//   --pet-test 1         memorize and cast a pet spell (SPA 33 / 71) from the
//                        book, report the pet list and the focused pet, try
//                        /pet health, sit, follow, then send the pet at the
//                        nearest low-level NPC and report the fight
// Prints every group and pet packet with its decoded fields.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/combat.h"
#include "game/spells.h"
#include "net/connection/game_connection.h"
#include "net/protocol/rof2/packets/group_packets.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/packets/pet_packets.h"
#include "net/protocol/rof2/packets/spell_packets.h"

namespace hailstone::probe {

class GroupProbe {
public:
    using Clock = std::chrono::steady_clock;

    bool option(const char* name, const char* value) {
        if (!std::strcmp(name, "--group-accept")) accept_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--group-decline")) decline_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--group-invite")) invite_ = value;
        else if (!std::strcmp(name, "--gsay")) gsay_ = value;
        else if (!std::strcmp(name, "--group-leave")) leave_after_ = std::atoi(value);
        else if (!std::strcmp(name, "--pet-test")) pet_test_ = std::atoi(value) != 0;
        else return false;
        return true;
    }

    bool busy() const {
        return (pet_test_ && pet_phase_ != PetPhase::Done) || (!invite_.empty() && !grouped_once_) ||
               (leave_after_ > 0 && grouped_once_ && !left_);
    }

    void tap(const net::AppPacket& p) {
        net::rof2::Op op = net::rof2::from_wire(p.opcode);
        double t = std::chrono::duration<double>(Clock::now() - start_).count();
        switch (op) {
        case net::rof2::Op::GroupInvite:
        case net::rof2::Op::GroupInvite2:
            if (auto v = net::decode_group_invite(p))
                std::printf("  GROUP +%.1fs: OP_%s invitee '%s' inviter '%s' (%zu bytes)\n", t, net::rof2::op_name(op), v->invitee.c_str(), v->inviter.c_str(), p.data.size());
            break;
        case net::rof2::Op::GroupFollow:
        case net::rof2::Op::GroupFollow2:
            if (auto v = net::decode_group_follow(p))
                std::printf("  GROUP +%.1fs: OP_%s inviter '%s' invitee '%s'\n", t, net::rof2::op_name(op), v->inviter.c_str(), v->invitee.c_str());
            break;
        case net::rof2::Op::GroupUpdateB:
            if (auto v = net::decode_group_update_b(p)) {
                std::printf("  GROUP +%.1fs: OP_GroupUpdateB leader '%s', %zu member(s):", t, v->leader.c_str(), v->members.size());
                for (const auto& m : v->members) std::printf(" [%u '%s' L%u]", m.slot, m.name.c_str(), m.level);
                std::printf("\n");
            } else {
                std::printf("  GROUP +%.1fs: OP_GroupUpdateB UNDECODABLE (%zu bytes)\n", t, p.data.size());
            }
            break;
        case net::rof2::Op::GroupUpdate:
            if (auto v = net::decode_group_join(p))
                std::printf("  GROUP +%.1fs: OP_GroupUpdate member '%s' level %u (%zu bytes)\n", t, v->member_name.c_str(), v->level, p.data.size());
            break;
        case net::rof2::Op::GroupLeaderChange:
            if (auto v = net::decode_group_leader_change(p)) std::printf("  GROUP +%.1fs: OP_GroupLeaderChange '%s'\n", t, v->c_str());
            break;
        case net::rof2::Op::GroupDisbandYou:
        case net::rof2::Op::GroupDisbandOther:
            if (auto v = net::decode_group_generic(p))
                std::printf("  GROUP +%.1fs: OP_%s '%s' '%s'\n", t, net::rof2::op_name(op), v->name1.c_str(), v->name2.c_str());
            break;
        case net::rof2::Op::GroupAcknowledge:
        case net::rof2::Op::GroupLeadershipAAUpdate:
        case net::rof2::Op::GroupDelete:
            std::printf("  GROUP +%.1fs: OP_%s (%zu bytes)\n", t, net::rof2::op_name(op), p.data.size());
            break;
        case net::rof2::Op::MobManaUpdate:
        case net::rof2::Op::MobEnduranceUpdate:
            if (auto v = net::decode_mob_percent(p)) std::printf("  GROUP +%.1fs: OP_%s spawn %u %u%%\n", t, net::rof2::op_name(op), v->spawn_id, v->percent);
            break;
        case net::rof2::Op::MobHealth:
            if (auto v = net::decode_mob_health(p); v && (v->spawn_id != last_hp_id_ || v->percent != last_hp_)) {
                last_hp_id_ = v->spawn_id;
                last_hp_ = v->percent;
                std::printf("  HP +%.1fs: OP_MobHealth spawn %u %u%%\n", t, v->spawn_id, v->percent);
            }
            break;
        case net::rof2::Op::PetCommandState:
            if (auto v = net::decode_pet_command_state(p)) std::printf("  PET +%.1fs: OP_PetCommandState button %u state %u\n", t, v->button, v->state);
            break;
        case net::rof2::Op::PetHoTT:
            if (auto v = net::decode_pet_hott(p)) std::printf("  PET +%.1fs: OP_PetHoTT target %u\n", t, *v);
            break;
        case net::rof2::Op::PetBuffWindow:
            if (auto v = net::decode_buff_icons(p)) {
                std::printf("  PET +%.1fs: OP_PetBuffWindow pet %u:", t, v->entity_id);
                for (const auto& e : v->entries) std::printf(" [slot %u spell %d tics %u]", e.slot, static_cast<int>(e.spell_id), e.tics);
                std::printf("\n");
            }
            break;
        default:
            break;
        }
    }

    void zone_in(net::GameConnection&) {
        start_ = Clock::now();
        invited_at_ = {};
    }

    void step(net::GameConnection& conn) {
        auto now = Clock::now();
        const auto& g = conn.group();
        // Invitations.
        if (!g.inviter.empty()) {
            if (invite_seen_ == Clock::time_point{}) invite_seen_ = now;
            if (now - invite_seen_ > std::chrono::seconds(2)) {
                if (decline_) {
                    std::printf("  GROUP: declining %s's invitation\n", g.inviter.c_str());
                    conn.decline_group_invite();
                } else if (accept_) {
                    std::printf("  GROUP: accepting %s's invitation\n", g.inviter.c_str());
                    conn.accept_group_invite();
                }
                invite_seen_ = {};
            }
        }
        if (!invite_.empty() && !g.active && now - start_ > std::chrono::seconds(5) &&
            (invited_at_ == Clock::time_point{} || now - invited_at_ > std::chrono::seconds(15))) {
            std::string who = invite_;
            if (who == "nearest-player") {
                // The nearest other player-type spawn (NMS virtual players spawn as players),
                // skipping this project's own test characters ("Nmsc...").
                const auto& w = conn.world();
                const game::Spawn* me = w.player();
                float best = 1e30f;
                who.clear();
                for (const auto& [id, s] : w.spawns) {
                    if (s.is_npc || s.is_corpse || id == w.player_id || s.name.rfind("Nmsc", 0) == 0 || !me) continue;
                    float d = glm::distance(s.position, me->position);
                    if (d < best) { best = d; who = s.name; }
                }
                std::printf("  GROUP: nearest player-type spawn: '%s' (%.0f away)\n", who.c_str(), best);
                if (who.empty()) { invite_.clear(); return; }
                conn.send_command("/target " + who);
                who.clear(); // /invite with the target
            }
            std::printf("  GROUP: /invite %s\n", who.c_str());
            conn.send_command("/invite " + who);
            invited_at_ = now;
        }
        if (g.active && g.count() >= 2) {
            if (!grouped_once_) {
                grouped_once_ = true;
                grouped_at_ = now;
                report(conn, "grouped");
            }
            if (!gsay_.empty() && !gsaid_ && now - grouped_at_ > std::chrono::seconds(2)) {
                conn.send_command("/g " + gsay_);
                gsaid_ = true;
            }
            if (now - last_report_ > std::chrono::seconds(10)) report(conn, "status");
        }
        if (leave_after_ > 0 && grouped_once_ && !left_ && now - grouped_at_ > std::chrono::seconds(leave_after_)) {
            std::printf("  GROUP: /disband\n");
            conn.send_command("/disband");
            left_ = true;
        }
        if (pet_test_) pet_step(conn, now);
    }

    void chat(const std::string&) {}

    void finish(net::GameConnection& conn) {
        if (grouped_once_ || conn.group().active) report(conn, "final");
        if (pet_test_) std::printf("  PET RESULT: phase %d, pet %u, list %zu, attack target reached %s\n", static_cast<int>(pet_phase_),
                                   conn.pets().focused, conn.pets().list.size(), pet_engaged_ ? "yes" : "no");
    }

private:
    enum class PetPhase { Start, Memorizing, Casting, Waiting, Commands, Attack, Done };

    void report(net::GameConnection& conn, const char* what) {
        last_report_ = Clock::now();
        const auto& g = conn.group();
        std::printf("  GROUP %s: active %d leader '%s' members:", what, g.active ? 1 : 0, g.leader.c_str());
        for (const auto& m : g.slots) {
            if (m.name.empty()) continue;
            int hp = -1, mana = -1;
            for (const auto& [id, s] : conn.world().spawns)
                if (s.name == m.name || game::display_name(s.name) == m.name) {
                    hp = s.hp_percent;
                    if (auto it = g.mana_percent.find(id); it != g.mana_percent.end()) mana = it->second;
                }
            std::printf(" [%s hp %d%% mana %d%%]", m.name.c_str(), hp, mana);
        }
        std::printf("\n");
    }

    void pet_step(net::GameConnection& conn, Clock::time_point now) {
        if (now - start_ < std::chrono::seconds(4)) return;
        const auto& sp = conn.spell_state();
        const auto& table = conn.spell_table();
        auto is_pet_spell = [&](std::uint32_t id) {
            const auto* s = table.get(id);
            return s && (s->has_effect(33) || s->has_effect(71));
        };
        switch (pet_phase_) {
        case PetPhase::Start: {
            if (conn.pets().focused) {
                std::printf("  PET: already have pet %u\n", conn.pets().focused);
                pet_phase_ = PetPhase::Commands;
                phase_at_ = now;
                break;
            }
            for (int gem = 0; gem < game::kSpellGems; ++gem)
                if (is_pet_spell(sp.gems[static_cast<std::size_t>(gem)].spell)) pet_gem_ = gem;
            if (pet_gem_ < 0) {
                int slot = -1;
                for (std::size_t i = 0; i < sp.book.size(); ++i)
                    if (is_pet_spell(sp.book[i])) { slot = static_cast<int>(i); break; }
                if (slot < 0) {
                    std::printf("  PET: no pet spell in the book or gems; book:");
                    for (std::size_t i = 0; i < sp.book.size(); ++i)
                        if (sp.book[i] != game::kNoSpell && sp.book[i] != 0) {
                            const auto* s = table.get(sp.book[i]);
                            std::printf(" %s", s ? s->name.c_str() : "?");
                        }
                    std::printf("\n");
                    pet_phase_ = PetPhase::Done;
                    break;
                }
                pet_gem_ = 7;
                std::printf("  PET: memorizing %s (book slot %d) into gem %d\n", table.get(sp.book[static_cast<std::size_t>(slot)])->name.c_str(), slot, pet_gem_ + 1);
                conn.set_sitting(true);
                conn.memorize(slot, pet_gem_);
                pet_phase_ = PetPhase::Memorizing;
                phase_at_ = now;
                break;
            }
            pet_phase_ = PetPhase::Casting;
            phase_at_ = now - std::chrono::seconds(10);
            break;
        }
        case PetPhase::Memorizing:
            if (is_pet_spell(sp.gems[static_cast<std::size_t>(pet_gem_)].spell)) {
                std::printf("  PET: memorized\n");
                conn.set_sitting(false);
                pet_phase_ = PetPhase::Casting;
                phase_at_ = now - std::chrono::seconds(10);
            } else if (now - phase_at_ > std::chrono::seconds(30)) {
                std::printf("  PET: memorizing timed out\n");
                pet_phase_ = PetPhase::Done;
            }
            break;
        case PetPhase::Casting:
            if (conn.pets().focused) {
                pet_phase_ = PetPhase::Waiting;
                break;
            }
            if (now - phase_at_ > std::chrono::seconds(12)) {
                if (++casts_ > 3) {
                    std::printf("  PET: no pet after %d casts\n", casts_ - 1);
                    pet_phase_ = PetPhase::Done;
                    break;
                }
                const auto* s = table.get(sp.gems[static_cast<std::size_t>(pet_gem_)].spell);
                std::printf("  PET: casting %s (gem %d), mana %u\n", s ? s->name.c_str() : "?", pet_gem_ + 1, conn.player_status().mana);
                conn.set_sitting(false);
                conn.set_target(conn.world().player_id);
                conn.cast_gem(pet_gem_);
                phase_at_ = now;
            }
            break;
        case PetPhase::Waiting:
            if (auto it = conn.world().spawns.find(conn.pets().focused); it != conn.world().spawns.end()) {
                std::printf("  PET: pet %u '%s' L%u (owner %u), list %zu\n", it->first, it->second.name.c_str(), it->second.level,
                            it->second.pet_owner_id, conn.pets().list.size());
                pet_phase_ = PetPhase::Commands;
                phase_at_ = now;
            }
            break;
        case PetPhase::Commands: {
            static const char* const kWords[] = {"health", "sit", "sit off", "follow", "guard", "follow", "taunt"};
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - phase_at_).count();
            std::size_t want = static_cast<std::size_t>(secs / 2);
            while (cmd_index_ < want && cmd_index_ < std::size(kWords)) {
                std::printf("  PET: /pet %s\n", kWords[cmd_index_]);
                conn.send_command(std::string("/pet ") + kWords[cmd_index_]);
                ++cmd_index_;
            }
            if (cmd_index_ >= std::size(kWords) && secs > 16) {
                pet_phase_ = PetPhase::Attack;
                phase_at_ = now;
            }
            break;
        }
        case PetPhase::Attack: {
            const auto& w = conn.world();
            if (!foe_) {
                const game::Spawn* me = w.player();
                float best = 1e30f;
                for (const auto& [id, s] : w.spawns) {
                    if (!s.is_npc || s.is_corpse || s.pet_owner_id || s.level > 3 || !s.targetable) continue;
                    if (s.name.rfind("a_", 0) != 0 && s.name.rfind("an_", 0) != 0) continue;
                    float d = me ? glm::distance(s.position, me->position) : 0.0f;
                    if (d < best) { best = d; foe_ = id; }
                }
                if (!foe_) {
                    std::printf("  PET: no low-level creature to attack\n");
                    pet_phase_ = PetPhase::Done;
                    break;
                }
                std::printf("  PET: targeting %s (id %u, %.0f away), /pet attack\n", w.spawns.at(foe_).name.c_str(), foe_, best);
                conn.set_target(foe_);
                conn.send_command("/pet attack");
                phase_at_ = now;
                break;
            }
            auto it = w.spawns.find(foe_);
            if (conn.pets().pet_target == foe_ && !pet_engaged_) {
                pet_engaged_ = true;
                std::printf("  PET: the pet's target is now %u (OP_PetHoTT)\n", foe_);
            }
            if (it == w.spawns.end() || it->second.is_corpse) {
                std::printf("  PET: %u is dead after %.0f s\n", foe_, std::chrono::duration<double>(now - phase_at_).count());
                conn.send_command("/pet back off");
                pet_phase_ = PetPhase::Done;
            } else if (now - phase_at_ > std::chrono::seconds(90)) {
                std::printf("  PET: gave up; foe at %u%%\n", it->second.hp_percent);
                conn.send_command("/pet back off");
                pet_phase_ = PetPhase::Done;
            }
            break;
        }
        case PetPhase::Done:
            break;
        }
    }

    bool accept_ = false, decline_ = false, pet_test_ = false;
    std::string invite_, gsay_;
    int leave_after_ = 0;
    bool grouped_once_ = false, gsaid_ = false, left_ = false;
    Clock::time_point start_ = Clock::now(), invited_at_{}, invite_seen_{}, grouped_at_{}, last_report_{};
    std::uint16_t last_hp_id_ = 0;
    std::uint8_t last_hp_ = 0;
    PetPhase pet_phase_ = PetPhase::Start;
    Clock::time_point phase_at_{};
    int pet_gem_ = -1, casts_ = 0;
    std::size_t cmd_index_ = 0;
    std::uint32_t foe_ = 0;
    bool pet_engaged_ = false;
};

} // namespace hailstone::probe
