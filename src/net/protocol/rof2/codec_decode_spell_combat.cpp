#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/zone.h"
#include "net/messages/notice.h"
#include "net/messages/pet.h"
#include "net/messages/spell.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/packets/pet_packets.h"
#include "net/protocol/rof2/packets/spell_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

std::optional<GameMessage> decode_spell_combat(GameOpcode op, const AppPacket& p) {
    switch (op) {
    case GameOpcode::BeginCast:
        if (auto x = net::decode_begin_cast(p))
            return GameMessage{op, CastStarted{x->spell_id, x->caster_id, x->cast_time}};
        break;
    case GameOpcode::InterruptCast:
        if (auto x = net::decode_interrupt_cast(p))
            return GameMessage{op, CastInterrupted{x->spawn_id, x->message_id, std::move(x->args)}};
        break;
    case GameOpcode::Action:
        if (auto x = net::decode_action(p))
            return GameMessage{op, SpellAction{x->target, x->source, x->level, x->instrument_mod,
                x->force, x->hit_heading, x->hit_pitch, x->type, x->damage, x->spell_id,
                x->unknown31, x->spell_level, x->effect_flag}};
        break;
    case GameOpcode::RestState: {
        if (p.data.empty()) break;
        hailstone::ByteReader reader(p.data);
        const bool in_combat = reader.le<std::uint8_t>() != 0;
        std::uint32_t downtime = 0;
        if (!in_combat && reader.remaining() >= 4) downtime = reader.le<std::uint32_t>();
        return GameMessage{op, RestCombatStatus{in_combat, downtime}};
    }
    case GameOpcode::MemorizeSpell:
        if (auto x = net::decode_memorize_spell(p))
            return GameMessage{op, MemorizationEvent{x->slot, x->spell_id, x->scribing, x->reduction}};
        break;
    case GameOpcode::Buff:
        if (auto x = net::decode_buff(p)) {
            BuffChange update; update.entity_id = x->entity_id; update.slot = x->slot; update.fade = x->fade;
            update.buff.effect_type = x->buff.effect_type; update.buff.level = x->buff.level;
            update.buff.bard_modifier = x->buff.bard_modifier; update.buff.spell_id = x->buff.spell_id;
            update.buff.duration = x->buff.duration; update.buff.caster_id = x->buff.caster_id;
            update.buff.num_hits = x->buff.num_hits;
            std::copy(std::begin(x->buff.slot_data), std::end(x->buff.slot_data), update.buff.slot_data.begin());
            return GameMessage{op, std::move(update)};
        }
        break;
    case GameOpcode::BuffCreate:
    case GameOpcode::TargetBuffs:
    case GameOpcode::PetBuffWindow:
        // EQEmu translates OP_PetBuffWindow to BuffCreate's icon layout;
        // preserve the dedicated semantic opcode for the focused pet UI.
        if (auto x = net::decode_buff_icons(p)) {
            BuffWindow view; view.entity_id = x->entity_id; view.tic_timer = x->tic_timer;
            view.all_buffs = x->all_buffs != 0; view.type = x->type;
            view.entries.reserve(x->entries.size());
            for (const auto& b : x->entries)
                view.entries.push_back({b.slot, b.spell_id, b.tics, b.num_hits, b.caster});
            return GameMessage{op, std::move(view)};
        }
        break;
    case GameOpcode::BlockedBuffs:
    case GameOpcode::RemoveBlockedBuffs:
        if (auto x = net::decode_blocked_buffs(p))
            return GameMessage{op, BlockedSpellList{std::move(x->spells), x->pet, x->initialise, x->flags, x->count}};
        break;
    case GameOpcode::ClearBlockedBuffs:
        if (!p.data.empty()) return GameMessage{op, ClearBlockedSpellList{p.data[0] != std::byte{0}}};
        break;
    case GameOpcode::SpellEffect:
        if (auto x = net::decode_spell_effect(p))
            return GameMessage{op, SpellVisualEffect{x->effect, x->caster, x->target, x->stage1_ms,
                x->stage1_length_ms, x->stage2_length_ms, x->full, x->add}};
        break;
    case GameOpcode::AddNimbusEffect:
    case GameOpcode::RemoveNimbusEffect:
        if (auto x = net::decode_nimbus_effect(p))
            return GameMessage{op, NimbusVisualEffect{x->spawn_id, x->effect}};
        break;
    case GameOpcode::DeleteSpell:
        if (auto x = net::decode_delete_spell(p))
            return GameMessage{op, SpellBookDelete{x->book_slot, x->success != 0}};
        break;
    case GameOpcode::SwapSpell:
        if (auto x = net::decode_swap_spell(p))
            return GameMessage{op, SpellBookSwap{x->from_slot, x->to_slot}};
        break;
    case GameOpcode::ColoredText:
        if (auto x = net::decode_colored_text(p)) return GameMessage{op, SpellText{x->color, std::move(x->text)}};
        break;
    case GameOpcode::Stamina:
        if (auto x = net::decode_stamina(p)) return GameMessage{op, HungerThirst{x->food, x->water}};
        break;
    case GameOpcode::LinkedReuse:
        if (auto x = net::decode_linked_reuse(p))
            return GameMessage{op, LinkedSpellReuse{x->timer_id, x->end_time, x->start_time}};
        break;
    case GameOpcode::DisciplineTimer:
        if (auto x = net::decode_discipline_timer(p))
            return GameMessage{op, DisciplineReuse{x->timer_id, x->seconds}};
        break;
    case GameOpcode::PetCommandState:
        if (auto v = net::decode_pet_command_state(p))
            return GameMessage{op, PetButtonState{v->button, v->state != 0}};
        break;
    case GameOpcode::PetHoTT:
        if (auto v = net::decode_pet_hott(p)) return GameMessage{op, PetTargetState{*v}};
        break;
    case GameOpcode::TargetHoTT:
        if (auto v = net::decode_pet_hott(p)) return GameMessage{op, TargetOfTargetState{*v}};
        break;
    case GameOpcode::Charm:
        if (auto v = net::decode_charm(p))
            return GameMessage{op, PetCharmState{v->owner, v->pet, v->on}};
        break;
    case GameOpcode::ControlBoat:
        if (p.data.size() >= 5) {
            hailstone::ByteReader r(p.data);
            const auto id = r.le<std::uint32_t>();
            const auto helm = r.le<std::uint8_t>();
            return GameMessage{op, BoatControl{id, helm == 1}};
        }
        break;
    case GameOpcode::Stun:
        if (auto v = net::decode_stun(p)) return GameMessage{op, StunNotice{v->duration_ms, v->lean}};
        break;
    case GameOpcode::Untargetable:
        if (auto v = net::decode_untargetable(p)) return GameMessage{op, TargetabilityNotice{v->spawn, v->targetable}};
        break;
    case GameOpcode::YellForHelp:
        if (auto v = net::decode_yell_for_help(p)) return GameMessage{op, AssistanceNotice{*v}};
        break;
    case GameOpcode::Weblink:
        if (auto v = net::decode_weblink(p)) return GameMessage{op, ExternalLinkNotice{std::move(*v)}};
        break;
    case GameOpcode::DisciplineUpdate:
        if (auto v = net::decode_disciplines(p)) return GameMessage{op, LearnedDisciplines{std::move(*v)}};
        break;
    case GameOpcode::ItemRecastDelay:
        if (auto v = net::decode_item_recast_delay(p))
            return GameMessage{op, ItemReuseNotice{v->delay_s, v->type, v->ignore_casting}};
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
