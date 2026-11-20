// The item preview (game/item_preview.h): OP_ItemPreviewRequest's bytes, the reply, which items show and on whom,
// and the player's copy wearing the item (0x5769a0).
#include <doctest/doctest.h>

#include <cstring>

#include "game/appearance.h"
#include "game/item_preview.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/protocol/rof2/opcode_map.h"

using namespace hailstone;
using namespace hailstone::game;

TEST_CASE("item preview: a body item carries the complete material record without changing the player") {
    Appearance original;
    original.material[1] = 1;
    net::Item chest;
    chest.equip_slots = 1u << 17;
    chest.material = 3;
    chest.material_unknown1 = 9;
    chest.elite_material = 12;
    chest.heros_forge_model = 15;
    chest.material_unknown2 = 18;
    const auto preview = preview_appearance(original, chest, nullptr, nullptr);
    CHECK(preview.material[1] == 3);
    CHECK(preview.material_variation[1] == 9);
    CHECK(preview.elite_material[1] == 12);
    CHECK(preview.hero_forge[1] == 15);
    CHECK(preview.new_armor_type[1] == 18);
    CHECK(original.material[1] == 1);
    CHECK(original.elite_material[1] == 0);
}

namespace {

net::Item item(std::uint32_t slots, const char* id_file) {
    net::Item i;
    i.id = 1234;
    i.name = "Thing";
    i.equip_slots = slots;
    i.id_file = id_file;
    return i;
}

} // namespace

TEST_CASE("item preview: the request (Send_OP_ItemPreviewRequest) and the reply") {
    net::Item plain = item(0, "IT63");
    net::AppPacket p = net::encode_item_preview_request(plain.id, &plain);
    REQUIRE(p.data.size() == 80);
    std::uint32_t id = 0;
    std::memcpy(&id, p.data.data(), 4);
    CHECK(id == 1234);
    std::int32_t a1 = 0;
    std::memcpy(&a1, p.data.data() + 0x40, 4);
    CHECK(a1 == -1);                          // unknowna1's default
    CHECK(p.data[0x4d] == std::byte{0});      // no ornamentation
    net::Item orn = plain;
    orn.ornament_main = "IT123";
    orn.ornament_hero_model = 7;
    p = net::encode_item_preview_request(orn.id, &orn);
    CHECK(p.data[0x4d] == std::byte{1});
    CHECK(std::memcmp(p.data.data() + 4, "IT123", 5) == 0);
    std::uint32_t hero = 0;
    std::memcpy(&hero, p.data.data() + 0x44, 4);
    CHECK(hero == 7);

    net::ItemPreviewReply r{plain, 0};
    auto back = net::decode_item_preview(net::encode(r));
    REQUIRE(back);
    CHECK(back->item.id == 1234);
    CHECK(back->trailing == 0);
}

TEST_CASE("item preview: what shows, and on whom") {
    CHECK(worn_in_visible_slot(item(1u << 17, "IT63"))); // chest
    CHECK(worn_in_visible_slot(item(1u << 13, "IT5")));  // primary
    CHECK_FALSE(worn_in_visible_slot(item(1u << 15, "IT63"))); // a ring
    auto s = item_preview_show(item(1u << 15, "IT63"), 0);
    CHECK_FALSE(s.show);
    s = item_preview_show(item(0, "IT23948"), 0);
    CHECK(s.show);
    CHECK_FALSE(s.on_player);
    s = item_preview_show(item(1u << 17, "IT63"), 0);
    CHECK(s.show);
    CHECK(s.on_player);
    CHECK(item_preview_show(item(0, "IT64"), 1).on_player);
    CHECK(preview_material_slot(10) == 3);
    CHECK(preview_material_slot(9) == -1);
    CHECK(item_model_number("IT112") == 112);
    CHECK(item_model_number("xyz") == 0);
}

TEST_CASE("item preview: the player's copy wearing it (0x5769a0)") {
    Appearance me;
    me.material[kSlotPrimary] = 10;
    me.material[kSlotSecondary] = 20;
    net::Item sword = item(1u << 13 | 1u << 14, "IT5");
    net::Item primary = item(1u << 13, "IT10"), secondary = item(1u << 14, "IT20");
    Appearance a = preview_appearance(me, sword, &primary, &secondary);
    CHECK(a.material[kSlotPrimary] == 5); // the first visible slot: primary
    CHECK(a.material[kSlotSecondary] == 20);
    sword.item_type = 1; // two-handed: nothing in the other hand
    a = preview_appearance(me, sword, &primary, &secondary);
    CHECK(a.material[kSlotSecondary] == 0);
    net::Item shield = item(1u << 14, "IT200");
    a = preview_appearance(me, shield, &primary, &secondary);
    CHECK(a.material[kSlotSecondary] == 200);
    CHECK(a.material[kSlotPrimary] == 10);
    net::Item chest = item(1u << 17, "IT63");
    chest.material = 3;
    chest.color = 0xff112233;
    a = preview_appearance(me, chest, &primary, &secondary);
    CHECK(a.material[1] == 3);
    CHECK(a.tint[1] == 0xff112233);
    net::Item bow = item(1u << 11, "IT8");
    a = preview_appearance(me, bow, &primary, &secondary);
    CHECK(a.material[kSlotPrimary] == 0);
    CHECK(a.material[kSlotSecondary] == 8);
}
