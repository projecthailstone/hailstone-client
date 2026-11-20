// CharacterZoneClient::UseSkill @0x45b2d0 (use_skill.h). Each block names the original's addresses.
#include "game/mechanics/use_skill.h"

#include <bit>

namespace hailstone::game::mechanics::useskill {

namespace {

// The skills a mounted player may still use (the table at 0x45d9a8).
bool usable_on_mount(std::uint32_t skill) {
    static constexpr std::uint32_t kMounted[] = {10, 16, 27, 30, 32, 40, 53, 55, 71, 73};
    for (std::uint32_t s : kMounted)
        if (s == skill) return true;
    return false;
}

constexpr std::int32_t kColorSkill = 0x10e, kColorRed = 0xd;

// eqstr ids (NMS/eqstr_us.txt) and tokens.
constexpr std::uint32_t kNoControl = 0x3050;      // 12368 You do not have control of yourself right now.
constexpr std::uint32_t kNotOnMount = 0x3069;     // 12393
constexpr std::uint32_t kNeedShield = 0x306a;     // 12394

float as_float(std::uint32_t bits) { return std::bit_cast<float>(bits); }

class Run {
public:
    Run(State& s, std::uint32_t skill, Queries& q)
        : s_(s), q_(q), f_(s.precision), dword_(skill), skill_(skill & 0xff) {}

    void go();

private:
    // getString(id, 0) then dsp_chat(text, color, 1, 1).
    void say(std::uint32_t id, std::int32_t color = kColorSkill) { q_.chat(q_.get_string(id, 0), color, 1, 1); }
    // The refusal at 0x45cae4: the message, and the skill's timer is given back.
    void fail(std::uint32_t id) {
        say(id);
        q_.set_skill_can_use(skill_, 1);
    }
    // A packet through the shared send buffer (0x45d372..): Send, then the send count; a failed send closes the
    // connection. Not connected: DebugLog unless 0xddf654.
    void send(SkillSend::Kind kind, std::uint32_t target = 0, std::uint32_t source = 0,
              std::uint32_t value = 0) {
        if (s_.connected) {
            bool ok = q_.send(4, SkillSend{kind, target, 0, value, source});
            --s_.send_count;
            if (!ok) q_.close_connection();
        } else if (!s_.quiet_log) {
            q_.debug_log_void_connection(static_cast<std::uint32_t>(kind));
        }
    }
    Obj pc() { return q_.get_pc_client() ? Obj::Pc : Obj::Null; }
    Obj pc_character() { return q_.get_pc_client() ? Obj::PcCharacter : Obj::Null; }
    // token_string_to_client(the PcClient's character, id, 0x10e, 0) (0x45cef5).
    void token(std::int32_t id) { q_.token_message(pc_character(), id, kColorSkill, 0); }
    void save_position() {
        s_.hide_position[0] = Fpu::ftol(as_float(s_.me->y));
        s_.hide_position[1] = Fpu::ftol(as_float(s_.me->x));
        s_.hide_position[2] = Fpu::ftol(as_float(s_.me->z));
    }
    // CDisplay::PlayerDistance(me, target, 0) compared with 15.0 (0x9c588c): above 15 (NaN and 15 pass).
    bool farther_than_15(Spawn& t) { return as_float(q_.player_distance(*s_.me, t, 0)) > 15.0f; }
    void success_combat();
    void combat_switch();
    void bash();
    void main_switch();

    State& s_;
    Queries& q_;
    Fpu f_;
    std::uint32_t dword_; // the argument as passed (DoAttack gets it whole)
    std::uint32_t skill_; // its low byte
};

void Run::go() {
    if (!s_.me) return;
    if (q_.cur_hp(0, 1) < 1) return;
    Spawn& me = *s_.me;
    if (skill_ < 0x64 && q_.get_adjusted_skill(static_cast<std::int32_t>(skill_)) <= 0) {
        if (skill_ != 0x1b) return;                         // forage with SPA 372
        if (q_.total_effect(0x174, 1, 0, 1, 1) == 0) return;
    }
    if (me.stand_state == 0x66) {
        say(kNoControl, kColorRed);
        return;
    }
    if (me.has_mount || q_.get_pc_spell_affect(Obj::Character, 0x71, nullptr, false) != 0) {
        if (!usable_on_mount(skill_)) {
            say(kNotOnMount, kColorRed);
            return;
        }
    }
    if (skill_ == 0x72) q_.send_ldon_inspect(0, 0);
    else if (skill_ == 0x73) q_.send_ldon_open(0, 0);
    else if (skill_ == 0x74) q_.cmd_reveal(0, 0);
    else if (skill_ == 0x6f) return combat_switch();
    if (q_.get_skill_combat_type(skill_) == 2) return combat_switch();
    // The reuse timer (0x45b424..): not sneak while sneaking, nor hide while hidden.
    bool skip = skill_ == 0x2a ? me.sneak != 0 : (skill_ == 0x1d && q_.pc_get_hide(Obj::Pc));
    if (!skip) {
        if (!q_.is_available(skill_)) return;
        std::int32_t reuse = q_.get_skill_reuse_time(skill_);
        std::int32_t ms = reuse < 0 ? 0 : reuse;
        q_.set_skill_timer_duration(skill_, ms);
        q_.set_skill_last_used(skill_, s_.display_time);
        q_.set_skill_can_use(skill_, 0);
        std::uint32_t other = skill_ == 0x43 ? 0x30u : skill_ == 0x30 ? 0x43u : 0u;
        if (other) {
            q_.set_skill_timer_duration(other, ms);
            q_.set_skill_last_used(other, s_.display_time);
            q_.set_skill_can_use(other, 0);
        }
    }
    main_switch();
}

// After DoAttack returned true (0x45d93c): the skill's timer.
void Run::success_combat() {
    q_.set_skill_can_use(skill_, 0);
    std::size_t index = skill_ == 0x34 ? 1 : 0; // GetCombatSkillTimerIndex
    q_.set_skill_last_used(skill_, s_.me->combat_skill_used[index]);
}

// The combat switch (0x45d3ea, by skill - 8).
void Run::combat_switch() {
    switch (skill_) {
    case 8: case 21: case 23: case 26: case 30: case 38: case 52: case 74:
        if (q_.do_attack(*s_.me, 100, dword_, s_.target)) success_combat();
        return;
    case 10: return bash();
    case 111: // slam: bash with InnateSkill[11]
        q_.get_current_profile();
        if (s_.innate_slam == 0xff) return;
        if (q_.do_attack(*s_.me, 100, 10, s_.target)) success_combat();
        return;
    default: return;
    }
}

// Bash (0x45d43c): a usable two-hander with SPA 226; else a usable shield; else one of three primaries; else 12394.
// Every lookup takes a reference that is released in reverse order.
void Run::bash() {
    bool two_hand_bash = false;
    {
        Handle primary = q_.get_item(13);
        bool second = false;
        if (primary && s_.me && q_.total_effect(0xe2, 1, 0, 1, 1) != 0) {
            Handle again = q_.get_item(13);
            second = true;
            two_hand_bash = q_.can_use_item(again, 1, 0);
        }
        if (second) q_.item_ref_release();
        q_.item_ref_release();
    }
    if (two_hand_bash) {
        int refs = 1;
        bool two_hander = q_.get_item_type(q_.get_item(13)) == 1;
        if (!two_hander) {
            ++refs;
            two_hander = q_.get_item_type(q_.get_item(13)) == 4;
            if (!two_hander) {
                ++refs;
                two_hander = q_.get_item_type(q_.get_item(13)) == 0x23;
            }
        }
        while (refs--) q_.item_ref_release();
        if (two_hander) {
            if (q_.do_attack(*s_.me, 100, 10, s_.target)) success_combat();
            return;
        }
    }
    bool no_shield;
    {
        int refs = 1;
        if (!q_.get_item(14)) {
            no_shield = true;
        } else {
            ++refs;
            if (q_.get_item_class(q_.get_item(14)) != 0) {
                no_shield = true;
            } else {
                ++refs;
                if (q_.get_item_type(q_.get_item(14)) != 8) {
                    no_shield = true;
                } else {
                    ++refs;
                    no_shield = !q_.can_use_item(q_.get_item(14), 1, 0);
                }
            }
        }
        while (refs--) q_.item_ref_release();
    }
    if (no_shield) {
        int refs = 1;
        bool refused;
        if (!q_.get_item(13)) {
            refused = true;
        } else {
            ++refs;
            if (q_.get_item_id(q_.get_item(13)) == 0x2b2a) {
                refused = false;
            } else {
                ++refs;
                if (q_.get_item_id(q_.get_item(13)) == 0x2773) {
                    refused = false;
                } else {
                    ++refs;
                    refused = q_.get_item_id(q_.get_item(13)) != 0x382f;
                }
            }
        }
        while (refs--) q_.item_ref_release();
        if (refused) {
            say(kNeedShield);
            return;
        }
    }
    if (q_.do_attack(*s_.me, 100, dword_, s_.target)) success_combat();
}

// The skill switch (0x45b52a, by skill - 9).
void Run::main_switch() {
    Spawn& me = *s_.me;
    Spawn* t = s_.target;
    switch (skill_) {
    case 35:   // pick lock (0x45b531)
    case 17: { // disarm traps (0x45b5a2)
        if (t && q_.is_interactive_object(*t)) send(skill_ == 35 ? SkillSend::Kind::PickLock : SkillSend::Kind::DisarmTraps);
        else say(skill_ == 35 ? 0x11e6 : 0x11e7);
        return;
    }
    case 75: { // remove traps (0x45b613)
        if (t && q_.is_interactive_object(*t)) {
            send(SkillSend::Kind::RemoveTrap, t->spawn_id);
        } else {
            say(0xe58, kColorRed);
        }
        q_.set_skill_can_use(0x4b, 1);
        return;
    }
    case 73: { // taunt (0x45b71b)
        if (!t || t->type != 1) return fail(0x306b);
        float distance = as_float(q_.player_distance(me, *t, 0));
        float range = as_float(q_.get_melee_range(me, *t));
        if (range < distance) return fail(0x182); // NaN passes
        if (q_.is_vehicle_race(*t)) return fail(0x306c);
        std::string name = q_.trim_name(t->name);
        q_.chat(q_.build_token_string(0x306d, name, 0), kColorSkill, 1, 1);
        send(SkillSend::Kind::Taunt, s_.target->spawn_id);
        return;
    }
    case 25: { // feign death (0x45b863)
        q_.change_position(me, 0x73);
        if (s_.me == s_.local_player) {
            q_.set_auto_attack(0);
            q_.set_auto_range_attack(0);
        }
        send(SkillSend::Kind::FeignDeath, s_.me->spawn_id, 0,
             static_cast<std::uint32_t>(s_.feign_value));
        return;
    }
    case 71: { // intimidation (0x45b905)
        std::uint32_t refusal = 0;
        if (!t) refusal = 0x306e;
        else if (t == s_.local_player || t == s_.controlled_player) refusal = 0x3193;
        else if (!s_.local_player || s_.local_player->stand_state != 0x64) refusal = 0x3070;
        if (refusal) {
            say(refusal);
            me.next_intimidate_time = 0;
            me.intimidate_count = 0;
            q_.set_skill_can_use(skill_, 1);
            return;
        }
        me.intimidate_count = 6;
        send(SkillSend::Kind::InstillDoubt);
        return;
    }
    case 67: { // begging (0x45ba1f)
        if (!t || t->type != 1) return fail(0x3071);
        if (s_.attack) return fail(0x3072);
        if (farther_than_15(*t)) return fail(0x3073);
        if (me.hide_mode != 0) return fail(0x3074);
        std::uint32_t my_id = me.spawn_id, target_id = s_.target->spawn_id;
        q_.get_current_profile();
        std::int32_t value = s_.skill_begging;
        ++s_.item_pending;
        q_.push_pending_reason(8);
        send(SkillSend::Kind::Begging, target_id, my_id, static_cast<std::uint32_t>(value));
        return;
    }
    case 48: { // pick pockets (0x45bbb6)
        if (!t) return fail(0x3075);
        if (q_.is_gm(*t)) return;
        std::uint8_t type = t->type;
        if (type >= 2) return fail(0x3076);
        if (s_.status == 1) return fail(0x3077);
        if (s_.attack) return fail(0x3078);
        std::int32_t ruleset = s_.ruleset;
        bool deity = false;
        if (type == 0) {
            if (t == s_.me) return fail(0x3079);
            if (!t->pvp && ruleset != 4) return fail(0x307a);
            if (!me.pvp && ruleset != 4) return fail(0x307b);
            deity = ruleset == 4;
        }
        if (deity) {
            if (q_.not_on_same_deity(*t, me, 0) == 0) return fail(0x307c);
            std::uint32_t mine = static_cast<std::uint32_t>(q_.get_level(me)) & 0xff;
            std::uint32_t theirs = static_cast<std::uint32_t>(q_.get_level(*t)) & 0xff;
            if (theirs - mine + 5 > 10 || (static_cast<std::uint32_t>(q_.get_level(*s_.target)) & 0xff) < 6)
                return fail(0x307d);
        }
        if (farther_than_15(*s_.target)) return fail(0x307e);
        std::uint32_t target_id = s_.target->spawn_id, my_id = me.spawn_id;
        q_.get_current_profile();
        std::int32_t value = s_.skill_pick_pockets;
        ++s_.item_pending;
        q_.push_pending_reason(9);
        send(SkillSend::Kind::PickPocket, target_id, my_id, static_cast<std::uint32_t>(value));
        q_.make_me_visible(q_.get_pc_client() ? Obj::PcCharacter : Obj::NullPcCharacter, 0, 1);
        return;
    }
    case 32: send(SkillSend::Kind::Mend); return;  // mend
    case 62: send(SkillSend::Kind::SenseTraps); return;  // sense traps
    case 16: { // disarm (0x45bed0)
        if (!t) return fail(0x3080);
        if (t == s_.me) return fail(0x3081);
        if (farther_than_15(*t)) return fail(0x3082);
        if (s_.target->type >= 2) return fail(0x3083); // PlayerClient::IsCorpse
        if (!q_.can_i_hit(me, s_.target, 0)) return;
        if (me.hide_mode != 0) {
            q_.set_hide_mode(me, 0);
            std::uint16_t id = static_cast<std::uint16_t>(me.spawn_id);
            std::int32_t mode = me.hide_mode;
            for (std::int32_t spa : {12, 28, 29, 314, 315, 316, 94, 312}) {
                std::int32_t slot = -1;
                Handle affect = q_.get_pc_spell_affect(Obj::PcCharacter, spa, &slot, false);
                q_.remove_pc_affect(Obj::PcCharacter, affect, 0);
                if (slot != -1) q_.notify_pc_affect_change(Obj::PcCharacter, slot, 1);
            }
            send(SkillSend::Kind::Unhide, id);
            q_.player_per_frame_update(s_.local_player);
        }
        std::uint32_t my_id = me.spawn_id, target_id = s_.target->spawn_id;
        q_.get_current_profile();
        send(SkillSend::Kind::Disarm, target_id, my_id,
             static_cast<std::uint32_t>(s_.skill_disarm));
        return;
    }
    case 53: // tracking (0x45c345)
        if (!s_.tracking_window_visible) q_.tracking_window_show(1, 1, 1);
        else q_.send_track();
        send(SkillSend::Kind::TrackUnknown);
        return;
    case 42: { // sneak (0x45c3a4)
        if (me.sneak) {
            me.sneak = 0;
            if (me.type == 0) {
                q_.pc_set_sneak(pc(), 0);
                if (q_.pc_get_hide(pc())) save_position();
            }
            send(SkillSend::Kind::StopSneak, me.spawn_id);
            say(0x3032);
            return;
        }
        if (s_.attack) return fail(0x3084);
        send(SkillSend::Kind::Sneak);
        say(0x3086);
        return;
    }
    case 29: { // hide (0x45c5ab)
        if (s_.activity == 0x1d) {
            say(0x307f);
            if (me.type == 0) q_.pc_set_hide(pc(), 0);
            q_.update_my_visible_status(Obj::Character, 1);
            q_.set_activity(-1, 0);
            return;
        }
        if (s_.attack || s_.autofire) return fail(0x3087);
        // Standing still: each speed within the limit (NaN passes); the limit is 0.001 up to skill 50, else
        // min((skill - 50) * 0.001, 0.5 ... ) at the FPU's precision (0x45c63b..).
        std::int32_t value = q_.get_base_skill(0x1d, 1);
        double limit = 0.001;
        if (value > 50) {
            double v = f_.mul(f_.mul(static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(value) - 50u)), 0.001), 0.5);
            limit = 0.5 < v ? 0.5 : v;
        }
        auto still = [&](std::uint32_t bits) {
            double v = as_float(bits);
            return !(v < -limit) && !(v > limit);
        };
        bool ok = still(me.speed_y) && still(me.speed_x) && still(me.speed_z);
        if (!ok) {
            std::int32_t chance = q_.total_effect(0x11d, 1, 0, 1, 1);
            if (q_.rand_nbr() % 100 + 1 > chance) return fail(0x3088);
        }
        q_.set_activity(0x1d, 0);
        save_position();
        if (me.type == 0) q_.pc_set_hide(pc(), 1);
        send(SkillSend::Kind::Hide);
        say(0x3089);
        q_.update_my_visible_status(Obj::Character, 1);
        return;
    }
    case 40: { // sense heading (0x45c85f)
        std::int32_t heading = Fpu::ftol(as_float(me.heading));
        std::int32_t roll = q_.random_low_high(1, 100);
        if (roll <= 7) roll = 0;
        if (roll > q_.get_base_skill(0x28, 1) || roll > 0x5f) return fail(0x308a);
        if (s_.compass_window) q_.compass_reset();
        std::string direction;
        std::uint32_t id = 0;
        if (heading >= 0x1e0) id = 0x308b;
        else if (heading > 0x1a0) id = 0x308c;
        else if (heading > 0x160) id = 0x308d;
        else if (heading > 0x120) id = 0x308e;
        else if (heading > 0xe0) id = 0x308f;
        else if (heading > 0xa0) id = 0x3090;
        else if (heading > 0x60) id = 0x3091;
        else if (heading > 0x20) id = 0x3092;
        else if (heading >= 0) id = 0x308b;
        if (id) direction = q_.get_string(id, 0);
        q_.chat(q_.build_token_string(0x3093, direction, 0), kColorSkill, 1, 1);
        send(SkillSend::Kind::SenseHeading);
        return;
    }
    case 27: // forage (0x45ce74)
        if (s_.stand_state != 0x64) {
            token(0x95);
            q_.set_skill_can_use(skill_, 1);
            return;
        }
        if (s_.attack) {
            token(0x94);
            q_.set_skill_can_use(skill_, 1);
            return;
        }
        send(SkillSend::Kind::Forage);
        return;
    case 9: { // bind wound (0x45cb47)
        if (s_.activity == 9) return;
        auto refuse = [&](std::int32_t id) {
            token(id);
            q_.set_skill_can_use(skill_, 1);
        };
        if (!t) return refuse(0x90);
        if (t->type != 0) {
            if (!t->mercenary) return refuse(0x90);
        } else if (s_.ruleset == 4 && q_.not_on_same_deity(me, *t, 0) != 0) {
            return refuse(0x91);
        }
        if (!q_.find_item_by_item_type(0x12, nullptr, nullptr)) return refuse(0x92);
        if (t->type != 0 && !t->mercenary) return;
        float distance = as_float(q_.player_distance(me, *t, 0));
        if (!(distance < 15.0f)) { // 15 and NaN refuse
            q_.chat_template(Obj::Character, 0x1a4, kColorSkill, t->displayed_name, 0);
            q_.set_skill_can_use(skill_, 1);
            return;
        }
        ++s_.currently_interacting;
        ++s_.item_pending;
        q_.push_interact_reason(1);
        q_.push_pending_reason(10);
        send(SkillSend::Kind::BindWound, t->spawn_id);
        if (me.stand_state == 0x64) q_.change_position(me, 0x69);
        std::string text = t == s_.local_player ? q_.get_string(0x3094, 0)
                                                : q_.build_token_string(0x3095, t->displayed_name, 0);
        q_.chat(text, kColorSkill, 1, 1);
        return;
    }
    case 55: { // fishing (0x45cf4a); its refusals keep the timer
        if (me.fishing_event) {
            if (me.type == 0) token(0x9c);
            return;
        }
        if (me.body_wet) {
            if (me.type == 0) token(0x9d);
            return;
        }
        if (me.type == 0) {
            bool cursor = q_.get_item(0x21) != 0;
            q_.item_ref_release();
            if (cursor) return token(0x9e);
            if (s_.attack) return token(0x9f);
            std::int32_t slot = 0, bag = 0;
            if (!q_.find_item_by_item_type(0x24, &slot, &bag)) return token(0xa0);
            if (bag != -1 || (slot != 13 && slot != 14)) return token(0xa1);
            if (!q_.find_item_by_item_type(0x25, nullptr, nullptr)) return token(0xa2);
            Handle pole = q_.get_item(slot);
            std::string id_file = q_.get_id_file(pole, 0x22);
            q_.update_item_slot(me, slot == 13 ? 7 : 8, id_file, 0, 1); // 0x4dcea0: 13 -> 7, 14 -> 8
            q_.item_ref_release();
            token(0xa3);
            q_.set_activity(0x37, 0);
            save_position();
        }
        if (s_.stand_state == 0x64) {
            Handle animation = q_.get_animation(me);
            q_.animation_vf0c(animation, 5, 1, 0, std::bit_cast<std::uint32_t>(0.5f), 1, 0xff, 0);
            send(SkillSend::Kind::FishAnimation, me.spawn_id);
        }
        me.fishing_eta = static_cast<std::int32_t>(static_cast<std::uint32_t>(q_.get_time()) + 2000u);
        me.fishing_event = 1;
        send(SkillSend::Kind::Fishing);
        return;
    }
    default: return; // apply poison and skills without a case: the timer only
    }
}

} // namespace

void use_skill(State& s, std::uint32_t skill, Queries& q) { Run(s, skill, q).go(); }

} // namespace hailstone::game::mechanics::useskill
