#pragma once

// Spawn appearance rules that do not depend on the model files: which
// dressing path a spawn uses and how armour updates change it. Sources:
// rof2decomp notes/spawns-races-models.md §2.8 (OP_WearChange), §3 row 30
// (EQPlayer::UnSerializeSpawn) and §4 (PlayerClient::ApplyBodyTexture,
// RefreshEquipSlot, SetHead).

#include <cstdint>

#include "game/world_state.h"

namespace nmsc::game {

// Visible equipment slots (WearChange / spawn order).
enum EquipSlot : std::uint8_t {
    kSlotHead = 0,
    kSlotChest = 1,
    kSlotArms = 2,
    kSlotWrists = 3,
    kSlotHands = 4,
    kSlotLegs = 5,
    kSlotFeet = 6,
    kSlotPrimary = 7,
    kSlotSecondary = 8,
};
inline constexpr std::uint8_t kNoTexture = 0xff; // Appearance::texture: per-slot armour

// PlayerZoneClient::LegalPlayerRace @0049deb0: races that wear armour per
// slot (< 13, 128, 130, 330, 522).
bool is_pc_model_race(std::uint32_t race);

// The unserializer's fold (@00591acb): for a PC-model race a TextureType
// >= 0 becomes the material of slots 1-6 and TextureType becomes -1.
void normalize_appearance(Appearance& a, std::uint32_t race);

// The appearance a spawn of `race` is drawn with. A race outside
// is_pc_model_race shows no armour colours (slots 0-6): seen in the RoF2
// client with a player under a skeleton illusion in dyed armour, whose bones
// stay undyed although the server re-sends every slot's tint after
// OP_Illusion. Where the original drops them is not found
// (docs/unknowns.md); SwapBodyMaterial itself has no race rule.
Appearance drawn_appearance(const Appearance& a, std::uint32_t race);

// OP_WearChange (§2.8). Returns false when the client ignores it: a body
// slot of a spawn with a fixed body texture, or a slot >= 9.
bool apply_wear_change(Appearance& a, std::uint8_t slot, std::uint32_t material, std::uint32_t hero_forge,
                       std::uint32_t tint);

// Material numbers. 10..16 on the chest are robes (the arms and wrists then
// show the chest's texture); robe textures are CLK04..CLK10.
inline bool is_robe(std::uint32_t chest_material) { return chest_material >= 10 && chest_material <= 16; }

// The material a body slot (0-6) is drawn with: the slot's own material, the
// chest's for arms and wrists under a robe, else the body texture for NPCs.
// The head is bare unless show_helm (SwapHead @00594210).
std::uint32_t effective_material(const Appearance& a, int slot);

// The tint slot a body slot (0-6) is drawn with: a robe's chest tint for the arms and wrists under it, as their material
// is (the Luclin path, character_parts.cpp), else the slot's own.
int tint_slot(const Appearance& a, int slot);

// FUN_0040a5b0 (ActorBase, @0x40a5b0): the head material ids 0xf0 / 0xf1 of
// the old models by the actor's race and gender (female: gender 1): 0xf0 is a
// per-race helm item, 0xf1 the head 3 (Ogres a helm, Barbarians the 0xf0 one);
// any other id, or a race outside 1-12 and 128, stays as it is.
std::uint32_t map_head_material(std::uint32_t race, std::uint8_t gender, std::uint32_t id);

// What PlayerClient::SwapHead @0x594210 puts on an old (classic) model for
// head material `material` (rof2decomp batch 248, row 30): the head mesh
// <code>HE<variant>, plus an item IT<item> held on the head point when
// item != 0. 0xf0..0xff map through map_head_material; a result still
// >= 0xf0 is that item with HE00. 7 is 2 (3 for Iksar). Vah Shir (130) wear
// helmets as items only (1..3 and 0xf0 -> IT0x295.. / IT0x29a..).
struct HeadModel {
    std::uint32_t variant = 0;
    std::uint32_t item = 0;
};
HeadModel head_model(std::uint32_t race, std::uint8_t gender, std::uint32_t material, std::uint32_t hero_forge = 0);

} // namespace nmsc::game
