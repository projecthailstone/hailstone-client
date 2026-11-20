#include "app/sound_events.h"

#include <cmath>
#include <span>

#include "audio/audio_system.h"
#include "audio/listener.h"
#include "audio/sound_control.h"
#include "core/game_dir.h"
#include "core/log.h"
#include "game/mechanics/anim_select.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"

namespace hailstone::app {

namespace {

// The classic animation numbers (OP_Animation's) the movement plays.
constexpr std::int32_t kAnimWalk = 0x11, kAnimRun = 0x12, kAnimJump = 0x13, kAnimSwim = 0x19, kAnimTread = 0x25,
                       kAnimStand = 0x20;

// Same as render/actor_renderer.cpp's choice of the walk and run clips.
constexpr float kIdleBelow = 1.5f, kRunAbove = 26.0f;

float height_of(const game::Spawn& s) { return s.heights.height; } // Height (+0x13c)

// How far past the reach of its sounds a walking spawn is still turned into them (audio::spawn_out_of_hearing): it
// moves between frames, and a step at the edge is quiet anyway.
constexpr float kHearingMargin = 50.0f;

} // namespace

SoundEvents::SoundEvents(audio::AudioSystem& audio, const GameDir& game) : audio_(audio) {
    if (auto fx = game.read("spellsnew.eff")) spell_effects_ = assets::parse_spell_effect_sounds(*fx);
    // SpellEffects_ReadFiles: the 256 old effects of spells.eff (0xae8 bytes each).
    if (auto old = game.read("spells.eff")) {
        std::span<const std::byte> bytes(*old);
        for (std::size_t at = 0; at + game::kOldSpellEffectSize <= bytes.size() && old_effects_.size() < 256;
             at += game::kOldSpellEffectSize)
            old_effects_.push_back(game::old_spell_effect_sounds(bytes.subspan(at, game::kOldSpellEffectSize)));
    }
}

void SoundEvents::reset() { spawns_.clear(); }

void SoundEvents::play_at(const game::Spawn& s, std::int32_t wave, bool local, int volume_percent) {
    if (wave == 0) return;
    // PlayerClient::PlaySound: the spawn's position, its height for the distances.
    audio::SoundControl c =
        audio::spawn_sound_control(s.display_valid ? s.display_position : s.position, height_of(s), local);
    if (volume_percent < 100) c.volume *= audio::option_volume_factor(volume_percent, c.positional());
    audio_.play_wave(wave, c);
}

void SoundEvents::animation(const game::Spawn& s, std::int32_t action, bool local, const LocalMotion* me,
                            std::uint32_t now_ms, SpawnState& st) {
    const game::SpawnSounds& ids = audio_.spawn_sounds(s.race, s.gender);
    game::AnimationSoundInput in;
    in.action = action;
    in.local = local;
    in.race = s.race;
    in.right_holding = game::held_type(s.appearance.material[7]);
    in.left_holding = game::held_type(s.appearance.material[8]);
    in.spawn_type = s.is_corpse ? 2 : s.is_npc ? 1 : 0;
    in.stand_state = s.stand_state;
    in.hp = s.hp_percent;
    // LegalPlayerRace(-1): the spawn's race up to 12 (signed: 0 and negatives too), or 128, 130, 330, 522.
    in.legal_player_race = audio::legal_player_race(-1, s.race);
    if (me) {
        in.sneaking = me->sneaking;
        in.flying = me->flying;
    }
    auto random = [this](std::int32_t low, std::int32_t high) { return rand_.between(low, high); };
    game::AnimationSounds out = game::animation_sounds(ids, in, st.anim, now_ms, random);
    for (std::int32_t w : out.play) {
        // The local player's (and its mount's) sound is not restarted while it plays.
        if (out.only_if_not_playing && audio_.wave_playing(w)) continue;
        play_at(s, w, local);
    }
    if (out.loop && !local) {
        // SetCurrentLoopSound: 0 means the Loop3d sound; nothing for corpses.
        std::int32_t loop = *out.loop == 0 ? ids[game::SoundKind::Loop3d] : *out.loop;
        if (in.spawn_type > 1) loop = 0;
        st.loop = loop;
    }
}

void SoundEvents::update(net::GameConnection& conn, const LocalMotion& me, std::uint32_t now_ms) {
    const game::WorldState& world = conn.world();
    const std::uint32_t my_id = world.player_id;
    auto spawn = [&](std::uint32_t id) -> const game::Spawn* {
        auto it = world.spawns.find(id);
        return it == world.spawns.end() ? nullptr : &it->second;
    };

    for (const net::SoundCue& c : conn.take_sound_cues()) {
        log::debug("sound cue {} source {} target {} damage {} spell {}", static_cast<int>(c.kind), c.source, c.target,
                   c.damage, c.spell);
        switch (c.kind) {
        case net::SoundCue::Kind::Wave:
            audio_.play_wave_2d(static_cast<std::int32_t>(c.spell));
            break;
        case net::SoundCue::Kind::NewMail:
            audio_.play_named_2d("YouveGotMail");
            break;
        case net::SoundCue::Kind::LevelUp:
            audio_.play_wave_2d(game::kSoundLevelUp);
            break;
        case net::SoundCue::Kind::Door:
            for (std::int32_t w : game::door_sounds(c.door_type, c.door_state))
                audio_.play_wave(w, audio::location_sound_control(c.position));
            break;
        case net::SoundCue::Kind::Hit: {
            const game::Spawn* d = spawn(c.target);
            if (!d) break;
            bool local = c.target == my_id;
            // Hailstone, not in the original: audio.npc_hits turns an NPC's hit, block and dodge sounds down.
            int volume = d->is_npc && !local ? audio_.settings().npc_hits : 100;
            if (c.damage > 0) {
                game::HitInput in;
                in.damage = c.damage;
                in.defender_is_me = local;
                in.defender_hp = static_cast<std::int32_t>(conn.own_hp().cur); // Cur_HP, the items' HP included
                in.defender_level = d->level;
                in.defender_corpse = d->is_corpse;
                in.environment = c.environment;
                for (std::int32_t w :
                     game::hit_sounds(audio_.spawn_sounds(d->race, d->gender), in, rand_.between(1, 4)))
                    play_at(*d, w, local, volume);
            } else if (c.damage == -1) {
                const game::Spawn* a = spawn(c.source);
                std::int32_t right = a ? game::held_type(a->appearance.material[7]) : 0;
                play_at(*d, game::block_sound(right, game::held_type(d->appearance.material[7]),
                                              game::held_type(d->appearance.material[8]), rand_() & 1),
                        local, volume);
            } else if (auto w = game::miss_sound(c.damage)) {
                play_at(*d, *w, local, volume);
            }
            break;
        }
        case net::SoundCue::Kind::BeginCast:
        case net::SoundCue::Kind::SpellLanded: {
            // DoSpellEffect @0x522500 (game/sound_rules.h): CEverQuest::StartCasting runs stage 0 with the casting
            // spawn as caster and target; PlayerZoneClient::IWasHit runs stage 2 for every OP_Action 231 (both
            // copies) with the packet's source as caster and the struck spawn as target.
            const game::SpellData* sp = conn.spell_table().get(c.spell);
            if (!sp) break;
            bool cast = c.kind == net::SoundCue::Kind::BeginCast;
            const game::Spawn* caster = spawn(c.source);
            const game::Spawn* target = cast ? caster : spawn(c.target);
            game::SpellEffectStageInput in;
            in.stage = cast ? 0 : 2;
            in.have_caster = caster != nullptr;
            in.have_target = target != nullptr;
            // SpellMgr_SetSpellExtraData: the new record by SpellAnim (1..count, with a name), the old by spaindex.
            if (sp->spell_anim >= 1 && static_cast<std::size_t>(sp->spell_anim) < spell_effects_.size() &&
                !spell_effects_[static_cast<std::size_t>(sp->spell_anim)].name.empty()) {
                const auto& fx = spell_effects_[static_cast<std::size_t>(sp->spell_anim)];
                in.have_new_record = true;
                in.stage_sounds = {fx.cast, fx.projectile, fx.impact};
            }
            if (sp->spa_index >= 0 && sp->spa_index <= 0xff && static_cast<std::size_t>(sp->spa_index) < old_effects_.size()) {
                in.have_old_record = true;
                if (!in.have_new_record) in.stage_sounds = old_effects_[static_cast<std::size_t>(sp->spa_index)];
            }
            game::SpellEffectStage out = game::spell_effect_stage(in);
            if (!out.sound) break;
            const game::Spawn* at = out.sound_at_target ? target : caster;
            std::uint32_t at_id = out.sound_at_target ? (cast ? c.source : c.target) : c.source;
            play_at(*at, *out.sound, at_id == my_id);
            break;
        }
        case net::SoundCue::Kind::SpawnWave: {
            // PlayerClient::PlaySound(wave) at a spawn (HitBySpell's Teleport sound).
            if (const game::Spawn* at = spawn(c.target)) play_at(*at, static_cast<std::int32_t>(c.spell), c.target == my_id);
            break;
        }
        }
    }

    // Animations.
    alive_.clear();
    const glm::vec3 listener = audio_.listener();
    for (const auto& [id, s] : world.spawns) {
        SpawnState& st = spawns_[id];
        bool local = id == my_id;
        // A new-style code (a missile's launch on a new-style source) sounds as its old-style number, as
        // 0x4b15b0 remaps it before PlayAnimationSound.
        const std::uint8_t action =
            s.action >= 0x4f ? game::mechanics::new_to_old_anim(s.action) : s.action;
        if (st.seq_seen && s.action_seq != st.action_seq && action != 0)
            animation(s, action, local, local ? &me : nullptr, now_ms, st);
        st.action_seq = s.action_seq;
        st.seq_seen = true;
        if (local) {
            // The local player's movement animation (and its jump).
            std::int32_t a = kAnimStand;
            if (me.wet) a = me.moving ? kAnimSwim : kAnimTread;
            else if (me.moving) a = me.running ? kAnimRun : kAnimWalk;
            if (me.jumped) animation(s, kAnimJump, true, &me, now_ms, st);
            animation(s, a, true, &me, now_ms, st);
            for (std::int32_t w : game::landing_sounds(me.land_sound, me.fall_damage)) play_at(s, w, true);
            continue;
        }
        if (s.is_corpse) {
            st.loop = 0;
        } else if (!audio::spawn_out_of_hearing(listener, s.display_valid ? s.display_position : s.position,
                                                height_of(s), kHearingMargin)) {
            // Hailstone, a deliberate difference: a spawn walking beyond the reach of its sounds (muted there, as
            // the original's are) is not turned into them each frame; its steps and loop pick up on its first frame
            // in reach. The original plays the muted steps, each holding one of pool 0's voices while it lasts
            // (docs/mechanics.md "Listener, music, spell sounds and spell gems"). OP_Animation's sounds above play
            // wherever the spawn is.
            // Hailstone (game::Spawn::stalled): no steps from a spawn that has stayed in one place for a second.
            float speed = s.stalled ? 0.0f
                                    : std::sqrt(s.velocity.x * s.velocity.x + s.velocity.y * s.velocity.y) * 20.0f; // per second
            float scale = height_of(s) / 6.0f;
            std::int32_t a = speed < kIdleBelow ? kAnimStand : speed < kRunAbove * scale / 1.2f ? kAnimWalk : kAnimRun;
            if (a != st.anim.animation || a != kAnimStand) animation(s, a, false, nullptr, now_ms, st);
        }
        if (st.loop != 0) {
            audio_.set_spawn_loop(id, st.loop, s.display_valid ? s.display_position : s.position, height_of(s));
            alive_.push_back(id);
        }
    }
    audio_.drop_spawn_loops_except(alive_);
    // Every spawn has its state (made above), so the two differ in size exactly when some have gone.
    if (spawns_.size() != world.spawns.size())
        std::erase_if(spawns_, [&](const auto& kv) { return !world.spawns.contains(kv.first); });
}

} // namespace hailstone::app
