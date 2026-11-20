#pragma once

// The ItemBase getters the character rules use, on net::Item (the item and
// its children: augments in a worn item's sockets, contents in a bag),
// checked against the original's vectors.
//
// The instance's stat scale (ItemBase::ScriptIndex, +0x70) is the item
// header's scaled_value (the header's sixth field, read into +0x70 by the
// item unserializer @0x5553b0); a charm (CharmFileID set) counts its stats at
// |scale| percent unless the scale is 100.

#include <cstdint>

#include "net/messages/item.h"

namespace hailstone::game::mechanics {

// SumIntStatRecursive / SumByteStatRecursive @0x7b4280 / 0x7b40a0 in aug mode
// (the per-stat getters' (recurse, 0, 1)): the item's own value (charm-scaled)
// plus its children's, children in scaling sockets at the aug stat percent.
// Only plain items (Type 0) count.
std::int32_t item_stat(const net::Item& item, std::int32_t (*field)(const net::Item&), bool recurse = true);

// The same sum with the getters' augTotal out-parameter (SumStat in aug mode): `aug_total` gets the part that comes
// from augments in sockets whose +5 flag is set (the scaled value of each such child, plus what its own flagged
// children add). The item display colours a stat by it (CItemDisplayWnd::CreateModString @0069dd80).
std::int32_t item_stat(const net::Item& item, std::int32_t (*field)(const net::Item&), std::int32_t& aug_total);

// ItemBase_GetAugStatPercent @0x7b2fe0.
std::int32_t aug_stat_percent(const net::Item& item, bool recurse = true);
// ItemBase_ApplyAugStatPercent @0x7b3d30 (this = the socket's owner).
std::int32_t apply_aug_stat_percent(const net::Item& owner, int socket, std::int32_t value, bool apply = true);
// ItemBase_GetMaxIntFieldRecursive @0x7b1810: required / recommended level, recommended skill, prestige.
std::int32_t item_max_field(const net::Item& item, std::int32_t (*field)(const net::Item&), bool recurse = true);
std::int32_t item_required_level(const net::Item& item);
std::int32_t item_recommended_level(const net::Item& item);
std::int32_t item_recommended_skill(const net::Item& item);
std::int32_t item_prestige(const net::Item& item);
// ItemBase_GetRaces @0x7b4e30 / GetDeity @0x7b2060 (masks ANDed with the children's).
std::uint32_t item_races(const net::Item& item);
std::uint32_t item_deity(const net::Item& item);
// EQ_Character::GetUsableClasses(1, 0) @007b4ce0: the class mask ANDed with the children's (0 for non-Type-0 items).
std::uint32_t item_classes(const net::Item& item);
// ItemBase_GetEquipSlots(recurse) @0x7b2210: the equip slot mask, ANDed with the children's masks with recurse; 0
// for non-Type-0 items.
std::uint32_t item_equip_slots(const net::Item& item, bool recurse = true);
// ItemBase_GetHaste @0x7ba570: the largest haste of the item and its children, at least `best`.
std::int32_t item_haste(const net::Item& item, std::int32_t best = 0, bool recurse = true);
// ItemBase_GetSkillModForSkill @0x7b2d10.
std::int32_t item_skill_mod(const net::Item& item, std::int32_t skill, std::int32_t base);
// ItemBase_GetWeight(recurse, augTotal, augMode) @0x7b6880: SumIntStatRecursive(+0x108, recurse && the item's
// definition is null, augTotal, checkType 0, augMode, ignoreDisabled 1). Every item has a definition, so this is
// the item's own weight field (tenths), charm-scaled, of any Type: an augment's weight is not added to its owner's,
// a bag's contents are not added (CalcCarriedWeight adds them itself), and nothing multiplies by the stack count
// (+0x98 is never read: a stack of 20 weighs what one does).
std::int32_t item_weight(const net::Item& item);
// ItemBase_GetSize(recurse) @0x7b2110: the size byte, or for a Type 0 item with recurse the largest size inside it.
std::uint32_t item_size(const net::Item& item, bool recurse = true);
// ItemBase::GetItemValue(contents) @0x7ba920: Cost (+0xf4), plus for a Type 0 item with `contents` the
// GetItemValue(1) of each item inside it (0x7b4460's functor over +0xa0, summed with wrap-around).
std::int32_t item_value(const net::Item& item, bool contents = true);
// ItemBase_GetDmgBonusValue(skill, recurse) @0x7b9800: the damage bonus (+0x508, charm-scaled) when the bonus skill
// (+0x504) is `skill`, plus with recurse the contents' (in scaling sockets at the aug stat percent). Type 0 only.
std::int32_t item_dmg_bonus(const net::Item& item, std::int32_t skill, bool recurse = true);
// ItemBase_GetNumPotionSlots(recurse) @0x7b4f80: the potion belt slots (+0x578, charm-scaled) plus with recurse the
// contents' (in scaling sockets at the aug stat percent). Type 0 only; the enabled byte (+0x574) is not read.
std::int32_t item_potion_slots(const net::Item& item, bool recurse = true);
// ItemBase_GetAC(1, 0, 1).
std::int32_t item_ac(const net::Item& item, bool recurse = true);
// ItemTypeToSkill @0x59eb40: the skill an item type trains.
std::uint8_t item_type_to_skill(std::uint32_t item_type);

} // namespace hailstone::game::mechanics
