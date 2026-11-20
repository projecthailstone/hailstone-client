#include "net/protocol/rof2/packets/spell_packets.h"

#include <algorithm>
#include <array>
#include <bit>

#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

namespace {

void write_slot(ByteWriter& w, const InventorySlot& s) {
    w.le<std::int16_t>(s.type);
    w.le<std::int16_t>(s.unknown02);
    w.le<std::int16_t>(s.slot);
    w.le<std::int16_t>(s.sub_index);
    w.le<std::int16_t>(s.aug_index);
    w.le<std::int16_t>(s.unknown01);
}

InventorySlot read_slot(ByteReader& r) {
    InventorySlot s;
    s.type = r.le<std::int16_t>();
    s.unknown02 = r.le<std::int16_t>();
    s.slot = r.le<std::int16_t>();
    s.sub_index = r.le<std::int16_t>();
    s.aug_index = r.le<std::int16_t>();
    s.unknown01 = r.le<std::int16_t>();
    return s;
}

} // namespace

// ---------------------------------------------------------------- casting

AppPacket encode(const CastSpell& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.slot);
    w.le<std::uint32_t>(v.spell_id);
    write_slot(w, v.inventory_slot);
    w.le<std::uint32_t>(v.target_id);
    w.le<std::uint32_t>(v.spell_crc);
    w.le<std::uint8_t>(v.free_target);
    w.zeros(3);
    for (float f : v.position) w.f32(f);
    return make_packet(rof2::Op::CastSpell, std::move(w));
}

CastSpell cast_spell_packet(const game::mechanics::cast::Packet& p) {
    CastSpell c;
    c.slot = p.slot;
    c.spell_id = static_cast<std::uint32_t>(p.spell_id);
    auto lo = [](std::uint32_t v) { return static_cast<std::int16_t>(v & 0xffff); };
    auto hi = [](std::uint32_t v) { return static_cast<std::int16_t>(v >> 16); };
    c.inventory_slot.type = lo(p.item_location[0]);
    c.inventory_slot.unknown02 = hi(p.item_location[0]);
    c.inventory_slot.slot = lo(p.item_location[1]);
    c.inventory_slot.sub_index = hi(p.item_location[1]);
    c.inventory_slot.aug_index = lo(p.item_location[2]);
    c.inventory_slot.unknown01 = hi(p.item_location[2]);
    c.target_id = p.target_id;
    c.spell_crc = p.crc;
    c.free_target = p.has_position;
    for (std::size_t k = 0; k < 3; ++k) c.position[k] = std::bit_cast<float>(p.position[k]);
    return c;
}

std::optional<CastSpell> decode_cast_spell(const AppPacket& p) {
    if (p.data.size() != kCastSpellSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        CastSpell v;
        v.slot = r.le<std::uint32_t>();
        v.spell_id = r.le<std::uint32_t>();
        v.inventory_slot = read_slot(r);
        v.target_id = r.le<std::uint32_t>();
        v.spell_crc = r.le<std::uint32_t>();
        v.free_target = r.le<std::uint8_t>();
        r.skip(3);
        for (float& f : v.position) f = r.f32();
        return v;
    });
}

AppPacket encode(const MemorizeSpell& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.slot);
    w.le<std::uint32_t>(v.spell_id);
    w.le<std::uint32_t>(v.scribing);
    w.le<std::uint32_t>(v.reduction);
    return make_packet(rof2::Op::MemorizeSpell, std::move(w));
}

std::optional<MemorizeSpell> decode_memorize_spell(const AppPacket& p) {
    if (p.data.size() < 16) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        MemorizeSpell v;
        v.slot = r.le<std::uint32_t>();
        v.spell_id = r.le<std::uint32_t>();
        v.scribing = r.le<std::uint32_t>();
        v.reduction = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const BeginCast& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.spell_id);
    w.le<std::uint16_t>(v.caster_id);
    w.le<std::uint32_t>(v.cast_time);
    return make_packet(rof2::Op::BeginCast, std::move(w));
}

std::optional<BeginCast> decode_begin_cast(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        BeginCast v;
        v.spell_id = r.le<std::uint32_t>();
        v.caster_id = r.le<std::uint16_t>();
        v.cast_time = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const InterruptCast& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.spawn_id);
    w.le<std::uint32_t>(v.message_id);
    for (const auto& a : v.args) detail::write_cstring(w, a);
    return make_packet(rof2::Op::InterruptCast, std::move(w));
}

std::optional<InterruptCast> decode_interrupt_cast(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        InterruptCast v;
        v.spawn_id = r.le<std::uint32_t>();
        v.message_id = r.le<std::uint32_t>();
        // InterruptCast_ParseMessageArgs @0x4b7440: NUL-separated arguments;
        // a missing final terminator still ends the last one.
        while (!r.empty()) {
            auto rest = r.bytes(r.remaining());
            std::string all(reinterpret_cast<const char*>(rest.data()), rest.size());
            std::size_t pos = 0;
            while (pos < all.size()) {
                std::size_t nul = all.find('\0', pos);
                if (nul == std::string::npos) nul = all.size();
                v.args.push_back(all.substr(pos, nul - pos));
                pos = nul + 1;
            }
        }
        while (!v.args.empty() && v.args.back().empty()) v.args.pop_back();
        return v;
    });
}

AppPacket encode_stop_cast_request() { return make_empty(rof2::Op::ManaChange); }

// ---------------------------------------------------------------- buffs

AppPacket encode(const BuffPacket& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.entity_id);
    const SpellBuff& b = v.buff;
    w.le<std::uint8_t>(b.effect_type);
    w.le<std::uint8_t>(b.level);
    w.le<std::uint8_t>(b.unknown002);
    w.le<std::uint8_t>(b.unknown003);
    w.f32(b.bard_modifier);
    w.le<std::uint32_t>(b.spell_id);
    w.le<std::int32_t>(b.duration);
    w.le<std::uint32_t>(b.caster_id);
    w.le<std::uint32_t>(b.num_hits);
    w.f32(b.y);
    w.f32(b.x);
    w.f32(b.z);
    w.le<std::uint32_t>(b.unknown036);
    for (std::int32_t d : b.slot_data) w.le<std::int32_t>(d);
    w.le<std::uint32_t>(v.slot);
    w.le<std::uint32_t>(v.fade);
    return make_packet(rof2::Op::Buff, std::move(w));
}

std::optional<BuffPacket> decode_buff(const AppPacket& p) {
    if (p.data.size() != kBuffPacketSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        BuffPacket v;
        v.entity_id = r.le<std::uint32_t>();
        SpellBuff& b = v.buff;
        b.effect_type = r.le<std::uint8_t>();
        b.level = r.le<std::uint8_t>();
        b.unknown002 = r.le<std::uint8_t>();
        b.unknown003 = r.le<std::uint8_t>();
        b.bard_modifier = r.f32();
        b.spell_id = r.le<std::uint32_t>();
        b.duration = r.le<std::int32_t>();
        b.caster_id = r.le<std::uint32_t>();
        b.num_hits = r.le<std::uint32_t>();
        b.y = r.f32();
        b.x = r.f32();
        b.z = r.f32();
        b.unknown036 = r.le<std::uint32_t>();
        for (std::int32_t& d : b.slot_data) d = r.le<std::int32_t>();
        v.slot = r.le<std::uint32_t>();
        v.fade = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const BuffIcons& v, bool target_window) {
    ByteWriter w;
    w.le<std::uint32_t>(v.entity_id);
    w.le<std::uint32_t>(v.tic_timer);
    w.le<std::uint8_t>(v.all_buffs);
    w.le<std::uint16_t>(static_cast<std::uint16_t>(v.entries.size()));
    for (const auto& e : v.entries) {
        w.le<std::uint32_t>(e.slot);
        w.le<std::uint32_t>(e.spell_id);
        w.le<std::int32_t>(e.tics);
        w.le<std::uint32_t>(e.num_hits);
        detail::write_cstring(w, e.caster);
    }
    w.le<std::uint8_t>(v.type);
    return make_packet(target_window ? rof2::Op::TargetBuffs : rof2::Op::BuffCreate, std::move(w));
}

std::optional<BuffIcons> decode_buff_icons(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        BuffIcons v;
        v.entity_id = r.le<std::uint32_t>();
        v.tic_timer = r.le<std::uint32_t>();
        v.all_buffs = r.le<std::uint8_t>();
        auto count = r.le<std::uint16_t>();
        for (std::uint16_t i = 0; i < count; ++i) {
            BuffIconEntry e;
            e.slot = r.le<std::uint32_t>();
            e.spell_id = r.le<std::uint32_t>();
            e.tics = r.le<std::int32_t>();
            e.num_hits = r.le<std::uint32_t>();
            e.caster = r.cstring();
            v.entries.push_back(std::move(e));
        }
        // EQEmu sizes the packet for one terminator per name; the type byte follows.
        if (!r.empty()) v.type = r.le<std::uint8_t>();
        return v;
    });
}

AppPacket encode_buff_remove_request(std::uint32_t slot, std::uint32_t entity_id) {
    ByteWriter w;
    w.le<std::uint32_t>(slot);
    w.le<std::uint32_t>(entity_id);
    return make_packet(rof2::Op::BuffRemoveRequest, std::move(w));
}

// ---------------------------------------------------------------- hits

AppPacket encode(const Action& v) {
    ByteWriter w;
    w.le<std::uint16_t>(v.target);
    w.le<std::uint16_t>(v.source);
    w.le<std::uint16_t>(v.level);
    w.le<std::uint32_t>(v.unknown06);
    w.f32(v.instrument_mod);
    w.f32(v.force);
    w.f32(v.hit_heading);
    w.f32(v.hit_pitch);
    w.le<std::uint8_t>(v.type);
    w.le<std::uint32_t>(v.damage);
    w.le<std::uint16_t>(v.unknown31);
    w.le<std::uint32_t>(v.spell_id);
    w.le<std::uint8_t>(v.spell_level);
    w.le<std::uint8_t>(v.effect_flag);
    w.le<std::uint8_t>(v.spell_gem);
    write_slot(w, v.slot);
    w.le<std::uint32_t>(v.item_cast_type);
    return make_packet(rof2::Op::Action, std::move(w));
}

std::optional<Action> decode_action(const AppPacket& p) {
    if (p.data.size() < kActionSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        Action v;
        v.target = r.le<std::uint16_t>();
        v.source = r.le<std::uint16_t>();
        v.level = r.le<std::uint16_t>();
        v.unknown06 = r.le<std::uint32_t>();
        v.instrument_mod = r.f32();
        v.force = r.f32();
        v.hit_heading = r.f32();
        v.hit_pitch = r.f32();
        v.type = r.le<std::uint8_t>();
        v.damage = r.le<std::uint32_t>();
        v.unknown31 = r.le<std::uint16_t>();
        v.spell_id = r.le<std::uint32_t>();
        v.spell_level = r.le<std::uint8_t>();
        v.effect_flag = r.le<std::uint8_t>();
        v.spell_gem = r.le<std::uint8_t>();
        v.slot = read_slot(r);
        v.item_cast_type = r.le<std::uint32_t>();
        return v;
    });
}

// ---------------------------------------------------------------- book, messages

AppPacket encode(const DeleteSpell& v) {
    ByteWriter w;
    w.le<std::int16_t>(v.book_slot);
    w.zeros(2);
    w.le<std::uint8_t>(v.success);
    w.zeros(3);
    return make_packet(rof2::Op::DeleteSpell, std::move(w));
}

std::optional<DeleteSpell> decode_delete_spell(const AppPacket& p) {
    if (p.data.size() < 8) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        DeleteSpell v;
        v.book_slot = r.le<std::int16_t>();
        r.skip(2);
        v.success = r.le<std::uint8_t>();
        return v;
    });
}

AppPacket encode(const SwapSpell& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.from_slot);
    w.le<std::uint32_t>(v.to_slot);
    return make_packet(rof2::Op::SwapSpell, std::move(w));
}

std::optional<SwapSpell> decode_swap_spell(const AppPacket& p) {
    if (p.data.size() < kSwapSpellSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        SwapSpell v;
        v.from_slot = r.le<std::uint32_t>();
        v.to_slot = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const ColoredText& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.color);
    detail::write_cstring(w, v.text);
    return make_packet(rof2::Op::ColoredText, std::move(w));
}

std::optional<ColoredText> decode_colored_text(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        ColoredText v;
        v.color = r.le<std::uint32_t>();
        // EQEmu sizes the packet as the struct + strlen and copies without a
        // terminator; the struct's own padding usually supplies one.
        auto rest = r.bytes(r.remaining());
        std::string s(reinterpret_cast<const char*>(rest.data()), rest.size());
        if (auto nul = s.find('\0'); nul != std::string::npos) s.resize(nul);
        v.text = std::move(s);
        return v;
    });
}

std::optional<Stamina> decode_stamina(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        Stamina v;
        v.food = r.le<std::uint32_t>();
        v.water = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_load_spell_set(const std::vector<int>& forget_gems) {
    std::uint8_t gems[12] = {};
    for (int g : forget_gems)
        if (g >= 0 && g < 12) gems[g] = 1;
    ByteWriter w;
    for (std::uint8_t g : gems) w.le<std::uint8_t>(g);
    w.le<std::uint32_t>(12);
    return make_packet(rof2::Op::LoadSpellSet, std::move(w));
}

std::optional<LinkedReuse> decode_linked_reuse(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        LinkedReuse v;
        v.timer_id = r.le<std::uint32_t>();
        v.end_time = r.le<std::uint32_t>();
        v.start_time = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<DisciplineTimer> decode_discipline_timer(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        DisciplineTimer v;
        v.timer_id = r.le<std::uint32_t>();
        v.seconds = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const BlockedBuffs& v, bool remove) {
    ByteWriter w;
    for (int k = 0; k < kBlockedBuffSlots; ++k)
        w.le<std::int32_t>(k < static_cast<int>(v.spells.size()) ? v.spells[static_cast<std::size_t>(k)] : -1);
    w.le(v.count ? *v.count : static_cast<std::uint32_t>(std::min<std::size_t>(v.spells.size(), kBlockedBuffSlots)));
    w.le<std::uint8_t>(v.pet ? 1 : 0);
    w.le<std::uint8_t>(v.initialise ? 1 : 0);
    w.le(v.flags);
    return make_packet(remove ? rof2::Op::RemoveBlockedBuffs : rof2::Op::BlockedBuffs, std::move(w));
}

AppPacket encode_clear_blocked_buffs(bool pet) {
    ByteWriter w;
    w.le<std::uint8_t>(pet ? 1 : 0);
    return make_packet(rof2::Op::ClearBlockedBuffs, std::move(w));
}

std::optional<BlockedBuffs> decode_blocked_buffs(const AppPacket& p) {
    if (p.data.size() < kBlockedBuffsSize) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        std::array<std::int32_t, kBlockedBuffSlots> ids{};
        for (auto& id : ids) id = r.le<std::int32_t>();
        BlockedBuffs v;
        std::uint32_t count = r.le<std::uint32_t>();
        v.pet = r.le<std::uint8_t>() != 0;
        v.initialise = r.le<std::uint8_t>() != 0;
        v.flags = r.le<std::uint16_t>();
        for (std::uint32_t k = 0; k < count && k < ids.size(); ++k)
            if (ids[k] > 0) v.spells.push_back(ids[k]); // the server's removal answer zeroes the unused ids
        return v;
    });
}

std::optional<messages::AuraUpdate> decode_aura_update(const AppPacket& p) {
    return detail::decode_with(p, [&](ByteReader& r) {
        messages::AuraUpdate v;
        v.action = r.le<std::uint32_t>();
        if (v.action == kAuraAdd) {
            v.type = r.le<std::uint32_t>();
            v.name = r.fixed_string(64);
            v.spawn_id = r.le<std::uint32_t>();
            v.icon = r.le<std::uint32_t>();
        } else if (v.action == kAuraRemove) {
            v.spawn_id = r.le<std::uint32_t>();
        }
        return v;
    });
}

AppPacket encode(const messages::AuraUpdate& v) {
    ByteWriter w;
    w.le(v.action);
    if (v.action == kAuraAdd) {
        w.le(v.type);
        w.fixed_string(std::string_view(v.name).substr(0, 63), 64);
        w.le(v.spawn_id);
        w.le(v.icon);
    } else if (v.action == kAuraRemove) {
        w.le(v.spawn_id);
    }
    return make_packet(rof2::Op::UpdateAura, std::move(w));
}

AppPacket encode(const SpellEffectPacket& v) {
    ByteWriter w;
    w.le(v.effect);
    w.le(v.caster);
    w.le(v.target);
    w.le(v.stage1_ms);
    w.le(v.stage1_length_ms);
    w.le(v.stage2_length_ms);
    w.le<std::uint8_t>(v.full ? 1 : 0);
    w.le<std::uint8_t>(v.add ? 1 : 0);
    w.le(v.unknown);
    return make_packet(rof2::Op::SpellEffect, std::move(w));
}

std::optional<SpellEffectPacket> decode_spell_effect(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        SpellEffectPacket v;
        v.effect = r.le<std::uint32_t>();
        v.caster = r.le<std::uint32_t>();
        v.target = r.le<std::uint32_t>();
        v.stage1_ms = r.le<std::uint32_t>();
        v.stage1_length_ms = r.le<std::uint32_t>();
        v.stage2_length_ms = r.le<std::uint32_t>();
        v.full = r.le<std::uint8_t>() != 0;
        v.add = r.le<std::uint8_t>() != 0;
        v.unknown = r.le<std::uint16_t>();
        return v;
    });
}

AppPacket encode(const NimbusEffect& v, bool add) {
    ByteWriter w;
    w.le(v.spawn_id);
    w.le(v.effect);
    return make_packet(add ? rof2::Op::AddNimbusEffect : rof2::Op::RemoveNimbusEffect, std::move(w));
}

std::optional<NimbusEffect> decode_nimbus_effect(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        NimbusEffect v;
        v.spawn_id = r.le<std::uint32_t>();
        v.effect = r.le<std::int32_t>();
        return v;
    });
}

} // namespace hailstone::net
