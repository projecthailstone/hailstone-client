#include "game/item_identity.h"

#include <array>
#include <cstdint>
#include <string_view>

#include "core/sha256.h"
#include "net/messages/item.h"

namespace hailstone::game {
namespace {

void number(Sha256& hash, std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = std::byte((value >> (i * 8)) & 0xff);
    hash.update(bytes);
}

void string(Sha256& hash, std::string_view value) {
    number(hash, value.size());
    hash.update(value);
}

void slot(Sha256& hash, const net::ItemSlot& value) {
    number(hash, static_cast<std::uint16_t>(value.type));
    number(hash, static_cast<std::uint16_t>(value.slot));
    number(hash, static_cast<std::uint16_t>(value.sub));
    number(hash, static_cast<std::uint16_t>(value.aug));
}

void snapshot(Sha256& hash, const net::Item& item) {
    // Tag every field in a fixed order. Do not hash the C++ object representation:
    // its padding, pointers, and container capacities are not item identity.
    number(hash, item.id);
    string(hash, item.serial);
    number(hash, item.instance_id);
    number(hash, item.parcel_item_id);
    slot(hash, item.slot);
    number(hash, item.stack_count);
    number(hash, item.charges);
    number(hash, item.attuned);
    number(hash, item.item_class);
    number(hash, item.is_evolving);
    number(hash, static_cast<std::uint32_t>(item.evolving.evolve_level));
    number(hash, item.evolving.final_item_id);
    number(hash, item.evolving.activated);
    number(hash, item.ornament_icon);
    number(hash, item.ornament_hero_model);
    string(hash, item.ornament_main);
    string(hash, item.ornament_off);
    number(hash, item.contents.size());
    for (const auto& child : item.contents) {
        number(hash, child.index);
        snapshot(hash, child.item);
    }
}

} // namespace

std::string loot_item_identity(const net::Item& item) {
    Sha256 hash;
    hash.update("hailstone/loot-item-identity/v1");
    snapshot(hash, item);
    return to_hex(hash.finish());
}

} // namespace hailstone::game
