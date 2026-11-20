#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/byte_io.h"
#include "game/inventory.h"
#include "game/item_links.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/protocol/rof2/opcode_map.h"

using namespace hailstone;
using namespace hailstone::net;

namespace {

// ---- an item built byte by byte the way rof2.cpp SerializeItem writes it,
// with the fixed blocks as zeroed arrays and values poked at their
// rof2_structs.h offsets (independent of read_item / write_item).

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
}
void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
void append(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& block) {
    out.insert(out.end(), block.begin(), block.end());
}
void append_cstr(std::vector<std::uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}
void append_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

struct HandItem {
    std::uint32_t serial = 1234;
    std::uint32_t stack = 1;
    std::uint8_t slot_type = 0;
    std::uint16_t main = 23, sub = 0xffff;
    std::uint32_t id = 1001;
    std::string name = "Short Sword";
    std::string lore = "Short Sword";
    std::string idfile = "IT10";
    std::uint32_t icon = 592;
    std::uint8_t item_class = 0;
    std::uint8_t item_type = 0;
    std::uint32_t slots = (1u << 13) | (1u << 14);
    std::int32_t ac = 0, hp = 0;
    std::int8_t astr = 0;
    std::uint32_t classes = 0xffff;
    std::uint8_t delay = 0;
    std::uint32_t damage = 0;
    std::uint8_t bag_slots = 0, bag_size = 0;
    std::uint32_t max_stack = 0;
    std::int32_t heroic_str = 0;
    std::string click_name;
    std::int32_t click_effect = -1;
    std::vector<std::pair<std::uint32_t, std::vector<std::uint8_t>>> children;
};

std::vector<std::uint8_t> serialize(const HandItem& h) {
    std::vector<std::uint8_t> out;
    // ItemSerializationHeader, 77 bytes.
    std::vector<std::uint8_t> hdr(77, 0);
    char serial[17] = {};
    std::snprintf(serial, sizeof serial, "%016u", h.serial);
    std::memcpy(hdr.data(), serial, 16);
    put32(hdr, 17, h.stack);
    hdr[25] = h.slot_type;
    put16(hdr, 26, h.main);
    put16(hdr, 28, h.sub);
    put16(hdr, 30, 0xffff);
    put32(hdr, 36, 1);        // merchant_slot
    put32(hdr, 44, h.serial); // instance_id
    put32(hdr, 56, 1);        // charges
    append(out, hdr);
    out.push_back(0); // no main-hand ornament
    out.push_back(0); // no off-hand ornament
    // ItemSerializationHeaderFinish, 26 bytes.
    std::vector<std::uint8_t> fin(26, 0);
    put32(fin, 4, 0xffffffff);
    put32(fin, 17, 0xffffffff);
    fin[25] = h.item_class;
    append(out, fin);
    append_cstr(out, h.name);
    append_cstr(out, h.lore);
    append_cstr(out, h.idfile);
    out.push_back(0);
    // ItemBodyStruct, 255 bytes.
    std::vector<std::uint8_t> body(255, 0);
    put32(body, 0, h.id);
    put32(body, 4, 30);   // weight 3.0
    body[8] = 1;          // norent
    body[9] = 255;        // nodrop: tradeable
    body[11] = 2;         // size
    put32(body, 12, h.slots);
    put32(body, 16, 100); // price
    put32(body, 20, h.icon);
    body[24] = 1;
    body[25] = 1;
    body[37] = static_cast<std::uint8_t>(h.astr);
    put32(body, 44, static_cast<std::uint32_t>(h.hp));
    put32(body, 56, static_cast<std::uint32_t>(h.ac));
    put32(body, 72, h.classes);
    put32(body, 76, 65535); // Races (NMS forces it)
    body[142] = h.delay;
    put32(body, 146, h.damage);
    put32(body, 150, 0xff123456); // color
    body[158] = h.item_type;
    put32(body, 159, 7); // material
    append(out, body);
    out.push_back(0); // charm file
    // ItemSecondaryBodyStruct, 74 bytes.
    std::vector<std::uint8_t> sec(74, 0);
    put32(sec, 4, 0xffffffff); // augrestrict2
    sec[69] = h.bag_slots;
    sec[70] = h.bag_size;
    append(out, sec);
    out.push_back(0); // book filename
    // ItemTertiaryBodyStruct, 76 bytes.
    std::vector<std::uint8_t> ter(76, 0);
    put32(ter, 35, 0xffffffff); // unknown3
    put32(ter, 50, h.max_stack);
    append(out, ter);
    // Six effect blocks: 30 bytes, name, u32.
    for (int e = 0; e < 6; ++e) {
        std::vector<std::uint8_t> eff(30, 0);
        if (e == 0) {
            put32(eff, 0, static_cast<std::uint32_t>(h.click_effect));
            put32(eff, 10, 3); // max charges
        }
        append(out, eff);
        append_cstr(out, e == 0 ? h.click_name : std::string());
        append_u32(out, 0);
    }
    // ItemQuaternaryBodyStruct, 171 bytes.
    std::vector<std::uint8_t> q(171, 0);
    put32(q, 22, static_cast<std::uint32_t>(h.heroic_str));
    put32(q, 124, 0xffffffff); // unknown28
    put32(q, 130, 0xffffffff); // unknown30
    q[170] = 1;                // unknown39
    append(out, q);
    append_u32(out, static_cast<std::uint32_t>(h.children.size()));
    for (const auto& [index, bytes] : h.children) {
        append_u32(out, index);
        append(out, bytes);
    }
    return out;
}

AppPacket raw_packet(rof2::Op op, const std::vector<std::uint8_t>& bytes) {
    AppPacket p(rof2::to_wire(op));
    for (auto b : bytes) p.data.push_back(static_cast<std::byte>(b));
    return p;
}

std::vector<std::uint8_t> bytes_of(const AppPacket& p) {
    std::vector<std::uint8_t> v;
    for (auto b : p.data) v.push_back(std::to_integer<std::uint8_t>(b));
    return v;
}

HandItem sword() {
    HandItem h;
    h.ac = 0;
    h.delay = 28;
    h.damage = 7;
    h.astr = 2;
    h.hp = 5;
    h.heroic_str = 3;
    h.click_effect = 2000;
    h.click_name = "Minor Healing";
    return h;
}

HandItem backpack() {
    HandItem h;
    h.serial = 77;
    h.main = 24;
    h.id = 17005;
    h.name = h.lore = "Backpack";
    h.idfile = "IT64";
    h.item_class = 1;
    h.item_type = 11;
    h.slots = 0;
    h.bag_slots = 8;
    h.bag_size = 3;
    // SerializeItem gives contents the bag's first sub-slot (server slot
    // 4010 + (24 - 23) * 200 -> {0, 24, 0}); the index before it is the real one.
    HandItem ration;
    ration.serial = 78;
    ration.main = 24;
    ration.sub = 0;
    ration.id = 13078;
    ration.name = ration.lore = "Summoned: Black Bread";
    ration.item_type = 14;
    ration.stack = 5;
    ration.max_stack = 20;
    ration.slots = 0;
    h.children.push_back({3, serialize(ration)});
    return h;
}

} // namespace

TEST_CASE("item serialization: a hand-built SerializeItem buffer decodes and re-encodes byte for byte") {
    std::vector<std::uint8_t> bytes;
    append_u32(bytes, 2);
    append(bytes, serialize(sword()));
    append(bytes, serialize(backpack()));
    AppPacket p = raw_packet(rof2::Op::CharInventory, bytes);
    auto items = decode_char_inventory(p);
    REQUIRE(items);
    REQUIRE(items->size() == 2);
    const Item& s = (*items)[0];
    CHECK(s.serial == "0000000000001234");
    CHECK(s.instance_id == 1234);
    CHECK(s.slot.type == 0);
    CHECK(s.slot.slot == 23);
    CHECK(s.slot.sub == -1);
    CHECK(s.id == 1001);
    CHECK(s.name == "Short Sword");
    CHECK(s.id_file == "IT10");
    CHECK(s.icon == 592);
    CHECK(s.weight == 30);
    CHECK(s.size == 2);
    CHECK(s.no_drop == 255);
    CHECK_FALSE(s.no_trade());
    CHECK(s.equip_slots == ((1u << 13) | (1u << 14)));
    CHECK(s.astr == 2);
    CHECK(s.hp == 5);
    CHECK(s.races == 65535);
    CHECK(s.delay == 28);
    CHECK(s.damage == 7);
    CHECK(s.color == 0xff123456);
    CHECK(s.material == 7);
    CHECK(s.heroic_str == 3);
    CHECK(s.effects[kEffectClick].effect == 2000);
    CHECK(s.effects[kEffectClick].max_charges() == 3);
    CHECK(s.effects[kEffectClick].name == "Minor Healing");
    CHECK(s.unknown28 == 0xffffffff);
    CHECK(s.unknown39 == 1);
    CHECK(s.contents.empty());

    const Item& bag = (*items)[1];
    CHECK(bag.is_bag());
    CHECK(bag.bag_slots == 8);
    REQUIRE(bag.contents.size() == 1);
    CHECK(bag.contents[0].index == 3);
    const Item* bread = bag.content(3);
    REQUIRE(bread);
    CHECK(bread->name == "Summoned: Black Bread");
    CHECK(bread->stack_count == 5);
    CHECK(bread->max_stack == 20);
    CHECK(bread->stackable());

    // Re-encoding gives the same bytes.
    CHECK(bytes_of(encode_char_inventory(*items)) == bytes);

    // Truncated or trailing data is refused.
    auto shorter = bytes;
    shorter.pop_back();
    CHECK_FALSE(decode_char_inventory(raw_packet(rof2::Op::CharInventory, shorter)));
    auto longer = bytes;
    longer.push_back(0);
    CHECK_FALSE(decode_char_inventory(raw_packet(rof2::Op::CharInventory, longer)));
    // An empty inventory is 4 zero bytes.
    auto empty = decode_char_inventory(raw_packet(rof2::Op::CharInventory, {0, 0, 0, 0}));
    REQUIRE(empty);
    CHECK(empty->empty());
}

TEST_CASE("item serialization: evolving block and ornament names") {
    HandItem h = sword();
    auto bytes = serialize(h);
    // Turn on isEvolving (header +76) and insert the 25-byte block plus ornament names.
    bytes[76] = 1;
    std::vector<std::uint8_t> evo(25, 0);
    put32(evo, 0, 5555);
    evo[16] = 1;
    bytes.insert(bytes.begin() + 77, evo.begin(), evo.end());
    // Replace the two empty ornament strings with "IT99".
    std::size_t at = 77 + 25;
    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + 2));
    std::vector<std::uint8_t> orn = {'I', 'T', '9', '9', 0, 'I', 'T', '9', '9', 0};
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), orn.begin(), orn.end());
    std::vector<std::uint8_t> pkt;
    append_u32(pkt, kItemPacketTrade);
    append(pkt, bytes);
    auto ip = decode_item_packet(raw_packet(rof2::Op::ItemPacket, pkt));
    REQUIRE(ip);
    CHECK(ip->type == kItemPacketTrade);
    CHECK(ip->item.is_evolving == 1);
    CHECK(ip->item.evolving.final_item_id == 5555);
    CHECK(ip->item.evolving.activated == 1);
    CHECK(ip->item.ornament_main == "IT99");
    CHECK(ip->item.ornament_off == "IT99");
    CHECK(ip->item.name == "Short Sword");
    CHECK(bytes_of(encode_item_packet(false, *ip)) == pkt);
    CHECK(encode_item_packet(true, *ip).opcode == rof2::to_wire(rof2::Op::ItemLinkResponse));
}

TEST_CASE("OP_MoveItem, OP_DeleteItem and the destroy slot") {
    MoveItem m;
    m.from = ItemSlot::possessions(kSlotCursor);
    m.to = ItemSlot::possessions(25, 4);
    m.count = 0;
    AppPacket p = encode(m);
    CHECK(p.opcode == rof2::to_wire(rof2::Op::MoveItem));
    std::vector<std::uint8_t> want = {0, 0, 0, 0, 33, 0, 0xff, 0xff, 0xff, 0xff, 0, 0,
                                      0, 0, 0, 0, 25, 0, 4, 0, 0xff, 0xff, 0, 0, 0, 0, 0, 0};
    CHECK(bytes_of(p) == want);
    auto back = decode_move_item(p);
    REQUIRE(back);
    CHECK(back->from.same(m.from));
    CHECK(back->to.same(m.to));

    // DestroyHeldItemOrMoney: to = Location -1 (int32), slots -1.
    m.to = ItemSlot::invalid();
    auto d = bytes_of(encode(m));
    CHECK(std::vector<std::uint8_t>(d.begin() + 12, d.begin() + 22) ==
          std::vector<std::uint8_t>{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    CHECK_FALSE(ItemSlot::invalid().valid());

    // OP_DeleteItem from the server's resync: {slot, invalid, 0xffffffff}.
    MoveItem del;
    del.from = ItemSlot::possessions(23);
    del.to = ItemSlot::invalid();
    del.count = 0xffffffff;
    AppPacket dp = encode_delete_item(del);
    CHECK(dp.opcode == rof2::to_wire(rof2::Op::DeleteItem));
    auto dd = decode_move_item(dp);
    REQUIRE(dd);
    CHECK(static_cast<std::int32_t>(dd->count) == -1);
    CHECK_FALSE(decode_move_item(raw_packet(rof2::Op::MoveItem, std::vector<std::uint8_t>(27, 0))));
}

TEST_CASE("loot packets") {
    CHECK(bytes_of(encode_loot_request(0x1234)) == std::vector<std::uint8_t>{0x34, 0x12, 0, 0});
    CHECK(encode_loot_request(1).opcode == rof2::to_wire(rof2::Op::LootRequest));
    CHECK(encode_end_loot_request(1).opcode == rof2::to_wire(rof2::Op::EndLootRequest));
    LootItem l;
    l.lootee = 300;
    l.looter = 12;
    l.slot = 23;
    l.auto_loot = 1;
    auto b = bytes_of(encode(l));
    CHECK(b == std::vector<std::uint8_t>{0x2c, 1, 0, 0, 12, 0, 0, 0, 23, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0});
    auto back = decode_loot_item(raw_packet(rof2::Op::LootItem, b));
    REQUIRE(back);
    CHECK(back->slot == 23);
    CHECK(back->auto_loot == 1);

    // moneyOnCorpseStruct as Corpse::MakeLootRequestPackets fills it.
    std::vector<std::uint8_t> money = {1, 0x42, 0xef, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    auto m = decode_money_on_corpse(raw_packet(rof2::Op::MoneyOnCorpse, money));
    REQUIRE(m);
    CHECK(m->response == 1);
    CHECK(m->platinum == 1);
    CHECK(m->gold == 2);
    CHECK(m->silver == 3);
    CHECK(m->copper == 4);
    CHECK(bytes_of(encode(*m)) == money);

    auto c = decode_money_update(raw_packet(rof2::Op::MoneyUpdate, {5, 0, 0, 0, 6, 0, 0, 0, 7, 0, 0, 0, 8, 0, 0, 0}));
    REQUIRE(c);
    CHECK(c->platinum == 5);
    CHECK(c->copper == 8);
}

TEST_CASE("item links: NMS wide and stock bodies, and OP_ItemLinkClick") {
    // EQ::saylink::GenerateLinkBody (NMS): "%1X%08X x7 %1X%04X%02X%05X%08X".
    std::string wide = std::string("\x12") + "0" + "000F4240" + "00000001" + "00000000" + "00000000" + "00000000" +
                       "00000000" + "00000000" + "0" + "0000" + "00" + "001F4" + "DEADBEEF" + "Legendary Sword" + "\x12";
    std::string line = "You have looted a " + wide + ".";
    auto segs = game::split_links(line);
    REQUIRE(segs.size() == 3);
    CHECK(segs[0].text == "You have looted a ");
    REQUIRE(segs[1].link);
    const auto& l = *segs[1].link;
    CHECK(l.wide);
    CHECK(l.item_id == 1000000);
    CHECK(l.augments[0] == 1);
    CHECK(l.ornament_icon == 0x1f4);
    CHECK(l.hash == 0xdeadbeef);
    CHECK(l.text == "Legendary Sword");
    CHECK(segs[2].text == ".");
    CHECK(game::strip_links(line) == "You have looted a Legendary Sword.");

    // Stock RoF2: 5-digit ids.
    std::string stock = std::string("\x12") + "0" + "03E9A" + "00000" + "00000" + "00000" + "00000" + "00000" +
                        "00000" + "0" + "0000" + "00" + "00000" + "0000ABCD" + "Bronze Dagger" + "\x12";
    auto s2 = game::split_links("look: " + stock);
    REQUIRE(s2.size() == 2);
    REQUIRE(s2[1].link);
    CHECK_FALSE(s2[1].link->wide);
    CHECK(s2[1].link->item_id == 0x3e9a);
    CHECK(s2[1].link->hash == 0xabcd);
    CHECK(s2[1].link->text == "Bronze Dagger");

    // make_item_link round trip, both widths.
    for (bool w : {true, false}) {
        auto made = game::split_links(game::make_item_link(13078, "Black Bread", w));
        REQUIRE(made.size() == 1);
        REQUIRE(made[0].link);
        CHECK(made[0].link->item_id == 13078);
        CHECK(made[0].link->wide == w);
        CHECK(made[0].text == "Black Bread");
    }
    // Unterminated tags and other tag codes stay text.
    CHECK(game::strip_links("broken \x12" "0abc") == "broken \x12" "0abc");
    CHECK(game::strip_links("\x12" "1Somebody\x12 waves") == "Somebody waves");

    // ItemViewRequest_Struct: 52 bytes, the evolving fields and icon at +0x24..+0x30.
    AppPacket click = encode(l.click_request());
    CHECK(click.opcode == rof2::to_wire(rof2::Op::ItemLinkClick));
    REQUIRE(click.data.size() == kItemLinkClickSize);
    auto cb = bytes_of(click);
    CHECK(cb[0] == 0x40);
    CHECK(cb[1] == 0x42);
    CHECK(cb[2] == 0x0f);
    CHECK(cb[4] == 1);      // augment 1
    CHECK(cb[28] == 0xef);  // hash
    CHECK(cb[32] == 4);     // flags
    CHECK(cb[48] == 0xf4);  // ornament icon
    CHECK(cb[49] == 0x01);
    auto cl = decode_item_link_click(click);
    REQUIRE(cl);
    CHECK(cl->item_id == 1000000);
}

TEST_CASE("inventory: load, bags, swaps and the item rules") {
    std::vector<std::uint8_t> bytes;
    append_u32(bytes, 2);
    append(bytes, serialize(sword()));
    append(bytes, serialize(backpack()));
    auto items = decode_char_inventory(raw_packet(rof2::Op::CharInventory, bytes));
    REQUIRE(items);
    game::Inventory inv;
    inv.load(std::move(*items));
    const ItemSlot cursor = ItemSlot::possessions(kSlotCursor);
    REQUIRE(inv.at(ItemSlot::possessions(23)));
    REQUIRE(inv.at(ItemSlot::possessions(24, 3)));
    CHECK(inv.at(ItemSlot::possessions(24, 3))->name == "Summoned: Black Bread");
    CHECK_FALSE(inv.at(ItemSlot::possessions(24, 2)));

    // Pick up the sword, put it into the primary slot.
    CHECK(inv.swap(ItemSlot::possessions(23), cursor));
    CHECK(inv.cursor());
    CHECK_FALSE(inv.at(ItemSlot::possessions(23)));
    CHECK(inv.swap(cursor, ItemSlot::possessions(kSlotPrimary)));
    CHECK(inv.at(ItemSlot::possessions(kSlotPrimary))->slot.slot == kSlotPrimary);
    CHECK_FALSE(inv.cursor());
    // Bread out of the bag to the cursor and into another bag slot.
    CHECK(inv.swap(ItemSlot::possessions(24, 3), cursor));
    CHECK(inv.cursor()->name == "Summoned: Black Bread");
    CHECK(inv.swap(cursor, ItemSlot::possessions(24, 7)));
    CHECK(inv.at(ItemSlot::possessions(24, 7)));
    // No bag in slot 25: a sub-slot there cannot hold anything.
    CHECK_FALSE(inv.swap(ItemSlot::possessions(24, 7), ItemSlot::possessions(25, 0)));
    // A bag cannot go into itself.
    CHECK_FALSE(inv.swap(ItemSlot::possessions(24), ItemSlot::possessions(24, 1)));

    const Item& sw = *inv.at(ItemSlot::possessions(kSlotPrimary));
    const Item& bag = *inv.at(ItemSlot::possessions(24));
    // PcZoneClient_CanItemGoInEquipSlot.
    CHECK(game::can_go_in_equip_slot(sw, kSlotPrimary, nullptr, nullptr, false));
    CHECK_FALSE(game::can_go_in_equip_slot(sw, kSlotHead, nullptr, nullptr, false));
    CHECK_FALSE(game::can_go_in_equip_slot(sw, kSlotSecondary, nullptr, nullptr, false)); // no dual wield
    CHECK(game::can_go_in_equip_slot(sw, kSlotSecondary, nullptr, nullptr, true));
    CHECK(game::can_go_in_equip_slot(bag, 30, nullptr, nullptr, false));    // general: anything
    CHECK_FALSE(game::can_go_in_equip_slot(bag, kSlotChest, nullptr, nullptr, false)); // bags never worn
    Item two_hander = sw;
    two_hander.item_type = 1;
    CHECK_FALSE(game::can_go_in_equip_slot(two_hander, kSlotPrimary, nullptr, &sw, true));
    CHECK_FALSE(game::can_go_in_equip_slot(sw, kSlotSecondary, &two_hander, nullptr, true));
    Item arrow = sw;
    arrow.item_type = 27;
    arrow.equip_slots = 0;
    CHECK(game::can_go_in_equip_slot(arrow, kSlotAmmo, nullptr, nullptr, false));
    CHECK_FALSE(game::can_go_in_equip_slot(arrow, kSlotRange, nullptr, nullptr, false));
    // Classes (CanUseItem class only): warrior bit 0.
    Item caster_only = sw;
    caster_only.classes = 1u << 11; // wizard
    CHECK_FALSE(game::can_use_class(caster_only, 1u << 0));
    CHECK(game::can_use_class(caster_only, (1u << 0) | (1u << 11))); // NMS multiclass
    CHECK(game::can_use_class(bag, 0));
    // Bag fit.
    CHECK(game::bag_fit(bag, sw) == 1);
    Item big = sw;
    big.size = 4;
    CHECK(game::bag_fit(bag, big) == 5);
    CHECK(game::bag_fit_message(5) == 12934);
    CHECK(game::bag_fit(bag, bag) == 3); // a bag with contents
    CHECK(game::bag_fit(sw, sw) == 0);
    // Stacks.
    const Item& bread = *inv.at(ItemSlot::possessions(24, 7));
    Item bread2 = bread;
    CHECK(game::can_stack_with(bread, bread2));
    bread2.attuned = 1;
    CHECK_FALSE(game::can_stack_with(bread, bread2));
    CHECK_FALSE(game::can_stack_with(sw, sw)); // not stackable
    // Visible slots and the race bits.
    CHECK(game::material_slot(kSlotChest) == 1);
    CHECK(game::material_slot(kSlotSecondary) == 8);
    CHECK(game::material_slot(kSlotWrist2) == -1);
    CHECK(game::item_race_bit(128) == 13);
    CHECK(game::item_race_bit(522) == 16);
    CHECK(game::item_race_bit(75) == 17);
    CHECK(game::coin_text(1, 0, 3, 0) == "1 platinum, 3 silver");
    CHECK(std::string(possessions_slot_name(kSlotPrimary)) == "Primary");
}

#include "data/nms_char_inventory.inc"

TEST_CASE("item serialization: a live OP_CharInventory from the NMS server") {
    std::vector<std::uint8_t> bytes(std::begin(kNmsCharInventory), std::end(kNmsCharInventory));
    auto items = decode_char_inventory(raw_packet(rof2::Op::CharInventory, bytes));
    REQUIRE(items);
    REQUIRE(items->size() == 4);
    const Item& sword = (*items)[0];
    CHECK(sword.name == "Short Sword");
    CHECK(sword.id == 9998);
    CHECK(sword.icon == 580);
    CHECK(sword.slot.type == kInvPossessions);
    CHECK(sword.slot.slot == kSlotPrimary);
    CHECK(sword.races == 65535); // NMS forces Races 65535 and Deity 0
    CHECK(sword.deity == 0);
    CHECK(sword.damage > 0);
    CHECK(sword.delay > 0);
    const Item& milk = (*items)[1];
    CHECK(milk.name == "Skin of Milk");
    CHECK(milk.slot.slot == kSlotGeneral1);
    CHECK(milk.stack_count == 5);
    CHECK(milk.stackable());
    CHECK((*items)[2].name == "Bread Cakes");
    CHECK((*items)[3].name == "Recruitment Flyer");
    CHECK((*items)[3].slot.slot == 25);
    for (const auto& it : *items) CHECK(it.unknown39 == 1);
    // Every field is kept: re-encoding gives the server's bytes back.
    CHECK(bytes_of(encode_char_inventory(*items)) == bytes);
}

#include "ui/item_windows.h"

TEST_CASE("item icons: A_DragItem sheet and cell, and the item info lines") {
    // Icon 500 is the first cell of dragitem1.dds; the grid runs down each column.
    auto c = ui::icon_cell(500);
    REQUIRE(c);
    CHECK(c->sheet == 1);
    CHECK(c->x == 0);
    CHECK(c->y == 0);
    c = ui::icon_cell(501);
    CHECK(c->x == 0);
    CHECK(c->y == 40);
    c = ui::icon_cell(506);
    CHECK(c->x == 40);
    CHECK(c->y == 0);
    c = ui::icon_cell(535);
    CHECK(c->sheet == 1);
    CHECK(c->x == 200);
    CHECK(c->y == 200);
    c = ui::icon_cell(580); // Short Sword: index 80 -> sheet 3, cell 8
    CHECK(c->sheet == 3);
    CHECK(c->x == 40);
    CHECK(c->y == 80);
    CHECK_FALSE(ui::icon_cell(499));

    Item sword;
    sword.name = "Short Sword";
    sword.classes = 1;
    sword.races = 65535;
    sword.size = 2;
    sword.weight = 50;
    sword.damage = 4;
    sword.delay = 29;
    sword.astr = 2;
    sword.heroic_str = 1;
    sword.no_drop = 0;
    sword.no_rent = 1; // 0 would add "Temporary"
    sword.value = 68;
    sword.id = 9998;
    auto lines = ui::item_info_lines(sword);
    auto has = [&](const std::string& want) {
        for (const auto& l : lines)
            if (l == want) return true;
        return false;
    };
    CHECK(has("No Trade"));
    CHECK(has("Class: WAR"));
    CHECK(has("Race: ALL"));
    CHECK(has("Size: MEDIUM    Weight: 5.0"));
    CHECK(has("Skill: 1H Slashing"));
    CHECK(has("Base Dmg: 4    Delay: 29"));
    CHECK(has("Strength: +2 +1"));
    CHECK(has("Value: 6 silver, 8 copper"));
    CHECK(has("Item ID: 9998"));
}

TEST_CASE("opcode 0x3ed4: the split coin ProcessCursorQueue sends back") {
    hailstone::net::AppPacket p = hailstone::net::encode_split_coin({1, 2, 3, 4});
    CHECK(p.opcode == 0x3ed4);
    REQUIRE(p.data.size() == 16);
    CHECK(std::to_integer<int>(p.data[0]) == 1);
    CHECK(std::to_integer<int>(p.data[4]) == 2);
    CHECK(std::to_integer<int>(p.data[8]) == 3);
    CHECK(std::to_integer<int>(p.data[12]) == 4);
}

TEST_CASE("item links: the loot window's Link All into the chat input (CLootWnd_LinkItemToChat)") {
    using hailstone::game::link_all_into_chat;
    using hailstone::game::make_item_link;
    std::string a = make_item_link(1001, "Cloth Cap", true), b = make_item_link(1002, "Rusty Cap", true); // as long as a
    // ", " between links, after what was typed.
    auto r = link_all_into_chat("/g ", {a, b});
    CHECK(r.sent.empty());
    CHECK(r.text == "/g " + a + ", " + b);
    // Ten links at most: the eleventh sends the line and starts again from the typed text.
    std::vector<std::string> many(11, a);
    r = link_all_into_chat("/g ", many, 4096);
    REQUIRE(r.sent.size() == 1);
    std::string ten = "/g ";
    for (int i = 0; i < 10; ++i) ten += (i ? ", " : "") + a;
    CHECK(r.sent[0] == ten + ", ");
    CHECK(r.text == "/g " + a);
    // MaxChars: the separator that does not fit sends the line (without it), then the link goes on the typed text.
    std::size_t limit = 3 + a.size() + 1; // room for one link and one character
    r = link_all_into_chat("/g ", {a, b}, limit);
    REQUIRE(r.sent.size() == 1);
    CHECK(r.sent[0] == "/g " + a);
    CHECK(r.text == "/g " + b);
    // A link that never fits, with nothing added before it, is left out.
    r = link_all_into_chat("/g ", {a}, 10);
    CHECK(r.sent.empty());
    CHECK(r.text == "/g ");
}

TEST_CASE("bags: contents up to index 999 decode and are held; 1000 and above are refused") {
    // A bag sending a content at index 999 (past EQEmu's 200) decodes.
    HandItem bag = backpack();
    bag.children.clear();
    HandItem bread;
    bread.name = bread.lore = "Summoned: Black Bread";
    bread.slots = 0;
    bag.children.push_back({999, serialize(bread)});
    std::vector<std::uint8_t> bytes;
    append_u32(bytes, 1);
    append(bytes, serialize(bag));
    auto items = decode_char_inventory(raw_packet(rof2::Op::CharInventory, bytes));
    REQUIRE(items);
    game::Inventory inv;
    inv.load(std::move(*items));
    CHECK(inv.at(ItemSlot::possessions(24, 999)));
    // The inventory takes and moves items in sub-slots below 1000 only.
    CHECK(inv.swap(ItemSlot::possessions(24, 999), ItemSlot::possessions(24, 500)));
    CHECK(inv.at(ItemSlot::possessions(24, 500)));
    CHECK_FALSE(inv.swap(ItemSlot::possessions(24, 500), ItemSlot::possessions(24, 1000)));
    Item extra;
    extra.name = "Extra";
    CHECK_FALSE(inv.put(ItemSlot::possessions(24, 1000), extra));

    // Index 1000 is out of range.
    HandItem over = backpack();
    over.children.clear();
    over.children.push_back({1000, serialize(bread)});
    std::vector<std::uint8_t> bad;
    append_u32(bad, 1);
    append(bad, serialize(over));
    CHECK_FALSE(decode_char_inventory(raw_packet(rof2::Op::CharInventory, bad)));
}

#include "fuzz_support.h"

TEST_CASE("item serialization: a count the bytes cannot hold reserves nothing large before it is refused") {
    using hailstone::net::Item;
    // An empty item takes more than the 64 bytes the decoder assumes as the least one can take.
    const auto one = hailstone::net::encode_char_inventory({Item{}});
    REQUIRE(one.data.size() - 4 >= 64);

    auto largest_while_decoding = [](const hailstone::net::AppPacket& p) {
        hailstone::fuzz::reset_largest_allocation();
        hailstone::fuzz::arm_allocation_guard(true);
        auto got = hailstone::net::decode_char_inventory(p);
        hailstone::fuzz::arm_allocation_guard(false);
        CHECK(!got);
        return hailstone::fuzz::largest_allocation();
    };
    // OP_CharInventory claiming 4096 items in a 4-byte packet: it used to reserve 4096 items (megabytes) first.
    std::vector<std::byte> claim = {std::byte{0x00}, std::byte{0x10}, std::byte{0x00}, std::byte{0x00}};
    CHECK(largest_while_decoding(hailstone::net::AppPacket(one.opcode, claim)) < 4096);
    // One item whose bag claims 1000 contents with nothing after the count: no 1000-entry reserve either.
    auto bag = one.data;
    const std::uint32_t contents = 1000;
    for (std::size_t i = 0; i < 4; ++i) bag[bag.size() - 4 + i] = static_cast<std::byte>(contents >> (8 * i));
    CHECK(largest_while_decoding(hailstone::net::AppPacket(one.opcode, bag)) < 1000 * sizeof(hailstone::net::BagItem) / 4);
}

TEST_CASE("item links: the item display's icon links only without a BenefitFlag (ItemBase_GetUnknown0x100Max)") {
    net::Item it;
    it.id = 1001;
    CHECK(game::benefit_flag_max(it) == 0);
    CHECK(game::display_icon_links(it));
    it.benefit_flag = 4;
    CHECK(game::benefit_flag_max(it) == 4);
    CHECK_FALSE(game::display_icon_links(it));
    // The highest of the item and its augments, each read the same way.
    it.benefit_flag = 2;
    net::BagItem aug;
    aug.index = 1;
    aug.item.benefit_flag = 7;
    it.contents.push_back(aug);
    CHECK(game::benefit_flag_max(it) == 7);
    it.benefit_flag = 0;
    it.contents[0].item.item_class = 1; // a content whose Type is not 0 counts 0
    CHECK(game::benefit_flag_max(it) == 0);
    // Above 0 only: the field is read as a signed int.
    it.contents.clear();
    it.benefit_flag = 0xffffffffu;
    CHECK(game::benefit_flag_max(it) == 0);
    // A container (Type 1) or a book (2) is 0 whatever it and its contents hold.
    it.benefit_flag = 5;
    it.item_class = 1;
    aug.item.item_class = 0;
    it.contents.push_back(aug);
    CHECK(game::benefit_flag_max(it) == 0);
    it.item_class = 2;
    CHECK(game::display_icon_links(it));
}
