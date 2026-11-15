#include "game/inventory.h"

#include "game/item_moves.h"

#include <string>

namespace hailstone::game {

using net::Item;
using net::ItemSlot;

namespace {

Inventory::Key key_of(const ItemSlot& s) { return {s.type, s.slot}; }

// Rewrites the header slots of an item and its contents after a move.
void relocate(Item& item, const ItemSlot& slot) {
    item.slot = slot;
    item.slot.aug = -1;
    for (auto& b : item.contents) {
        ItemSlot inner = slot;
        inner.sub = static_cast<std::int16_t>(b.index);
        b.item.slot = inner;
        b.item.slot.aug = -1;
    }
}

} // namespace

void Inventory::touched(const ItemSlot& slot) {
    if (slot.type == net::kInvPossessions && slot.slot == net::kSlotCursor) ++cursor_placement_;
}

void Inventory::clear() {
    items_.clear();
    ++revision_;
    ++cursor_placement_;
}

void Inventory::load(std::vector<Item> items) {
    items_.clear();
    ++cursor_placement_;
    for (auto& it : items) {
        ItemSlot s = it.slot;
        s.aug = -1;
        put(s, std::move(it));
    }
    ++revision_;
}

const Item* Inventory::at(const ItemSlot& slot) const {
    auto it = items_.find(key_of(slot));
    if (it == items_.end()) return nullptr;
    if (slot.sub < 0) return &it->second;
    return it->second.content(static_cast<std::uint32_t>(slot.sub));
}

Item* Inventory::at(const ItemSlot& slot) {
    return const_cast<Item*>(static_cast<const Inventory&>(*this).at(slot));
}

bool Inventory::put(const ItemSlot& slot, Item item) {
    if (slot.type < 0 || slot.slot < 0) return false;
    if (slot.sub < 0) {
        relocate(item, slot);
        items_[key_of(slot)] = std::move(item);
        ++revision_;
        touched(slot);
        return true;
    }
    auto parent = items_.find(key_of(slot));
    if (parent == items_.end() || !parent->second.is_bag() ||
        static_cast<std::uint32_t>(slot.sub) >= net::kMaxBagContents)
        return false;
    auto index = static_cast<std::uint32_t>(slot.sub);
    item.slot = slot;
    item.slot.aug = -1;
    if (Item* existing = parent->second.content(index)) {
        *existing = std::move(item);
    } else {
        parent->second.contents.push_back({index, std::move(item)});
    }
    ++revision_;
    touched(slot);
    return true;
}

std::optional<Item> Inventory::take(const ItemSlot& slot) {
    auto parent = items_.find(key_of(slot));
    if (parent == items_.end()) return std::nullopt;
    if (slot.sub < 0) {
        Item item = std::move(parent->second);
        items_.erase(parent);
        ++revision_;
        touched(slot);
        return item;
    }
    auto& contents = parent->second.contents;
    for (auto it = contents.begin(); it != contents.end(); ++it) {
        if (it->index == static_cast<std::uint32_t>(slot.sub)) {
            Item item = std::move(it->item);
            contents.erase(it);
            ++revision_;
            touched(slot);
            return item;
        }
    }
    return std::nullopt;
}

bool Inventory::swap(const ItemSlot& a, const ItemSlot& b) {
    if (a.same(b)) return true;
    auto can_hold = [&](const ItemSlot& s) {
        if (s.type < 0 || s.slot < 0) return false;
        if (s.sub < 0) return true;
        const Item* bag = at(s.parent());
        return bag && bag->is_bag() && static_cast<std::uint32_t>(s.sub) < net::kMaxBagContents;
    };
    if (!can_hold(a) || !can_hold(b)) return false;
    // A bag cannot be put into itself.
    if ((a.sub >= 0 && b.sub < 0 && key_of(a) == key_of(b)) || (b.sub >= 0 && a.sub < 0 && key_of(a) == key_of(b)))
        return false;
    auto from_a = take(a);
    auto from_b = take(b);
    if (from_b) put(a, std::move(*from_b));
    if (from_a) put(b, std::move(*from_a));
    ++revision_;
    return true;
}

// ---------------------------------------------------------------- rules

int item_race_bit(std::uint32_t race) { return item_race_bit_of(static_cast<std::int32_t>(race)); }

bool can_use_class(const Item& item, std::uint32_t class_mask) {
    if (item.item_class != 0) return true; // bags and books (@0044c444)
    return (item.classes & class_mask) != 0;
}

namespace {
EquipItem equip_item(const Item& i) {
    EquipItem e;
    e.item_class = i.item_class;
    e.item_type = i.item_type;
    e.range = i.range;
    e.equip_slots = i.equip_slots;
    e.weapon = item_is_weapon(i.item_type, static_cast<std::int32_t>(i.damage));
    return e;
}
} // namespace

bool can_go_in_equip_slot(const Item& item, int slot, const Item* primary, const Item* secondary, bool dual_wield,
                          bool slot_available) {
    EquipItem it = equip_item(item);
    std::optional<EquipItem> p, s;
    if (primary) p = equip_item(*primary);
    if (secondary) s = equip_item(*secondary);
    return can_item_go_in_equip_slot(slot, &it, slot_available, p ? &*p : nullptr, s ? &*s : nullptr, dual_wield);
}

int bag_fit(const Item& bag, const Item& item) {
    if (!bag.is_bag()) return 0;
    if (bag.placeable_flags() != 0) return 2;
    if (item.is_bag() && !item.contents.empty()) return 3;
    if (item.size > bag.bag_size) return 5;
    switch (bag.bag_type) {
    case 2: if (item.item_type != 27 && item.item_type != 43) return 6; break;
    case 8: if (item.item_type != 19) return 7; break;
    case 58: if (!item.tradeskills) return 8; break;
    case 60: return 10;
    default: break;
    }
    return 1;
}

bool server_bag_accepts(const Item& bag, const Item& item) {
    if (!bag.is_bag() || item.size > bag.bag_size) return false;
    if (bag.bag_type == 2 && item.item_type != 27) return false;
    if (bag.bag_type == 8 && item.item_type != 19) return false;
    return true;
}

std::uint32_t bag_fit_message(int fit) {
    switch (fit) {
    case 2: return 5246;
    case 3: return 12933;
    case 4: return 5216;
    case 5: return 12934;
    case 6: return 12935;
    case 7: return 12936;
    case 8: return 7685;
    case 9: return 7691;
    case 10: return 4105;
    default: return 0;
    }
}

bool can_stack_with(const Item& a, const Item& b) {
    return a.max_stack > 1 && b.max_stack > 1 && a.id == b.id && a.attuned == b.attuned && a.copied == b.copied;
}

int material_slot(int possessions_slot) {
    switch (possessions_slot) {
    case net::kSlotHead: return 0;
    case net::kSlotChest: return 1;
    case net::kSlotArms: return 2;
    case net::kSlotWrist1: return 3;
    case net::kSlotHands: return 4;
    case net::kSlotLegs: return 5;
    case net::kSlotFeet: return 6;
    case net::kSlotPrimary: return 7;
    case net::kSlotSecondary: return 8;
    default: return -1;
    }
}

std::string coin_text(std::uint32_t platinum, std::uint32_t gold, std::uint32_t silver, std::uint32_t copper) {
    std::string s;
    auto add = [&](std::uint32_t n, const char* what) {
        if (!n) return;
        if (!s.empty()) s += ", ";
        s += std::to_string(n) + " " + what;
    };
    add(platinum, "platinum");
    add(gold, "gold");
    add(silver, "silver");
    add(copper, "copper");
    return s;
}

std::string coin_amounts_text(std::uint32_t platinum, std::uint32_t gold, std::uint32_t silver, std::uint32_t copper) {
    // The original tests the amounts as signed ints: positive to print, non-zero for the separators.
    auto p = static_cast<std::int32_t>(platinum), g = static_cast<std::int32_t>(gold);
    auto s = static_cast<std::int32_t>(silver), c = static_cast<std::int32_t>(copper);
    std::string out;
    if (p > 0) out = std::to_string(p) + " platinum";
    if (g > 0) {
        if (p != 0) out += (s != 0 || c != 0) ? ", " : " and ";
        out += std::to_string(g) + " gold";
    }
    if (s > 0) {
        if (p != 0 || g != 0) out += c != 0 ? ", " : " and ";
        out += std::to_string(s) + " silver";
    }
    if (c > 0) {
        if (p != 0 || s != 0 || g != 0) out += " and ";
        out += std::to_string(c) + " copper";
    }
    return out;
}

} // namespace hailstone::game
