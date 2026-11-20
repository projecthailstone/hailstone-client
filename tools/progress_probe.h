#pragma once

// playprobe's checks of the progress systems (included by tools/playprobe.cpp):
//   --train TEXT     walks (straight, at run speed, through walls) to the guildmaster whose name
//                    contains TEXT ("any": the nearest guildmaster of one of our classes), opens
//                    the training window, prints its rows, trains --train-skill ID (default: the
//                    first row the checks allow) and closes the window
//   --aa 1           prints the AA table, points and visible / trainable ranks, sets the AA
//                    experience to 50 %, then back to 0, and with --aa-buy ID trains a rank
//                    (--aa-buy-all 1: as the AA window's "buy every rank" box, then waits 10 s)
//   --task TEXT      walks to the NPC whose name contains TEXT, hails it, accepts the first
//                    task it offers and prints the task journal; --task-history 1 asks for the
//                    first completed task's elements
//   --aa-use TEXT    activates the owned ability whose name contains TEXT (e.g. "Bazaar and Back")
//   --task-say WORD  with --task: says WORD to the NPC instead of hailing it (EQEmu task selectors
//                    often answer a keyword)
//   --task-cancel ID abandons the active task ID and prints the journal after the server's answer
//   --combine SLOT   combines the bag in general slot SLOT (23..32; 0: the first bag with items)
//   --object 1       walks to the nearest tradeskill ground object, opens it and presses Combine
//                    (an empty container: the client refuses with eqstr 12025); --object-item TEXT
//                    first moves the general-slot item whose name contains TEXT into its slot 1;
//                    --object-take 1 instead takes the item in its slot 1 back into the inventory

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <glm/geometric.hpp>

#include "game/coords.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "net/protocol/rof2/opcode_map.h"

namespace hailstone::probe {

class ProgressProbe {
public:
    using Clock = std::chrono::steady_clock;

    bool option(const char* name, const char* value) {
        if (!std::strcmp(name, "--train")) train_ = value;
        else if (!std::strcmp(name, "--train-skill")) train_skill_ = std::atoi(value);
        else if (!std::strcmp(name, "--aa")) aa_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--aa-buy")) aa_buy_ = static_cast<std::uint32_t>(std::atoi(value));
        else if (!std::strcmp(name, "--aa-buy-all")) aa_buy_all_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--task")) task_ = value;
        else if (!std::strcmp(name, "--task-say")) task_say_ = value;
        else if (!std::strcmp(name, "--aa-use")) aa_use_ = value;
        else if (!std::strcmp(name, "--task-cancel")) task_cancel_ = static_cast<std::uint32_t>(std::atoi(value));
        else if (!std::strcmp(name, "--task-history")) task_history_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--combine")) combine_ = std::atoi(value);
        else if (!std::strcmp(name, "--object")) object_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--object-item")) object_item_ = value;
        else if (!std::strcmp(name, "--object-take")) object_take_ = std::atoi(value) != 0;
        else return false;
        return true;
    }

    void tap(const net::AppPacket& p) {
        if (!active()) return;
        net::rof2::Op op = net::rof2::from_wire(p.opcode);
        switch (op) {
        case net::rof2::Op::RespondAA: case net::rof2::Op::AAExpUpdate: case net::rof2::Op::AAAction: case net::rof2::Op::ClearAA:
        case net::rof2::Op::GMTraining: case net::rof2::Op::GMTrainSkillConfirm:
        case net::rof2::Op::TradeSkillCombine: case net::rof2::Op::ClearObject: case net::rof2::Op::ClickObjectAction:
        case net::rof2::Op::TaskDescription: case net::rof2::Op::TaskActivity: case net::rof2::Op::CompletedTasks:
        case net::rof2::Op::TaskSelectWindow: case net::rof2::Op::SharedTaskSelectWindow: case net::rof2::Op::TaskHistoryReply:
        case net::rof2::Op::TaskActivityComplete: case net::rof2::Op::CancelTask: case net::rof2::Op::TaskRequestTimer:
        case net::rof2::Op::SkillUpdate: case net::rof2::Op::LevelUpdate: case net::rof2::Op::ExpUpdate: {
            std::printf("  PROGRESS: got %s (%zu bytes):", net::rof2::op_name(op), p.data.size());
            for (std::size_t i = 0; i < p.data.size() && i < 24; ++i) std::printf(" %02x", std::to_integer<unsigned>(p.data[i]));
            std::printf("\n");
            break;
        }
        case net::rof2::Op::SendAATable: ++aa_ranks_seen_; break;
        default: break;
        }
    }

    void zone_in(net::GameConnection& conn) {
        if (!active()) return;
        if (const auto* me = conn.world().player()) pos_ = me->position;
        phase_ = Phase::Start;
        since_ = Clock::now();
    }

    bool busy() const { return active() && phase_ != Phase::Done; }

    void step(net::GameConnection& conn) {
        if (!active() || phase_ == Phase::Done || phase_ == Phase::Idle) return;
        auto now = Clock::now();
        float dt = std::min(0.1f, std::chrono::duration<float>(now - last_step_).count());
        last_step_ = now;
        if (now - since_ > std::chrono::seconds(70)) {
            std::printf("  PROGRESS: gave up in phase %d\n", static_cast<int>(phase_));
            phase_ = Phase::Done;
            return;
        }
        const auto& w = conn.world();
        switch (phase_) {
        case Phase::Start: {
            if (now - since_ < std::chrono::seconds(4)) return;
            if (task_cancel_) {
                print_tasks(conn);
                for (const auto& t : conn.tasks().tasks)
                    if (t.task_id == task_cancel_) {
                        std::printf("  PROGRESS: cancel_task(type %u, slot %u)\n", t.type, t.sequence);
                        conn.cancel_task(t.type, t.sequence);
                        break;
                    }
                wait(Phase::TaskJournal, now, 3);
                return;
            }
            if (!aa_use_.empty()) {
                if (aa_used_) {
                    phase_ = Phase::Done;
                    return;
                }
                aa_used_ = true;
                for (const auto& o : conn.alt_adv().owned) {
                    const game::AltAbility* a = conn.alt_adv().get(o.id);
                    if (!a || conn.aa_name(*a).find(aa_use_) == std::string::npos) continue;
                    auto r = conn.aa_readiness(a->id);
                    std::printf("  PROGRESS: activating '%s' (rank %u, group %d, spell %d, timer %d): ready %d, %d s left\n",
                                conn.aa_name(*a).c_str(), a->id, a->group, a->spell, a->timers.empty() ? -1 : a->timers[0],
                                r.ready, r.remaining);
                    std::printf("  PROGRESS: aa_activate -> %d\n", conn.aa_activate(static_cast<std::uint32_t>(a->group)));
                    wait(Phase::WaitEnd, now, 20);
                    return;
                }
                std::printf("  PROGRESS: no owned ability named \"%s\"\n", aa_use_.c_str());
                phase_ = Phase::Done;
                return;
            }
            if (aa_) {
                print_aa(conn);
                conn.set_aa_percent(50);
                wait(Phase::AAHalf, now, 3);
                return;
            }
            if (combine_ >= 0 && !object_) {
                combine(conn);
                wait(Phase::Done, now, 4);
                phase_ = Phase::WaitEnd;
                return;
            }
            if (object_) {
                const game::GroundObject* best = nullptr;
                for (const auto& o : conn.ground_objects())
                    if (o.type > 1 && (!best || glm::distance(o.position, pos_) < glm::distance(best->position, pos_))) best = &o;
                if (!best) {
                    std::printf("  PROGRESS: no tradeskill object here\n");
                    phase_ = Phase::Done;
                    return;
                }
                object_id_ = best->drop_id;
                walk_to_ = best->position;
                std::printf("  PROGRESS: walking to object %u %s (type %d) %.0f units away\n", best->drop_id,
                            best->name.c_str(), best->type, glm::distance(best->position, pos_));
                phase_ = Phase::Walk;
                return;
            }
            const std::string& want = !train_.empty() ? train_ : task_;
            target_ = find(conn, want, !train_.empty());
            if (!target_) {
                std::printf("  PROGRESS: nobody named \"%s\" here\n", want.c_str());
                phase_ = Phase::Done;
                return;
            }
            const auto& s = w.spawns.at(target_);
            walk_to_ = s.position;
            std::printf("  PROGRESS: walking to %s (id %u, class %u) %.0f units away\n", s.name.c_str(), target_,
                        s.class_id, glm::distance(s.position, pos_));
            phase_ = Phase::Walk;
            return;
        }
        case Phase::Walk: {
            glm::vec3 to = walk_to_;
            if (target_) {
                auto it = w.spawns.find(target_);
                if (it == w.spawns.end()) { phase_ = Phase::Done; return; }
                to = it->second.position;
            }
            glm::vec3 d = to - pos_;
            d.z = 0;
            float len = glm::length(d);
            float reach = object_ ? 6.0f : 8.0f;
            glm::vec3 v(0.0f);
            if (len > reach) {
                v = d / len * 30.0f;
                pos_ += len - reach > 30.0f * dt ? v * dt : d / len * (len - reach);
            }
            pos_.z = to.z;
            conn.set_player_motion(pos_, game::heading_to(pos_, to), v, len > reach);
            if (len <= reach + 0.5f) {
                conn.set_player_motion(pos_, game::heading_to(pos_, to), glm::vec3(0.0f), false);
                if (target_) conn.set_target(target_);
                wait(Phase::Arrived, now, 2);
            }
            return;
        }
        case Phase::Arrived:
            if (now < wait_until_) return;
            if (!train_.empty()) {
                std::printf("  PROGRESS: open_trainer -> %d\n", conn.open_trainer(target_));
                wait(Phase::Training, now, 3);
            } else if (!task_.empty()) {
                if (task_say_.empty()) conn.hail();
                else conn.say(task_say_);
                wait(Phase::TaskOffered, now, 4);
            } else if (object_) {
                std::printf("  PROGRESS: click_object -> %d\n", conn.click_object(object_id_));
                wait(Phase::ObjectOpen, now, 3);
            }
            return;
        case Phase::Training: {
            if (now < wait_until_) return;
            const auto& t = conn.training();
            if (!t) {
                std::printf("  PROGRESS: no training window\n");
                phase_ = Phase::Done;
                return;
            }
            std::printf("  PROGRESS: training with %s: %zu rows, %d practice points\n", t->trainer_name.c_str(),
                        t->rows.size(), conn.practice_points());
            for (std::size_t i = 0; i < t->rows.size(); ++i) {
                const auto& r = t->rows[i];
                auto id = static_cast<std::uint32_t>(r.kind);
                std::printf("    row %zu: %s (%d) %d/%d cost %d\n", i, r.language() ? "language" : conn.skill_name(id).c_str(),
                            r.kind, conn.skill_value(id), r.cap, conn.train_cost(i));
            }
            bool sent = false;
            for (std::size_t i = 0; i < t->rows.size() && !sent; ++i) {
                if (train_skill_ >= 0 && t->rows[i].kind != train_skill_) continue;
                sent = conn.train(i);
                if (sent) std::printf("  PROGRESS: trained row %zu (kind %d)\n", i, t->rows[i].kind);
            }
            wait(Phase::Trained, now, 3);
            return;
        }
        case Phase::Trained:
            if (now < wait_until_) return;
            std::printf("  PROGRESS: %d practice points left; closing\n", conn.practice_points());
            conn.close_trainer();
            wait(Phase::WaitEnd, now, 2);
            return;
        case Phase::AAHalf:
            if (now < wait_until_) return;
            std::printf("  PROGRESS: AA percent %u\n", conn.alt_adv().percent);
            if (aa_buy_)
                std::printf("  PROGRESS: aa_train(%u%s) -> %d\n", aa_buy_, aa_buy_all_ ? ", all" : "",
                            conn.aa_train(aa_buy_, aa_buy_all_));
            conn.set_aa_percent(0);
            wait(Phase::AAOff, now, aa_buy_all_ ? 10 : 3);
            return;
        case Phase::AAOff:
            if (now < wait_until_) return;
            print_aa(conn);
            if (const game::AltAbility* b = aa_buy_ ? conn.alt_adv().get(aa_buy_) : nullptr) {
                const game::AltAbility* o = conn.alt_adv().owned_rank(b->group);
                std::printf("  PROGRESS: '%s' now owned at rank %d of %d (rank id %u)\n", conn.aa_name(*b).c_str(),
                            o ? o->rank : 0, b->max_rank, o ? o->id : 0);
            }
            phase_ = Phase::Done;
            return;
        case Phase::TaskOffered: {
            if (now < wait_until_) return;
            if (const auto& o = conn.tasks().offers) {
                std::printf("  PROGRESS: %zu offers from %u (type %u%s)\n", o->offers.size(), o->giver_id, o->type,
                            o->shared ? ", shared" : "");
                for (const auto& t : o->offers) {
                    std::printf("    task %u '%s' (duration %u)\n", t.task_id, t.title.c_str(), t.duration);
                    for (const auto& e : t.elements) std::printf("      %s\n", conn.task_element_text(e).c_str());
                }
                if (!o->offers.empty()) conn.accept_task(o->offers.front().task_id);
            } else {
                std::printf("  PROGRESS: no task offered\n");
            }
            wait(Phase::TaskJournal, now, 4);
            return;
        }
        case Phase::TaskJournal:
            if (now < wait_until_) return;
            print_tasks(conn);
            if (task_history_ && !conn.tasks().completed.empty()) {
                conn.request_task_history(0);
                wait(Phase::TaskHistory, now, 3);
            } else {
                phase_ = Phase::Done;
            }
            return;
        case Phase::TaskHistory:
            if (now < wait_until_) return;
            for (const auto& [index, elements] : conn.tasks().history) {
                std::printf("  PROGRESS: history of %u: %zu elements\n", index, elements.size());
                for (const auto& e : elements) std::printf("      %s\n", conn.task_element_text(e).c_str());
            }
            phase_ = Phase::Done;
            return;
        case Phase::ObjectOpen:
            if (now < wait_until_) return;
            if (const auto& c = conn.world_container()) {
                std::printf("  PROGRESS: container %u '%s' type %u open\n", c->drop_id, c->name.c_str(), c->type);
                if (object_take_ && move_step_ < 2) {
                    if (move_step_++ == 0) {
                        const net::Item* in = conn.inventory().at(net::ItemSlot{net::kInvWorld, 0, -1, -1, 0, 0});
                        std::printf("  PROGRESS: taking %s\n", in ? in->name.c_str() : "nothing");
                        conn.click_slot(net::ItemSlot{net::kInvWorld, 0, -1, -1, 0, 0}, true);
                    } else {
                        conn.auto_inventory();
                    }
                    wait(Phase::ObjectOpen, now, 2);
                    return;
                }
                if (object_take_) {
                    conn.close_world_container();
                    wait(Phase::WaitEnd, now, 2);
                    return;
                }
                if (!object_item_.empty() && move_step_ < 2) {
                    if (move_step_++ == 0) {
                        for (std::int16_t sl = net::kSlotGeneral1; sl <= net::kSlotGeneral10; ++sl) {
                            const net::Item* it = conn.inventory().at(net::ItemSlot::possessions(sl));
                            if (!it || it->name.find(object_item_) == std::string::npos) continue;
                            std::printf("  PROGRESS: picking up %s\n", it->name.c_str());
                            conn.click_slot(net::ItemSlot::possessions(sl), true);
                            break;
                        }
                    } else {
                        const net::Item* cur = conn.inventory().cursor();
                        std::printf("  PROGRESS: putting %s into the container\n", cur ? cur->name.c_str() : "nothing");
                        conn.click_slot(net::ItemSlot{net::kInvWorld, 0, -1, -1, 0, 0});
                    }
                    wait(Phase::ObjectOpen, now, 2);
                    return;
                }
                if (const net::Item* in = conn.inventory().at(net::ItemSlot{net::kInvWorld, 0, -1, -1, 0, 0}))
                    std::printf("  PROGRESS: world slot 1 holds %s\n", in->name.c_str());
                std::printf("  PROGRESS: combine\n");
                conn.combine_world_container();
                wait(Phase::ObjectClose, now, 3);
            } else {
                std::printf("  PROGRESS: the container did not open\n");
                phase_ = Phase::Done;
            }
            return;
        case Phase::ObjectClose:
            if (now < wait_until_) return;
            std::printf("  PROGRESS: combine pending %d; closing\n", conn.combine_pending());
            conn.close_world_container();
            wait(Phase::WaitEnd, now, 2);
            return;
        case Phase::WaitEnd:
            if (now >= wait_until_) phase_ = Phase::Done;
            return;
        default:
            return;
        }
    }

    void finish(const net::GameConnection& conn) {
        if (!active()) return;
        std::printf("  PROGRESS: %d AA ranks received; %d practice points; combine pending %d\n", aa_ranks_seen_,
                    conn.practice_points(), conn.combine_pending());
    }

private:
    enum class Phase { Idle, Start, Walk, Arrived, Training, Trained, AAHalf, AAOff, TaskOffered, TaskJournal,
                       TaskHistory, ObjectOpen, ObjectClose, WaitEnd, Done };

    bool active() const { return !train_.empty() || aa_ || !aa_use_.empty() || !task_.empty() || task_cancel_ || combine_ >= 0 || object_; }

    void wait(Phase next, Clock::time_point now, int seconds) {
        phase_ = next;
        wait_until_ = now + std::chrono::seconds(seconds);
    }

    std::uint32_t find(const net::GameConnection& conn, const std::string& want, bool guildmaster) const {
        std::uint32_t best = 0;
        float best_d = 1e30f;
        std::uint32_t mask = conn.player_class_mask();
        for (const auto& [id, s] : conn.world().spawns) {
            if (id == conn.world().player_id || s.is_corpse || !s.is_npc) continue;
            if (guildmaster) {
                if (s.class_id < 20 || s.class_id > 35) continue;
                bool own = conn.profile() && conn.profile()->class_ + 19u == s.class_id;
                if (want == "any" && !own && !(mask & (1u << (s.class_id - 20)))) continue;
            }
            if (want != "any" && s.name.find(want) == std::string::npos) continue;
            float d = glm::distance(s.position, pos_);
            if (d < best_d) { best_d = d; best = id; }
        }
        return best;
    }

    void combine(net::GameConnection& conn) {
        for (const auto& [key, it] : conn.inventory().items())
            if (key.first == net::kInvPossessions)
                std::printf("    slot %d: %s%s (bag type %u, %zu items)\n", key.second, it.name.c_str(), it.is_bag() ? " [bag]" : "",
                            it.bag_type, it.contents.size());
        for (std::int16_t s = net::kSlotGeneral1; s <= net::kSlotGeneral10; ++s) {
            if (combine_ > 0 && s != combine_) continue;
            const net::Item* it = conn.inventory().at(net::ItemSlot::possessions(s));
            if (!it || !it->is_bag() || (combine_ == 0 && it->contents.empty())) continue;
            std::printf("  PROGRESS: combining %s (bag type %u, %zu items) in slot %d\n", it->name.c_str(), it->bag_type,
                        it->contents.size(), s);
            std::printf("  PROGRESS: combine_container -> %d\n", conn.combine_container(s));
            return;
        }
        std::printf("  PROGRESS: no bag to combine\n");
    }

    void print_aa(const net::GameConnection& conn) const {
        const auto& aa = conn.alt_adv();
        std::uint8_t cls = conn.profile() ? conn.profile()->class_ : 0;
        auto visible = aa.visible(0, cls);
        int trainable = 0;
        for (const game::AltAbility* a : visible)
            if (conn.aa_can_train(a->id)) ++trainable;
        std::printf("  PROGRESS: AA table %zu ranks, %zu owned, %u points, %u spent, exp %u/330, %u%%; %zu visible, %d trainable\n",
                    aa.table.size(), aa.owned.size(), aa.points, aa.spent, aa.exp, aa.percent, visible.size(), trainable);
        int shown = 0;
        for (const game::AltAbility* a : visible) {
            if (++shown > 8) break;
            std::printf("    rank %u '%s' group %d rank %d/%d cost %d level %d type %d%s\n", a->id, conn.aa_name(*a).c_str(),
                        a->group, a->rank, a->max_rank, a->cost, a->level_req, a->type,
                        conn.aa_can_train(a->id) ? " trainable" : "");
        }
    }

    void print_tasks(const net::GameConnection& conn) const {
        const auto& ts = conn.tasks();
        std::printf("  PROGRESS: %zu active tasks, %zu completed\n", ts.tasks.size(), ts.completed.size());
        for (const auto& t : ts.tasks) {
            std::printf("    task %u '%s' type %u slot %u, %zu elements\n", t.task_id, t.title.c_str(), t.type, t.sequence,
                        t.elements.size());
            for (const auto& [id, e] : t.elements)
                std::printf("      [%u] %s %d/%d%s\n", id, e.hidden ? "? ? ?" : conn.task_element_text(e).c_str(), e.done_count,
                            e.goal_count, e.optional ? " (optional)" : "");
        }
    }

    std::string train_, task_, task_say_, aa_use_;
    bool aa_used_ = false;
    int move_step_ = 0;
    bool object_take_ = false;
    std::string object_item_;
    std::uint32_t task_cancel_ = 0;
    int train_skill_ = -1;
    bool aa_ = false, task_history_ = false, object_ = false;
    std::uint32_t aa_buy_ = 0;
    bool aa_buy_all_ = false;
    int combine_ = -1;
    int aa_ranks_seen_ = 0;
    Phase phase_ = Phase::Idle;
    Clock::time_point since_{}, wait_until_{}, last_step_{};
    glm::vec3 pos_{0.0f}, walk_to_{0.0f};
    std::uint32_t target_ = 0, object_id_ = 0;
};

} // namespace hailstone::probe
