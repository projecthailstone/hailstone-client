// RoF2-compatible SerializeItem field order shared by RoF2 packet processing
// and the existing bank.dat version 1 on-disk format. Do not change the
// encoding without explicitly versioning the persisted bank snapshot.

#include "net/wire/item_serialization.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "net/wire/byte_codec.h"

namespace hailstone::net {

using detail::write_cstring;

namespace {

constexpr int kMaxItemDepth = 4; // includes nested bag/augment contents
constexpr std::uint32_t kMaxContents = kMaxBagContents;
constexpr std::size_t kMinItemBytes = 64;
std::size_t reserve_for(std::uint32_t count, const ByteReader& r, std::size_t per_entry) {
    return std::min<std::size_t>(count, r.remaining() / per_entry);
}

double read_f64(ByteReader& r) {
    std::uint64_t bits = r.le<std::uint64_t>();
    double d;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}
void write_f64(ByteWriter& w, double d) {
    std::uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    w.le(bits);
}

template <typename T> void get(ByteReader& r, T& v) { v = r.le<T>(); }
void get(ByteReader& r, float& v) { v = r.f32(); }
template <typename T> void put(ByteWriter& w, T v) { w.le(v); }
void put(ByteWriter& w, float v) { w.f32(v); }

void read_effect(ByteReader& r, ItemEffect& e) {
    get(r, e.effect);
    get(r, e.level2);
    get(r, e.type);
    get(r, e.level);
    for (auto& x : e.extra) get(r, x);
    e.name = r.cstring();
    get(r, e.trailer);
}

void write_effect(ByteWriter& w, const ItemEffect& e) {
    put(w, e.effect);
    put(w, e.level2);
    put(w, e.type);
    put(w, e.level);
    for (auto x : e.extra) put(w, x);
    write_cstring(w, e.name);
    put(w, e.trailer);
}

Item read_item_at(ByteReader& r, int depth) {
    if (depth > kMaxItemDepth) throw std::out_of_range("item nesting too deep");
    Item it;
    // ItemSerializationHeader
    it.serial = r.fixed_string(17);
    get(r, it.stack_count);
    get(r, it.unknown004);
    it.slot.type = static_cast<std::int16_t>(r.le<std::uint8_t>());
    it.slot.slot = r.le<std::int16_t>();
    it.slot.sub = r.le<std::int16_t>();
    it.slot.aug = r.le<std::int16_t>();
    get(r, it.price);
    get(r, it.merchant_slot);
    get(r, it.scaled_value);
    get(r, it.instance_id);
    get(r, it.parcel_item_id);
    get(r, it.last_cast_time);
    get(r, it.charges);
    get(r, it.attuned);
    get(r, it.unknown044);
    get(r, it.unknown048);
    get(r, it.unknown052);
    get(r, it.is_evolving);
    if (it.is_evolving) {
        auto& e = it.evolving;
        get(r, e.final_item_id);
        get(r, e.evolve_level);
        e.progress = read_f64(r);
        get(r, e.activated);
        get(r, e.evolve_max_level);
        for (auto& b : e.unknown005) get(r, b);
    }
    it.ornament_main = r.cstring();
    it.ornament_off = r.cstring();
    // ItemSerializationHeaderFinish
    get(r, it.ornament_icon);
    get(r, it.unknowna1);
    get(r, it.ornament_hero_model);
    get(r, it.unknown063);
    get(r, it.copied);
    get(r, it.unknowna4);
    get(r, it.unknowna5);
    get(r, it.item_class);
    // SerializeItem writes the name only when it is not empty (no terminator
    // either); every real item has one.
    it.name = r.cstring();
    it.lore = r.cstring();
    it.id_file = r.cstring();
    it.id_file2 = r.cstring();
    // ItemBodyStruct
    get(r, it.id);
    get(r, it.weight);
    get(r, it.no_rent);
    get(r, it.no_drop);
    get(r, it.attuneable);
    get(r, it.size);
    get(r, it.equip_slots);
    get(r, it.value);
    get(r, it.icon);
    get(r, it.unknown1);
    get(r, it.unknown2);
    get(r, it.benefit_flag);
    get(r, it.tradeskills);
    for (std::int8_t* v : {&it.cr, &it.dr, &it.pr, &it.mr, &it.fr, &it.sv_corruption, &it.astr, &it.asta, &it.aagi,
                           &it.adex, &it.acha, &it.aint, &it.awis})
        get(r, *v);
    get(r, it.hp);
    get(r, it.mana);
    get(r, it.endurance);
    get(r, it.ac);
    get(r, it.regen);
    get(r, it.mana_regen);
    get(r, it.end_regen);
    get(r, it.classes);
    get(r, it.races);
    get(r, it.deity);
    get(r, it.skill_mod_value);
    get(r, it.skill_mod_max);
    get(r, it.skill_mod_type);
    get(r, it.skill_mod_extra);
    get(r, it.bane_dmg_race);
    get(r, it.bane_dmg_body);
    get(r, it.bane_dmg_race_amt);
    get(r, it.bane_dmg_amt);
    get(r, it.magic);
    get(r, it.cast_time_);
    get(r, it.req_level);
    get(r, it.rec_level);
    get(r, it.rec_skill);
    get(r, it.bard_type);
    get(r, it.bard_value);
    get(r, it.light);
    get(r, it.delay);
    get(r, it.elem_dmg_type);
    get(r, it.elem_dmg_amt);
    get(r, it.range);
    get(r, it.damage);
    get(r, it.color);
    get(r, it.prestige);
    get(r, it.item_type);
    get(r, it.material);
    get(r, it.material_unknown1);
    get(r, it.elite_material);
    get(r, it.heros_forge_model);
    get(r, it.material_unknown2);
    get(r, it.sell_rate);
    for (std::int32_t* v : {&it.combat_effects, &it.shielding, &it.stun_resist, &it.strike_through,
                            &it.extra_dmg_skill, &it.extra_dmg_amt, &it.spell_shield, &it.avoidance, &it.accuracy})
        get(r, *v);
    get(r, it.charm_file_id);
    for (int i = 0; i < 4; ++i) {
        get(r, it.faction_mod[static_cast<std::size_t>(i)]);
        get(r, it.faction_amt[static_cast<std::size_t>(i)]);
    }
    it.charm_file = r.cstring();
    // ItemSecondaryBodyStruct
    get(r, it.aug_type);
    get(r, it.aug_restrict2);
    get(r, it.aug_restrict);
    for (auto& s : it.aug_sockets) {
        get(r, s.type);
        get(r, s.visible);
        get(r, s.unknown);
    }
    get(r, it.ldon_point_type);
    get(r, it.ldon_theme);
    get(r, it.ldon_price);
    get(r, it.ldon_sellback_rate);
    get(r, it.ldon_sold);
    get(r, it.bag_type);
    get(r, it.bag_slots);
    get(r, it.bag_size);
    get(r, it.weight_reduction);
    get(r, it.book);
    get(r, it.book_type);
    it.book_file = r.cstring();
    // ItemTertiaryBodyStruct
    get(r, it.lore_group);
    get(r, it.artifact);
    get(r, it.summoned);
    get(r, it.favor);
    get(r, it.fv_no_drop);
    get(r, it.dot_shield);
    get(r, it.attack);
    get(r, it.haste);
    get(r, it.damage_shield);
    get(r, it.guild_favor);
    get(r, it.aug_distiller);
    get(r, it.unknown3);
    get(r, it.unknown4);
    get(r, it.no_pet);
    get(r, it.unknown5);
    get(r, it.potion_belt_enabled);
    get(r, it.potion_belt_slots);
    get(r, it.max_stack);
    get(r, it.no_transfer);
    get(r, it.expendable_arrow);
    for (auto& v : it.unknown8_11) get(r, v);
    for (auto& v : it.unknown12_14) get(r, v);
    // Click, proc, worn, focus, scroll, bard.
    for (auto& e : it.effects) read_effect(r, e);
    // ItemQuaternaryBodyStruct
    get(r, it.script_file_id);
    get(r, it.quest_item);
    get(r, it.power);
    get(r, it.purity);
    get(r, it.unknown16);
    get(r, it.backstab_dmg);
    get(r, it.ds_mitigation);
    for (std::int32_t* v : {&it.heroic_str, &it.heroic_int, &it.heroic_wis, &it.heroic_agi, &it.heroic_dex,
                            &it.heroic_sta, &it.heroic_cha, &it.heroic_mr, &it.heroic_fr, &it.heroic_cr, &it.heroic_dr,
                            &it.heroic_pr, &it.heroic_sv_corrup, &it.heal_amt, &it.spell_dmg, &it.clairvoyance,
                            &it.sub_type})
        get(r, *v);
    get(r, it.evolve_string);
    get(r, it.unknown19);
    get(r, it.unknown20);
    get(r, it.unknown21);
    get(r, it.heirloom);
    get(r, it.placeable);
    for (auto& v : it.unknown22) get(r, v);
    for (auto& v : it.unknown23_25) get(r, v);
    get(r, it.unknown26);
    get(r, it.unknown27);
    get(r, it.unknown_rof_6);
    get(r, it.unknown28);
    get(r, it.unknown29);
    get(r, it.unknown30);
    get(r, it.unknown31);
    get(r, it.unknown32);
    get(r, it.unknown33);
    for (auto& v : it.unknown34_37) get(r, v);
    get(r, it.no_zone);
    for (auto& v : it.unknown_rof_7) get(r, v);
    get(r, it.no_ground);
    for (auto& v : it.unknown_rof_8) get(r, v);
    get(r, it.unknown37a);
    get(r, it.unknown38);
    get(r, it.unknown39);
    // Contents.
    std::uint32_t count = r.le<std::uint32_t>();
    if (count > kMaxContents) throw std::out_of_range("too many bag contents");
    it.contents.reserve(reserve_for(count, r, 4 + kMinItemBytes)); // each entry: its u32 index and the item
    for (std::uint32_t i = 0; i < count; ++i) {
        BagItem b;
        b.index = r.le<std::uint32_t>();
        if (b.index >= kMaxContents) throw std::out_of_range("bag index out of range");
        b.item = read_item_at(r, depth + 1);
        it.contents.push_back(std::move(b));
    }
    return it;
}

} // namespace

const Item* Item::content(std::uint32_t index) const {
    for (const auto& b : contents)
        if (b.index == index) return &b.item;
    return nullptr;
}

Item* Item::content(std::uint32_t index) {
    for (auto& b : contents)
        if (b.index == index) return &b.item;
    return nullptr;
}

Item read_item(ByteReader& r) { return read_item_at(r, 0); }

void write_item(ByteWriter& w, const Item& it) {
    w.fixed_string(it.serial, 17);
    put(w, it.stack_count);
    put(w, it.unknown004);
    put(w, static_cast<std::uint8_t>(it.slot.type));
    put(w, it.slot.slot);
    put(w, it.slot.sub);
    put(w, it.slot.aug);
    put(w, it.price);
    put(w, it.merchant_slot);
    put(w, it.scaled_value);
    put(w, it.instance_id);
    put(w, it.parcel_item_id);
    put(w, it.last_cast_time);
    put(w, it.charges);
    put(w, it.attuned);
    put(w, it.unknown044);
    put(w, it.unknown048);
    put(w, it.unknown052);
    put(w, it.is_evolving);
    if (it.is_evolving) {
        const auto& e = it.evolving;
        put(w, e.final_item_id);
        put(w, e.evolve_level);
        write_f64(w, e.progress);
        put(w, e.activated);
        put(w, e.evolve_max_level);
        for (auto b : e.unknown005) put(w, b);
    }
    write_cstring(w, it.ornament_main);
    write_cstring(w, it.ornament_off);
    put(w, it.ornament_icon);
    put(w, it.unknowna1);
    put(w, it.ornament_hero_model);
    put(w, it.unknown063);
    put(w, it.copied);
    put(w, it.unknowna4);
    put(w, it.unknowna5);
    put(w, it.item_class);
    write_cstring(w, it.name);
    write_cstring(w, it.lore);
    write_cstring(w, it.id_file);
    write_cstring(w, it.id_file2);
    put(w, it.id);
    put(w, it.weight);
    put(w, it.no_rent);
    put(w, it.no_drop);
    put(w, it.attuneable);
    put(w, it.size);
    put(w, it.equip_slots);
    put(w, it.value);
    put(w, it.icon);
    put(w, it.unknown1);
    put(w, it.unknown2);
    put(w, it.benefit_flag);
    put(w, it.tradeskills);
    for (std::int8_t v : {it.cr, it.dr, it.pr, it.mr, it.fr, it.sv_corruption, it.astr, it.asta, it.aagi, it.adex,
                          it.acha, it.aint, it.awis})
        put(w, v);
    put(w, it.hp);
    put(w, it.mana);
    put(w, it.endurance);
    put(w, it.ac);
    put(w, it.regen);
    put(w, it.mana_regen);
    put(w, it.end_regen);
    put(w, it.classes);
    put(w, it.races);
    put(w, it.deity);
    put(w, it.skill_mod_value);
    put(w, it.skill_mod_max);
    put(w, it.skill_mod_type);
    put(w, it.skill_mod_extra);
    put(w, it.bane_dmg_race);
    put(w, it.bane_dmg_body);
    put(w, it.bane_dmg_race_amt);
    put(w, it.bane_dmg_amt);
    put(w, it.magic);
    put(w, it.cast_time_);
    put(w, it.req_level);
    put(w, it.rec_level);
    put(w, it.rec_skill);
    put(w, it.bard_type);
    put(w, it.bard_value);
    put(w, it.light);
    put(w, it.delay);
    put(w, it.elem_dmg_type);
    put(w, it.elem_dmg_amt);
    put(w, it.range);
    put(w, it.damage);
    put(w, it.color);
    put(w, it.prestige);
    put(w, it.item_type);
    put(w, it.material);
    put(w, it.material_unknown1);
    put(w, it.elite_material);
    put(w, it.heros_forge_model);
    put(w, it.material_unknown2);
    put(w, it.sell_rate);
    for (std::int32_t v : {it.combat_effects, it.shielding, it.stun_resist, it.strike_through, it.extra_dmg_skill,
                           it.extra_dmg_amt, it.spell_shield, it.avoidance, it.accuracy})
        put(w, v);
    put(w, it.charm_file_id);
    for (std::size_t i = 0; i < 4; ++i) {
        put(w, it.faction_mod[i]);
        put(w, it.faction_amt[i]);
    }
    write_cstring(w, it.charm_file);
    put(w, it.aug_type);
    put(w, it.aug_restrict2);
    put(w, it.aug_restrict);
    for (const auto& s : it.aug_sockets) {
        put(w, s.type);
        put(w, s.visible);
        put(w, s.unknown);
    }
    put(w, it.ldon_point_type);
    put(w, it.ldon_theme);
    put(w, it.ldon_price);
    put(w, it.ldon_sellback_rate);
    put(w, it.ldon_sold);
    put(w, it.bag_type);
    put(w, it.bag_slots);
    put(w, it.bag_size);
    put(w, it.weight_reduction);
    put(w, it.book);
    put(w, it.book_type);
    write_cstring(w, it.book_file);
    put(w, it.lore_group);
    put(w, it.artifact);
    put(w, it.summoned);
    put(w, it.favor);
    put(w, it.fv_no_drop);
    put(w, it.dot_shield);
    put(w, it.attack);
    put(w, it.haste);
    put(w, it.damage_shield);
    put(w, it.guild_favor);
    put(w, it.aug_distiller);
    put(w, it.unknown3);
    put(w, it.unknown4);
    put(w, it.no_pet);
    put(w, it.unknown5);
    put(w, it.potion_belt_enabled);
    put(w, it.potion_belt_slots);
    put(w, it.max_stack);
    put(w, it.no_transfer);
    put(w, it.expendable_arrow);
    for (auto v : it.unknown8_11) put(w, v);
    for (auto v : it.unknown12_14) put(w, v);
    for (const auto& e : it.effects) write_effect(w, e);
    put(w, it.script_file_id);
    put(w, it.quest_item);
    put(w, it.power);
    put(w, it.purity);
    put(w, it.unknown16);
    put(w, it.backstab_dmg);
    put(w, it.ds_mitigation);
    for (std::int32_t v : {it.heroic_str, it.heroic_int, it.heroic_wis, it.heroic_agi, it.heroic_dex, it.heroic_sta,
                           it.heroic_cha, it.heroic_mr, it.heroic_fr, it.heroic_cr, it.heroic_dr, it.heroic_pr,
                           it.heroic_sv_corrup, it.heal_amt, it.spell_dmg, it.clairvoyance, it.sub_type})
        put(w, v);
    put(w, it.evolve_string);
    put(w, it.unknown19);
    put(w, it.unknown20);
    put(w, it.unknown21);
    put(w, it.heirloom);
    put(w, it.placeable);
    for (auto v : it.unknown22) put(w, v);
    for (auto v : it.unknown23_25) put(w, v);
    put(w, it.unknown26);
    put(w, it.unknown27);
    put(w, it.unknown_rof_6);
    put(w, it.unknown28);
    put(w, it.unknown29);
    put(w, it.unknown30);
    put(w, it.unknown31);
    put(w, it.unknown32);
    put(w, it.unknown33);
    for (auto v : it.unknown34_37) put(w, v);
    put(w, it.no_zone);
    for (auto v : it.unknown_rof_7) put(w, v);
    put(w, it.no_ground);
    for (auto v : it.unknown_rof_8) put(w, v);
    put(w, it.unknown37a);
    put(w, it.unknown38);
    put(w, it.unknown39);
    put(w, static_cast<std::uint32_t>(it.contents.size()));
    for (const auto& b : it.contents) {
        put(w, b.index);
        write_item(w, b.item);
    }
}

} // namespace hailstone::net
