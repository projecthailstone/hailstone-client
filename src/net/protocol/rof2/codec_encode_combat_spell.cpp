#include "net/protocol/rof2/codec_dispatch.h"
#include "net/messages/zone.h"
#include "net/messages/spell.h"
#include "net/protocol/rof2/wire_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "net/protocol/rof2/packets/ability_packets.h"
#include "net/protocol/rof2/packets/spell_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

namespace hailstone::net::rof2::detail {
using namespace messages;

template<class T>
const T* payload(const GameMessage& m) { return messages::message_get<T>(m); }

std::optional<AppPacket> encode_combat_spell(GameOpcode opcode, const GameMessage& m) {
    switch (opcode) {
    case GameOpcode::AutoAttack:
    case GameOpcode::AutoAttack2:
        if (const auto* x = payload<AttackToggle>(m)) return net::encode_auto_attack(opcode == GameOpcode::AutoAttack2, x->enabled);
        break;
    case GameOpcode::AutoFire:
        if (const auto* x = payload<AttackToggle>(m)) return net::encode_auto_fire(x->enabled);
        break;
    case GameOpcode::CombatAbility:
        if (const auto* x = payload<CombatAbilityRequest>(m))
            return net::encode_combat_ability(x->target, x->attack_slot, x->skill);
        break;
    case GameOpcode::CastSpell:
        if (const auto* x = payload<CastRequest>(m)) return net::encode(net::cast_spell_packet(x->cast));
        break;
    case GameOpcode::ManaChange:
        return messages::message_is<std::monostate>(m)
            ? std::optional(net::encode_stop_cast_request()) : std::nullopt;
    case GameOpcode::Animation:
        if (const auto* x = payload<Animation>(m)) return net::encode(*x);
        break;
    case GameOpcode::MemorizeSpell:
        if (const auto* x = payload<MemorizationEvent>(m))
            return net::encode(net::MemorizeSpell{x->slot, x->spell_id, x->scribing, x->reduction});
        break;
    case GameOpcode::BuffRemoveRequest:
        if (const auto* x = payload<BuffRemoval>(m)) return net::encode_buff_remove_request(x->slot, x->entity_id);
        break;
    case GameOpcode::Buff:
        if (const auto* x = payload<BuffChange>(m)) {
            net::BuffPacket p;
            p.entity_id = x->entity_id; p.slot = x->slot; p.fade = x->fade;
            p.buff.effect_type = x->buff.effect_type; p.buff.level = x->buff.level;
            p.buff.bard_modifier = x->buff.bard_modifier; p.buff.spell_id = x->buff.spell_id;
            p.buff.duration = x->buff.duration; p.buff.caster_id = x->buff.caster_id;
            p.buff.num_hits = x->buff.num_hits;
            std::copy(x->buff.slot_data.begin(), x->buff.slot_data.end(), std::begin(p.buff.slot_data));
            return net::encode(p);
        }
        break;
    case GameOpcode::BlockedBuffs:
    case GameOpcode::RemoveBlockedBuffs:
        if (const auto* x = payload<BlockedSpellList>(m)) {
            net::BlockedBuffs p; p.spells = x->spells; p.pet = x->pet;
            p.initialise = x->initialise; p.flags = x->flags; p.count = x->count;
            return net::encode(p, opcode == GameOpcode::RemoveBlockedBuffs);
        }
        break;
    case GameOpcode::ClearBlockedBuffs:
        if (const auto* x = payload<ClearBlockedSpellList>(m)) return net::encode_clear_blocked_buffs(x->pet);
        break;
    case GameOpcode::LoadSpellSet:
        if (const auto* x = payload<SpellBookSet>(m)) return net::encode_load_spell_set(x->forget_gems);
        break;
    case GameOpcode::ClientUpdate:
        if (const auto* x = payload<ClientPositionUpdate>(m)) return net::encode(*x);
        break;
    case GameOpcode::ChannelMessage:
        if (const auto* x = payload<ChannelMessage>(m)) return net::encode(*x);
        break;
    case GameOpcode::ZoneChange:
        if (const auto* x = payload<ZoneChange>(m)) return net::encode(*x);
        break;
    case GameOpcode::SpawnAppearance:
        if (const auto* x = payload<SpawnAppearance>(m)) return net::encode(*x);
        break;
    case GameOpcode::WearChange:
        if (const auto* x = payload<WearChange>(m)) return net::encode(*x);
        break;
    case GameOpcode::TimeOfDay:
        if (const auto* x = payload<TimeOfDay>(m)) return net::encode(*x);
        break;
    case GameOpcode::Weather:
        if (const auto* x = payload<Weather>(m)) return net::encode(*x);
        break;
    case GameOpcode::Damage:
        if (const auto* x = payload<CombatDamage>(m)) return net::encode(*x);
        break;
    default: break;
    }
    return std::nullopt;
}

} // namespace hailstone::net::rof2::detail
