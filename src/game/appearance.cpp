#include "game/appearance.h"

namespace nmsc::game {

bool is_pc_model_race(std::uint32_t race) {
    return race <= 12 || race == 128 || race == 130 || race == 330 || race == 522;
}

void normalize_appearance(Appearance& a, std::uint32_t race) {
    if (!is_pc_model_race(race)) return;
    if (a.texture != kNoTexture)
        for (int s = kSlotChest; s <= kSlotFeet; ++s) a.material[static_cast<std::size_t>(s)] = a.texture;
    a.texture = kNoTexture;
}

Appearance drawn_appearance(const Appearance& a, std::uint32_t race) {
    Appearance out = a;
    if (!is_pc_model_race(race))
        for (int s = kSlotHead; s <= kSlotFeet; ++s) out.tint[static_cast<std::size_t>(s)] = 0;
    return out;
}

bool apply_wear_change(Appearance& a, std::uint8_t slot, std::uint32_t material, std::uint32_t hero_forge,
                       std::uint32_t tint) {
    if (slot >= 9) return false;
    if (slot < kSlotPrimary && a.texture != kNoTexture) return false; // fixed body texture
    // material -1 resets the slot (the actor part goes back to its default).
    a.material[slot] = material == 0xffffffffu ? 0 : material;
    a.tint[slot] = tint;
    a.hero_forge[slot] = hero_forge;
    return true;
}

std::uint32_t effective_material(const Appearance& a, int slot) {
    if (slot < 0 || slot > kSlotFeet) return 0;
    if (a.texture != kNoTexture) return slot == kSlotHead ? a.helm : a.texture;
    auto s = static_cast<std::size_t>(slot);
    if (slot == kSlotHead) {
        if (!a.show_helm) return 0;
        // SwapHead: material 7 is drawn as 2 on old (non new-style) models
        // (3 for Iksar, which this function cannot tell).
        return a.material[0] == 7 ? 2 : a.material[0];
    }
    if ((slot == kSlotArms || slot == kSlotWrists) && is_robe(a.material[kSlotChest])) return a.material[kSlotChest];
    return a.material[s];
}

int tint_slot(const Appearance& a, int slot) {
    if ((slot == kSlotArms || slot == kSlotWrists) && is_robe(a.material[kSlotChest])) return kSlotChest;
    return slot;
}

std::uint32_t map_head_material(std::uint32_t race, std::uint8_t gender, std::uint32_t id) {
    const bool female = gender != 0;
    if (id == 0xf0) {
        switch (race) {
        case 1: return female ? 0x26c : 0x273;
        case 2: break; // the Barbarian helm below
        case 3: return female ? 0x23a : 0x23f;
        case 4: return female ? 0x231 : 0x235;
        case 5: return female ? 0x258 : 0x25d;
        case 6: return female ? 0x21c : 0x221;
        case 7: return female ? 0x24e : 0x253;
        case 8: return female ? 0x226 : 0x22d;
        case 9: return female ? 0x28a : 0x28f;
        case 10: return female ? 0x280 : 0x285;
        case 11: return female ? 0x262 : 0x267;
        case 12: return female ? 0x244 : 0x249;
        case 128: return female ? 0x276 : 0x27b;
        default: return id;
        }
    } else if (id == 0xf1) {
        switch (race) {
        case 2: break;
        case 10: return female ? 0x281 : 0x286;
        case 1: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 11: case 12: case 128: return 3;
        default: return id;
        }
    } else {
        return id;
    }
    return female ? 0x212 : 0x219;
}

HeadModel head_model(std::uint32_t race, std::uint8_t gender, std::uint32_t material, std::uint32_t hero_forge) {
    if (race == 130) { // Vah Shir: helmets are items (IT<n>), else none ("0")
        std::uint32_t m = material;
        bool item;
        if (m > 0x297) {
            item = m >= 0x29a && m <= 0x29c;
        } else if (m >= 0x295) {
            item = true;
        } else if (m != 0 && (m <= 3 || m == 0xf0)) {
            if (m == 0xf0) m = 3;
            m += gender == 1 ? 0x294 : 0x299;
            item = true;
        } else {
            item = false;
        }
        return {0, item ? m : 0};
    }
    std::uint32_t v = material;
    bool mapped = false;
    if (v - 0xf0 <= 0xf) {
        v = map_head_material(race, gender, v);
        mapped = true;
    }
    if (v == 7) v = race == 128 ? 3 : 2;
    if (v >= 0xf0 && mapped) return {0, v}; // IT<v> on the head point, head HE00
    // A material above 0xf1 not mapped names the head by the record's +0x10.
    return {v > 0xf1 ? hero_forge : v, 0};
}

} // namespace nmsc::game
