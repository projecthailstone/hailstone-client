// GameConnection: the ability commands that are not skills or casts of their own: /stopdisc, /melody and the bard
// twist, /dismount (net/connection/game_connection_mounts.cpp). /autofire is with the attack flags in
// game_connection.cpp, /discipline with doCombatAbility in game_connection_cast.cpp.

#include <algorithm>
#include <cctype>

#include "game/race_models.h"
#include "game/spells.h"
#include "net/connection/game_connection.h"
#include "net/messages/spell.h"

namespace hailstone::net {

namespace {

constexpr std::uint8_t kBard = 8;
constexpr std::int32_t kSpaCannotRemove = 101; // RemoveMyAffect: SpellAffects(0x65) refuses
constexpr std::int32_t kSpaSilence = 96;       // StopSpellCast's twist restart: a buff with SPA 0x60 stops it

} // namespace

bool GameConnection::bard_song(const game::SpellData& s) const {
    // CharacterZoneClient::BardCastBard @0x432960 for the profile's class (the server extension may refuse the song
    // by the record's own Bard byte), as StopSpellCast's own song test.
    if (!profile_ || profile_->class_ != kBard || s.is_skill) return false;
    if (server_ext().song_blocked(*this, s.class_level[kBard - 1])) return false;
    const std::uint8_t level = spell_level_needed(s, kBard);
    return level != 0 && level < 0xff;
}

// ---------------------------------------------------------------- disciplines

void GameConnection::stop_discipline() {
    // Cmd_stopdisc @0x478cb0: IsDisciplineActive, then RemoveMyAffect(GetFirstEffectSlot(0, 1)).
    if (state_ != ConnectionState::InZone || !discipline_active()) return;
    remove_my_affect(game::kBuffSlots - 1);
}

bool GameConnection::remove_my_affect(int slot) {
    // PcZoneClient::RemoveMyAffect @0x57fa50: GetEffect(slot) for a slot below GetMaxEffects; with the spawn and an
    // effect, a valid spell without SPA 101 that IsClickOffAllowed lets go is removed (RemovePCAffectex, then
    // NotifyPCAffectChange(slot, 1) tells the server). Anything else (also an empty slot) prints token 137.
    if (state_ != ConnectionState::InZone || slot < 0 || slot >= game::kBuffSlots) return false;
    const auto& b = spells_.buffs[static_cast<std::size_t>(slot)];
    if (const game::SpellData* s = b.empty() ? nullptr : spell_table_->get(b.spell)) {
        // EQ_Spell::IsClickOffAllowed @0x41d890 answers 1 (kept) for a detrimental spell or one with NoRemove.
        if (!s->has_effect(kSpaCannotRemove) && s->beneficial() && !s->no_remove) {
            // The server is asked as the buff window asks it (OP_BuffRemoveRequest); its fade clears the slot.
            // The original's own OP_Buff (NotifyPCAffectChange) is not sent (docs/unknowns.md).
            send_zone(messages::outgoing(messages::GameOpcode::BuffRemoveRequest,
        messages::BuffRemoval{static_cast<std::uint32_t>(slot), world_.player_id}));
            return true;
        }
    }
    system(137, {}, 0xd); // You cannot remove this effect.
    return false;
}

// ---------------------------------------------------------------- the bard twist

std::int32_t GameConnection::next_bard_twist_gem() {
    const auto count = static_cast<std::uint32_t>(bard_twist_.size());
    if (count == 0) return 0;
    if (bard_twist_pos_ >= count) bard_twist_pos_ = 0;
    return bard_twist_[bard_twist_pos_++];
}

std::int32_t GameConnection::current_bard_twist_gem() const {
    if (bard_twist_.empty()) return 0;
    const std::uint32_t index = bard_twist_pos_ != 0 ? bard_twist_pos_ - 1 : 0;
    return index < bard_twist_.size() ? bard_twist_[index] : 0;
}

void GameConnection::stop_current_spell_cast(std::uint8_t reason) {
    // StopCurrentSpellCast @0x433460 forwards the reason and current spell. StopSpellCast @0x4348a0 sends the
    // empty OP_ManaChange only for reason 0; ProcessAffects' reason 1 is already a local interruption.
    const game::SpellData* s = spell_table_->get(spells_.casting.spell);
    const bool song = s && bard_song(*s);
    if (!song) spells_.pending = std::max(0, spells_.pending - 1);
    // A bard's non-song (stop_spell_cast.cpp:84-101) ends at the launch data reset: no interrupt packet and no
    // twist restart, whatever the reason. A spell with recovery at most 100 ms that is not an item cast (the
    // +0x450/+0x454 test) is kept without the reset.
    if (profile_ && profile_->class_ == kBard && !song) {
        if (!(s && !spells_.casting.is_item_cast() && s->recovery_time <= 100)) spells_.casting = {};
        return;
    }
    if (!s) return;
    if (reason == 0) send_zone(messages::outgoing(messages::GameOpcode::ManaChange));
    spells_.casting = {};
    if (song) bard_song_stopped(reason);
}

void GameConnection::bard_song_stopped(std::uint32_t reason) {
    // StopSpellCast after the launch reset, for a bard song: the gem timers of memorised spells without a recast
    // (+0x18) cleared, the recovery lock (+0x3cc) cleared, "Your song ends abruptly." (12686, reason 1) or "Your
    // song ends." (12687), colour 0x108, unless chat filter 9 is 1.
    for (int gem = 0; gem < game::kSpellGems; ++gem) {
        auto& g = spells_.gems[static_cast<std::size_t>(gem)];
        const game::SpellData* m = g.spell != game::kNoSpell ? spell_table_->get(g.spell) : nullptr;
        if (!m || m->recast_time == 0) g.ready = 0;
    }
    spells_.recovery_ready = 0;
    if (chat_filters_.mode(9) != 1) system(reason == 1 ? 12686 : 12687, {}, 0x108);
    restart_bard_twist(static_cast<std::uint8_t>(reason));
}

void GameConnection::restart_bard_twist(std::uint8_t reason) {
    // Then, for the local bard and a reason other than 0, the twist's current gem 1..9 plays again (the cast
    // stopped first), unless a buff has SPA 96.
    if (reason == 0 || !profile_ || profile_->class_ != kBard) return;
    const std::int32_t gem = current_bard_twist_gem() - 1;
    if (static_cast<std::uint32_t>(gem) > 8) return;
    stop_current_spell_cast();
    for (const auto& b : spells_.buffs) {
        if (b.empty()) continue;
        if (const game::SpellData* s = spell_table_->get(b.spell); s && s->has_effect(kSpaSilence)) return;
    }
    cast_gem(gem);
}

void GameConnection::melody_command(const std::string& args) {
    // Cmd_melody @0x4f5bf0.
    if (state_ != ConnectionState::InZone || !profile_ || profile_->class_ != kBard) return;
    clear_bard_twist();
    // While a valid spell is cast: a bard song (BardCastBard with the character's class) stops, nothing else.
    if (const game::SpellData* s = spells_.casting.casting() ? spell_table_->get(spells_.casting.spell) : nullptr) {
        if (bard_song(*s)) stop_current_spell_cast();
        return;
    }
    // Each run of digits (sscanf "%d") above 0 joins the twist (0x594cf0: appended, no limit).
    for (std::size_t i = 0; i < args.size();) {
        if (!std::isdigit(static_cast<unsigned char>(args[i]))) {
            ++i;
            continue;
        }
        // The digits accumulate in 32 bits and wrap, as core::msvc_atoi reads the CRT's (a signed product would
        // overflow, undefined, for ten digits or more).
        std::uint32_t digits = 0;
        while (i < args.size() && std::isdigit(static_cast<unsigned char>(args[i])))
            digits = digits * 10u + static_cast<std::uint32_t>(args[i++] - '0');
        const auto gem = static_cast<std::int32_t>(digits);
        if (gem > 0) bard_twist_.push_back(gem);
    }
    // The twist's first gem is clicked in the cast window (HandleSpellLeftClick(gem - 1) for gems 1..12).
    const std::int32_t gem = next_bard_twist_gem() - 1;
    if (static_cast<std::uint32_t>(gem) <= 0xb) cast_gem(gem);
}

// ---------------------------------------------------------------- mounts

} // namespace hailstone::net
