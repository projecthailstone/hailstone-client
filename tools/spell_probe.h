#pragma once

// playprobe's spell checks (included by tools/playprobe.cpp):
//   --spells 1         after zone-in: print the spell book, gems and buffs;
//                      memorize the first book spell when no gem holds one;
//                      cast gem 1 (on yourself; self-target spells need none),
//                      report the cast bar, the server's packets and the
//                      recast; then cast again and /stopcast it, then cast and
//                      move to see the server's interrupt; then forget and
//                      re-memorize a gem
//   --spell-gem N      cast gem N instead of gem 1
//   --spell-med S      afterwards sit for S seconds and report the mana regen
//   --spell-mem-timeout S  how long a memorise may take (default 60 s)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "game/spells.h"
#include "net/connection/game_connection.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/packets/spell_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

namespace hailstone::probe {

class SpellProbe {
public:
    using Clock = std::chrono::steady_clock;

    bool option(const char* name, const char* value) {
        if (!std::strcmp(name, "--spells")) enabled_ = std::atoi(value) != 0;
        else if (!std::strcmp(name, "--spell-gem")) gem_ = std::max(0, std::atoi(value) - 1);
        else if (!std::strcmp(name, "--spell-med")) med_seconds_ = std::atoi(value);
        else if (!std::strcmp(name, "--spell-mem-timeout")) mem_timeout_ = std::atoi(value);
        else return false;
        return true;
    }

    bool busy() const { return enabled_ && phase_ != Phase::Done; }

    void tap(const net::AppPacket& p) {
        if (!enabled_ || phase_ == Phase::Idle) return;
        net::rof2::Op op = net::rof2::from_wire(p.opcode);
        double t = std::chrono::duration<double>(Clock::now() - mark_).count();
        switch (op) {
        case net::rof2::Op::BeginCast:
            if (auto b = net::decode_begin_cast(p)) std::printf("  SPELLS +%.2fs: OP_BeginCast spell %u caster %u time %u ms\n", t, b->spell_id, b->caster_id, b->cast_time);
            break;
        case net::rof2::Op::ManaChange:
            if (auto m = net::decode_mana_change(p))
                std::printf("  SPELLS +%.2fs: OP_ManaChange mana %u end %u spell %d keep %u slot %d\n", t, m->mana, m->endurance,
                            static_cast<int>(m->spell_id), m->keep_casting, m->slot);
            break;
        case net::rof2::Op::MemorizeSpell:
            if (auto m = net::decode_memorize_spell(p))
                std::printf("  SPELLS +%.2fs: OP_MemorizeSpell slot %u spell %u scribing %u reduction %u\n", t, m->slot, m->spell_id, m->scribing, m->reduction);
            break;
        case net::rof2::Op::InterruptCast:
            if (auto ic = net::decode_interrupt_cast(p))
                std::printf("  SPELLS +%.2fs: OP_InterruptCast spawn %u message %u (%zu args)\n", t, ic->spawn_id, ic->message_id, ic->args.size());
            break;
        case net::rof2::Op::Buff:
            if (auto b = net::decode_buff(p))
                std::printf("  SPELLS +%.2fs: OP_Buff entity %u slot %u spell %d tics %u fade %u\n", t, b->entity_id, b->slot,
                            static_cast<int>(b->buff.spell_id), b->buff.duration, b->fade);
            break;
        case net::rof2::Op::BuffCreate:
        case net::rof2::Op::TargetBuffs:
            if (auto b = net::decode_buff_icons(p)) {
                std::printf("  SPELLS +%.2fs: OP_%s entity %u all %u type %u:", t, net::rof2::op_name(op), b->entity_id, b->all_buffs, b->type);
                for (const auto& e : b->entries) std::printf(" [slot %u spell %d tics %u '%s']", e.slot, static_cast<int>(e.spell_id), e.tics, e.caster.c_str());
                std::printf("\n");
            }
            break;
        case net::rof2::Op::Action:
            if (auto a = net::decode_action(p))
                std::printf("  SPELLS +%.2fs: OP_Action %u -> %u type %u spell %u flag %u level %u\n", t, a->source, a->target, a->type, a->spell_id, a->effect_flag, a->level);
            break;
        case net::rof2::Op::Damage:
            if (auto d = net::decode_combat_damage(p); d && d->type == 231)
                std::printf("  SPELLS +%.2fs: OP_Damage spell %u %u -> %u damage %d\n", t, d->spell_id, d->source, d->target, d->damage);
            break;
        case net::rof2::Op::LinkedReuse:
        case net::rof2::Op::ColoredText:
        case net::rof2::Op::Stamina:
        case net::rof2::Op::SpellEffect:
            std::printf("  SPELLS +%.2fs: OP_%s (%zu bytes)\n", t, net::rof2::op_name(op), p.data.size());
            break;
        default:
            break;
        }
    }

    void chat(const std::string& text) {
        if (!enabled_) return;
        last_chat_ = text;
        if (text.find("interrupted") != std::string::npos) ++interrupts_;
        if (text.find("fizzle") != std::string::npos) ++fizzles_;
    }

    void zone_in(net::GameConnection& conn) {
        if (enabled_ && phase_ != Phase::Idle) { // a later zone-in: the gems as the new profile has them
            std::printf("  SPELLS: zoned; ");
            print_gems(conn);
            return;
        }
        if (!enabled_ || phase_ != Phase::Idle) return;
        const auto& st = conn.spell_state();
        const auto& table = conn.spell_table();
        const auto& ps = conn.player_status();
        std::printf("  SPELLS: %zu spells in spells_us.txt; level %u, class mask 0x%x, profile class %u, spawn class %u, mana %u/%u, usable gems %d\n",
                    table.size(), ps.level, conn.player_class_mask(), conn.profile() ? conn.profile()->class_ : 0,
                    conn.world().player() ? conn.world().player()->class_id : 0, ps.mana, ps.max_mana, conn.usable_gems());
        int shown = 0;
        for (int slot = 0; slot < game::kSpellBookSize; ++slot) {
            auto id = st.book[static_cast<std::size_t>(slot)];
            if (id == game::kNoSpell || id == 0) continue;
            if (book_first_ < 0) book_first_ = slot;
            if (shown++ < 12) {
                const auto* s = table.get(id);
                std::printf("    book %d: %u %s (cast %u ms, mana %d, target type %u)\n", slot, id, s ? s->name.c_str() : "?",
                            s ? s->cast_time : 0, s ? s->mana : 0, s ? s->target_type : 0);
            }
        }
        std::printf("  SPELLS: %d spells in the book\n", shown);
        print_gems(conn);
        print_buffs(conn);
        phase_ = Phase::Settle;
        mark_ = Clock::now();
    }

    void step(net::GameConnection& conn) {
        if (!enabled_ || phase_ == Phase::Idle || phase_ == Phase::Done) return;
        auto now = Clock::now();
        auto since = [&](double s) { return now - mark_ > std::chrono::duration<double>(s); };
        const auto& st = conn.spell_state();
        const auto gem_spell = st.gems[static_cast<std::size_t>(gem_)].spell;
        switch (phase_) {
        case Phase::Settle:
            if (!since(3)) break;
            if (book_first_ < 0 && gem_spell == game::kNoSpell && find_scroll(conn) >= 0) {
                std::printf("  SPELLS: the book is empty; scribing the scrolls in the inventory (sitting)\n");
                conn.set_sitting(true);
                next(Phase::Scribe);
                break;
            }
            if (gem_spell == game::kNoSpell) {
                if (!conn.sitting()) conn.set_sitting(true); // the book works sitting
                if (book_first_ < 0) {
                    std::printf("  SPELLS: no spell in the book and none memorized; nothing to cast\n");
                    phase_ = Phase::Done;
                    break;
                }
                std::printf("  SPELLS: memorizing book slot %d into gem %d\n", book_first_, gem_ + 1);
                print_formula(conn, st.book[static_cast<std::size_t>(book_first_)]);
                conn.memorize(book_first_, gem_);
                next(Phase::Memorize);
            } else {
                next(Phase::Cast);
            }
            break;
        case Phase::Scribe: {
            // One scroll at a time: pick it up, scribe it, wait for the book entry.
            if (st.scribing || st.pending > 0 || !since(1)) break;
            if (conn.cursor_scroll_spell()) {
                if (!scribe_sent_) {
                    conn.scribe_cursor(-1);
                    scribe_sent_ = true;
                    scribe_mark_ = now;
                } else if (now - scribe_mark_ > std::chrono::seconds(10)) {
                    std::printf("  SPELLS RESULT: scribing got no answer\n");
                    next(Phase::Settle);
                    book_first_ = 0;
                }
                break;
            }
            scribe_sent_ = false;
            int slot = find_scroll(conn);
            if (slot < 0) {
                book_first_ = -1;
                for (int i = 0; i < game::kSpellBookSize; ++i) {
                    auto id = st.book[static_cast<std::size_t>(i)];
                    if (id != game::kNoSpell && id != 0) {
                        if (book_first_ < 0) book_first_ = i;
                        std::printf("  SPELLS RESULT: book slot %d now holds %s\n", i, name(conn, id).c_str());
                    }
                }
                next(Phase::Settle);
                break;
            }
            std::printf("  SPELLS: picking up the scroll in general slot %d\n", slot - 22);
            conn.click_slot(net::ItemSlot::possessions(static_cast<std::int16_t>(slot)));
            mark_ = now;
            break;
        }
        case Phase::Memorize:
            if (gem_spell != game::kNoSpell) {
                std::printf("  SPELLS RESULT: memorized %s into gem %d after %.1f s\n", name(conn, gem_spell).c_str(), gem_ + 1,
                            std::chrono::duration<double>(now - mark_).count());
                next(Phase::Cast);
            } else if (since(mem_timeout_)) {
                std::printf("  SPELLS RESULT: memorizing timed out\n");
                phase_ = Phase::Done;
            }
            break;
        case Phase::Cast:
            if (!wait_ready(conn, now)) break;
            target_for(conn, gem_spell);
            mana_before_ = conn.player_status().mana;
            std::printf("  SPELLS: casting %s from gem %d (mana %u)\n", name(conn, gem_spell).c_str(), gem_ + 1, mana_before_);
            conn.cast_gem(gem_);
            cast_bar_ = st.casting.casting() ? st.casting.cast_time : 0;
            next(Phase::CastWait);
            break;
        case Phase::CastWait:
            if (!st.casting.casting() && st.pending == 0 && since(0.5)) {
                const auto& g = st.gems[static_cast<std::size_t>(gem_)];
                auto ms = static_cast<long long>(static_cast<std::int32_t>(g.ready - net::GameConnection::eq_time()));
                std::printf("  SPELLS RESULT: cast done after %.2f s (cast bar %u ms); mana %u -> %u; gem %d ready in %lld ms, global in %lld ms\n",
                            std::chrono::duration<double>(now - mark_).count(), cast_bar_, mana_before_, conn.player_status().mana,
                            gem_ + 1, ms, static_cast<long long>(static_cast<std::int32_t>(st.recovery_ready - net::GameConnection::eq_time())));
                print_buffs(conn);
                next(Phase::StopCast);
            } else if (since(30)) {
                std::printf("  SPELLS RESULT: the server never ended the cast\n");
                next(Phase::StopCast);
            }
            break;
        case Phase::StopCast:
            if (!stop_sent_) {
                if (!wait_ready(conn, now)) break;
                target_for(conn, gem_spell);
                std::printf("  SPELLS: casting again and /stopcast after 300 ms\n");
                conn.cast_gem(gem_);
                stop_sent_ = true;
                mark_ = now;
                break;
            }
            if (since(0.3) && !stop_done_) {
                conn.send_command("/stopcast");
                stop_done_ = true;
            }
            if (stop_done_ && !st.casting.casting() && st.pending == 0 && since(1.0)) {
                std::printf("  SPELLS RESULT: /stopcast: last line '%s', interrupts so far %d\n", last_chat_.c_str(), interrupts_);
                next(Phase::MoveCast);
            } else if (since(15)) {
                std::printf("  SPELLS RESULT: /stopcast got no answer\n");
                next(Phase::MoveCast);
            }
            break;
        case Phase::MoveCast: {
            const auto* me = conn.world().player();
            if (!me) break;
            if (!move_sent_) {
                if (!wait_ready(conn, now)) break;
                const auto* s = conn.spell_table().get(gem_spell);
                if (s && s->cast_time < 1500) {
                    std::printf("  SPELLS: %s casts too fast to move during it; skipping the movement test\n", s->name.c_str());
                    next(Phase::Forget);
                    break;
                }
                std::printf("  SPELLS: casting again and moving 10 units during the cast\n");
                target_for(conn, gem_spell);
                conn.cast_gem(gem_);
                start_pos_ = me->position;
                move_sent_ = true;
                mark_ = now;
                break;
            }
            if (since(0.4) && since(0.4) && !moved_) {
                glm::vec3 to = start_pos_ + glm::vec3(10.0f, 0.0f, 0.0f);
                conn.set_player_motion(to, me->heading, glm::vec3(20.0f, 0, 0), true);
                moved_ = true;
            }
            if (moved_ && since(0.8)) conn.set_player_motion(start_pos_ + glm::vec3(10.0f, 0, 0), me->heading, glm::vec3(0.0f), false);
            if (moved_ && !st.casting.casting() && st.pending == 0 && since(1.0)) {
                std::printf("  SPELLS RESULT: moving cast: last line '%s', interrupts %d, fizzles %d\n", last_chat_.c_str(), interrupts_, fizzles_);
                next(Phase::Forget);
            } else if (since(20)) {
                std::printf("  SPELLS RESULT: moving cast got no answer\n");
                next(Phase::Forget);
            }
            break;
        }
        case Phase::Forget:
            if (since(2) && !forget_sent_) {
                forgotten_ = gem_spell;
                std::printf("  SPELLS: forgetting gem %d (%s)\n", gem_ + 1, name(conn, forgotten_).c_str());
                conn.forget_gem(gem_);
                forget_sent_ = true;
                mark_ = now;
                break;
            }
            if (forget_sent_ && gem_spell == game::kNoSpell && st.pending == 0) {
                std::printf("  SPELLS RESULT: gem %d forgotten after %.2f s\n", gem_ + 1, std::chrono::duration<double>(now - mark_).count());
                // Memorize it back from the book (sitting, as the book needs).
                conn.set_sitting(true);
                for (int slot = 0; slot < game::kSpellBookSize; ++slot)
                    if (st.book[static_cast<std::size_t>(slot)] == forgotten_) {
                        print_formula(conn, forgotten_);
                        conn.memorize(slot, gem_);
                        break;
                    }
                next(Phase::Rememorize);
            } else if (forget_sent_ && since(10)) {
                std::printf("  SPELLS RESULT: forget got no answer\n");
                next(Phase::Med);
            }
            break;
        case Phase::Rememorize:
            if (gem_spell == forgotten_) {
                std::printf("  SPELLS RESULT: re-memorized %s after %.1f s\n", name(conn, gem_spell).c_str(),
                            std::chrono::duration<double>(now - mark_).count());
                next(Phase::Med);
            } else if (since(mem_timeout_)) {
                std::printf("  SPELLS RESULT: re-memorizing timed out\n");
                next(Phase::Med);
            }
            break;
        case Phase::Med:
            if (med_seconds_ <= 0) {
                phase_ = Phase::Done;
                break;
            }
            if (!sat_) {
                conn.set_sitting(true);
                sat_ = true;
                mana_before_ = conn.player_status().mana;
                mark_ = now;
                break;
            }
            if (since(med_seconds_)) {
                std::printf("  SPELLS RESULT: sat %d s: mana %u -> %u (max %u)\n", med_seconds_, mana_before_, conn.player_status().mana,
                            conn.player_status().max_mana);
                conn.set_sitting(false);
                phase_ = Phase::Done;
            }
            break;
        default:
            break;
        }
    }

    void finish(net::GameConnection& conn) {
        if (!enabled_ || phase_ == Phase::Idle) return;
        print_gems(conn);
        print_buffs(conn);
    }

private:
    enum class Phase { Idle, Settle, Scribe, Memorize, Cast, CastWait, StopCast, MoveCast, Forget, Rememorize, Med, Done };

    void next(Phase p) {
        phase_ = p;
        mark_ = Clock::now();
    }

    // A general slot (23..32) holding a spell scroll, or -1.
    static int find_scroll(const net::GameConnection& conn) {
        for (std::int16_t slot = net::kSlotGeneral1; slot <= net::kSlotGeneral10; ++slot) {
            const auto* it = conn.inventory().at(net::ItemSlot::possessions(slot));
            if (it && it->item_type == 20 && it->effects[net::kEffectScroll].effect > 0) return slot;
        }
        return -1;
    }

    // GetSpellMemTicksLeft's numbers for a non-bard (profile class, level, Meditate as the profile has it).
    static void print_formula(const net::GameConnection& conn, std::uint32_t id) {
        const auto* s = conn.spell_table().get(id);
        const auto& prof = conn.profile();
        if (!s || !prof) return;
        const auto* p = &*prof;
        int cls = p->class_ >= 1 && p->class_ <= 16 ? p->class_ : 1;
        int need = conn.spell_level_needed(*s, static_cast<std::uint32_t>(cls)); // the NMS detour on an NMS server
        int level = conn.player_status().level;
        int interval = std::max(100, (need - level + 5) * 100);
        int med = p->skills.size() > 31 ? static_cast<int>(p->skills[31]) : 0;
        int step = 3 + std::max(med, 10) / 10;
        int ticks = (92 + step - 1) / step;
        std::printf("  SPELLS: memorize formula: class %d needs level %d, level %d -> a tick every > %d ms, Meditate %d -> "
                    "step %d, %d ticks (first at once): about %.1f s + frames + the server's answer\n",
                    cls, need, level, interval, med, step, ticks, (ticks - 1) * interval / 1000.0);
    }

    static std::string name(const net::GameConnection& conn, std::uint32_t id) {
        const auto* s = conn.spell_table().get(id);
        return s ? s->name : "spell " + std::to_string(id);
    }

    // Waits for the gem and enough mana (sitting to regain it if needed).
    bool wait_ready(net::GameConnection& conn, Clock::time_point now) {
        const auto& st = conn.spell_state();
        auto id = st.gems[static_cast<std::size_t>(gem_)].spell;
        const auto* s = conn.spell_table().get(id);
        if (!s) return false;
        if (!st.gem_castable(gem_, net::GameConnection::eq_time()) || st.casting.casting() || st.pending > 0) return false;
        if (static_cast<std::int64_t>(conn.player_status().mana) < s->mana) {
            if (!conn.sitting()) conn.set_sitting(true);
            return false;
        }
        if (conn.sitting()) conn.set_sitting(false);
        return now - mark_ > std::chrono::milliseconds(500);
    }

    // Self-only spells need no target; others are cast on ourselves.
    void target_for(net::GameConnection& conn, std::uint32_t id) {
        const auto* s = conn.spell_table().get(id);
        if (s && s->target_type != game::kTargetSelf && conn.target() != conn.world().player_id) conn.set_target(conn.world().player_id);
    }

    void print_gems(const net::GameConnection& conn) const {
        const auto& st = conn.spell_state();
        std::printf("  SPELLS: gems:");
        for (int g = 0; g < game::kSpellGems; ++g) {
            auto id = st.gems[static_cast<std::size_t>(g)].spell;
            if (id == game::kNoSpell) continue;
            // The gem timer's time left (__EQGetTime ms; negative: ready) and the profile's spellSlotRefresh.
            auto left = static_cast<std::int32_t>(st.gems[static_cast<std::size_t>(g)].ready - net::GameConnection::eq_time());
            const auto& prof = conn.profile();
            std::uint32_t refresh = prof && static_cast<std::size_t>(g) < prof->gem_refresh.size() ? prof->gem_refresh[static_cast<std::size_t>(g)] : 0;
            std::printf(" %d=%s (timer %+d ms, refresh %u, castable %d)", g + 1, name(conn, id).c_str(), left, refresh,
                        st.gem_castable(g, net::GameConnection::eq_time()) ? 1 : 0);
        }
        std::printf(" recovery %+d ms\n", static_cast<std::int32_t>(st.recovery_ready - net::GameConnection::eq_time()));
    }

    void print_buffs(const net::GameConnection& conn) const {
        const auto& st = conn.spell_state();
        auto now = net::GameConnection::now_ms();
        std::printf("  SPELLS: buffs:");
        for (int i = 0; i < game::kBuffSlots; ++i) {
            const auto& b = st.buffs[static_cast<std::size_t>(i)];
            if (b.empty()) continue;
            const auto* s = conn.spell_table().get(b.spell);
            std::printf(" [%d %s %s]", i, name(conn, b.spell).c_str(),
                        game::buff_is_permanent(b.tics, s) ? "permanent"
                                                           : game::format_duration(game::buff_ms_left(b.tics, b.received_ms, now)).c_str());
        }
        std::printf("\n");
    }

    bool enabled_ = false;
    int gem_ = 0;
    int med_seconds_ = 0;
    int mem_timeout_ = 60;
    Phase phase_ = Phase::Idle;
    Clock::time_point mark_{};
    int book_first_ = -1;
    std::uint32_t mana_before_ = 0;
    std::uint32_t cast_bar_ = 0;
    bool stop_sent_ = false, stop_done_ = false;
    bool move_sent_ = false, moved_ = false;
    glm::vec3 start_pos_{0.0f};
    bool forget_sent_ = false;
    std::uint32_t forgotten_ = game::kNoSpell;
    bool sat_ = false;
    bool scribe_sent_ = false;
    Clock::time_point scribe_mark_{};
    int interrupts_ = 0, fizzles_ = 0;
    std::string last_chat_;
};

} // namespace hailstone::probe
