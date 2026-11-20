// The bank as last seen (game/bank_snapshot.h): bank.dat's format, and GameConnection's copy against the mock server
// as EQEmu's Inventory:LazyLoadBank sends the bank (not in OP_CharInventory; item packets near a banker): listed in
// its place until the server sends the bank again, never in the inventory, and kept per character across sessions.
#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

#include "game/bank_snapshot.h"
#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "scratch_dir.h"

using namespace hailstone;
using namespace hailstone::test;
namespace fs = std::filesystem;

namespace {

net::Item banked(std::uint32_t id, const std::string& name, std::int16_t type, std::int16_t slot) {
    net::Item it = make_item(id, name, 0);
    it.slot = net::ItemSlot{type, slot, -1, -1, 0, 0};
    return it;
}

net::Item bank_bag(std::int16_t slot) {
    net::Item bag = banked(17005, "Large Sack", net::kInvBank, slot);
    bag.item_class = 1;
    bag.bag_slots = 8;
    net::Item inside = make_item(9990, "Fine Steel Ring", 0);
    inside.slot = net::ItemSlot{net::kInvBank, slot, 2, -1, 0, 0};
    bag.contents.push_back({2, inside});
    return bag;
}

std::span<const std::byte> bytes_of(const std::string& s) { return std::as_bytes(std::span(s.data(), s.size())); }

bool zone_again(Rig& rig) {
    const int entries = rig.server.zone_entries;
    rig.server.request_zone_change(1, 20.0f, 10.0f, 3.0f);
    return rig.until([&] { return rig.server.zone_entries == entries + 1 && rig.conn.state() == net::ConnectionState::InZone; }) &&
           rig.until([&] { return rig.conn.player_status().max_hp != 0; });
}

const net::Item* seen_at(const net::GameConnection& c, std::int16_t type, std::int16_t slot) {
    for (const auto& it : c.bank_seen().items)
        if (it.slot.type == type && it.slot.slot == slot) return &it;
    return nullptr;
}

} // namespace

TEST_CASE("bank snapshot: bank.dat keeps the bank's and shared bank's items with their bag contents") {
    game::Inventory inv;
    inv.put(net::ItemSlot::possessions(23), make_item(13005, "Ration", 23, 5));
    inv.put(net::ItemSlot{net::kInvBank, 3, -1, -1, 0, 0}, bank_bag(3));
    inv.put(net::ItemSlot{net::kInvSharedBank, 1, -1, -1, 0, 0}, banked(1001, "Cloth Cap", net::kInvSharedBank, 1));

    const game::BankSnapshot s = game::bank_snapshot_of(inv, 1'700'000'000);
    REQUIRE(s.items.size() == 2); // the carried Ration is not the bank's
    const std::string file = game::encode_bank_snapshot(s);
    auto back = game::decode_bank_snapshot(bytes_of(file));
    REQUIRE(back);
    CHECK(back->seen == 1'700'000'000);
    REQUIRE(back->items.size() == 2);
    game::Inventory again;
    again.load(back->items);
    const net::Item* bag = again.at(net::ItemSlot{net::kInvBank, 3, -1, -1, 0, 0});
    REQUIRE(bag);
    CHECK(bag->name == "Large Sack");
    const net::Item* ring = again.at(net::ItemSlot{net::kInvBank, 3, 2, -1, 0, 0});
    REQUIRE(ring);
    CHECK(ring->name == "Fine Steel Ring");
    CHECK(ring->serial == bank_bag(3).contents[0].item.serial);
    const net::Item* cap = again.at(net::ItemSlot{net::kInvSharedBank, 1, -1, -1, 0, 0});
    REQUIRE(cap);
    CHECK(cap->name == "Cloth Cap");
}

TEST_CASE("bank snapshot: a file that is not one, from a later version, cut short or with more after it is refused") {
    game::BankSnapshot s;
    s.seen = 5;
    s.items.push_back(banked(1001, "Cloth Cap", net::kInvBank, 0));
    const std::string good = game::encode_bank_snapshot(s);
    REQUIRE(game::decode_bank_snapshot(bytes_of(good)));

    CHECK_FALSE(game::decode_bank_snapshot(bytes_of(std::string())));
    std::string magic = good;
    magic[0] = 'X';
    CHECK_FALSE(game::decode_bank_snapshot(bytes_of(magic)));
    std::string version = good;
    version[4] = 2;
    CHECK_FALSE(game::decode_bank_snapshot(bytes_of(version)));
    CHECK_FALSE(game::decode_bank_snapshot(bytes_of(good.substr(0, good.size() - 3))));
    CHECK_FALSE(game::decode_bank_snapshot(bytes_of(good + "x")));

    // an item that is not in a bank slot (a hand-edited or damaged file) is dropped
    game::BankSnapshot odd;
    odd.items.push_back(make_item(13005, "Ration", 23, 5));
    odd.items.push_back(banked(1001, "Cloth Cap", net::kInvSharedBank, 0));
    auto back = game::decode_bank_snapshot(bytes_of(game::encode_bank_snapshot(odd)));
    REQUIRE(back);
    REQUIRE(back->items.size() == 1);
    CHECK(back->items[0].name == "Cloth Cap");
}

TEST_CASE("bank snapshot: with the bank left out of the zone-in, the bank as last seen is listed until a banker sends it") {
    MockConfig config = MockConfig::standard();
    config.characters[0].inventory.push_back(bank_bag(3)); // the first zone-in has the bank (LazyLoadBank off)
    Rig rig(std::move(config));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    CHECK(conn.bank_live());
    CHECK(conn.bank_seen().seen == 0); // nothing seen before
    REQUIRE(conn.inventory().at(net::ItemSlot{net::kInvBank, 3, -1, -1, 0, 0}));

    // The next zone-in leaves the bank out, as EQEmu does with Inventory:LazyLoadBank: the bank as it was is the copy.
    auto& inv = rig.server.config.characters[0].inventory;
    std::erase_if(inv, [](const net::Item& it) { return it.slot.type == net::kInvBank; });
    const auto revision = conn.bank_view_revision();
    REQUIRE(zone_again(rig));
    CHECK_FALSE(conn.bank_live());
    CHECK(conn.bank_view_revision() != revision);
    CHECK(conn.bank_seen().seen > 0);
    const net::Item* bag = seen_at(conn, net::kInvBank, 3);
    REQUIRE(bag);
    REQUIRE(bag->contents.size() == 1);
    CHECK(bag->contents[0].item.name == "Fine Steel Ring");
    // never in the inventory: nothing can click, move, use or count it
    CHECK(conn.inventory().at(net::ItemSlot{net::kInvBank, 3, -1, -1, 0, 0}) == nullptr);
    CHECK(conn.inventory().at(net::ItemSlot{net::kInvBank, 3, 2, -1, 0, 0}) == nullptr);

    // Near a banker the server sends the bank slot by slot (type 0x67): that is the bank again. The bag moved to
    // slot 5 meanwhile; the copy is kept until the next capture, but no longer listed.
    rig.server.item_packet(net::kItemPacketTrade, bank_bag(5));
    REQUIRE(rig.until([&] { return conn.bank_live(); }));
    CHECK(conn.inventory().at(net::ItemSlot{net::kInvBank, 5, 2, -1, 0, 0}));
    CHECK(conn.inventory().at(net::ItemSlot{net::kInvBank, 3, -1, -1, 0, 0}) == nullptr);

    // the next zone-in (bank left out again) takes the bank as it is now
    REQUIRE(zone_again(rig));
    CHECK_FALSE(conn.bank_live());
    CHECK(seen_at(conn, net::kInvBank, 5));
    CHECK_FALSE(seen_at(conn, net::kInvBank, 3));
}

TEST_CASE("bank snapshot: opening the bank makes what the inventory holds the bank, even an empty one") {
    MockConfig config = MockConfig::standard();
    config.characters[0].inventory.push_back(banked(1001, "Cloth Cap", net::kInvBank, 0));
    Rig rig(std::move(config));
    auto& conn = rig.conn;
    REQUIRE(rig.to_zone());
    rig.server.config.characters[0].inventory.pop_back(); // emptied at the bank (not seen) and the bank left out
    REQUIRE(zone_again(rig));
    REQUIRE_FALSE(conn.bank_live());
    REQUIRE(seen_at(conn, net::kInvBank, 0));
    // Nothing comes for an empty bank; the window's opening says the bank is what the inventory has: nothing.
    REQUIRE(conn.activate_bank(rig.server.config.own_spawn_id));
    CHECK(conn.bank_live());
    conn.close_bank(); // and closing it takes the copy: the bank is empty
    CHECK(conn.bank_seen().items.empty());
}

TEST_CASE("bank snapshot: bank.dat keeps the bank as last seen for the character's next session") {
    ScratchDir scratch("bank_snapshot_test");
    net::ConnectionOptions options = quick_options();
    options.profile_id = "test-server";
    options.servers_dir = scratch.path() / "servers";
    options.bank_snapshot_file = true;
    const fs::path file = options.servers_dir / "test-server" / "characters" / "Mock EQEmu" / "Gennro" / "bank.dat";
    {
        MockConfig config = MockConfig::standard();
        config.characters[0].inventory.push_back(bank_bag(3));
        config.characters[0].inventory.push_back(banked(1001, "Cloth Cap", net::kInvSharedBank, 1));
        Rig rig(std::move(config), options);
        REQUIRE(rig.to_zone());
        CHECK_FALSE(fs::exists(file)); // written when the session or the zone ends, or the bank closes
        rig.conn.disconnect();
    }
    REQUIRE(fs::exists(file));
    {
        Rig rig(MockConfig::standard(), options); // the bank left out of the zone-in
        REQUIRE(rig.to_zone());
        CHECK_FALSE(rig.conn.bank_live());
        CHECK(rig.conn.bank_seen().seen > 0);
        CHECK(seen_at(rig.conn, net::kInvBank, 3));
        CHECK(seen_at(rig.conn, net::kInvSharedBank, 1));
        rig.conn.disconnect(); // not live: the file stays as it was
    }
    {
        // Without the option (tests, a test login) nothing is read or written.
        net::ConnectionOptions off = options;
        off.bank_snapshot_file = false;
        Rig rig(MockConfig::standard(), off);
        REQUIRE(rig.to_zone());
        CHECK(rig.conn.bank_seen().items.empty());
    }
    {
        Rig rig(MockConfig::standard(), options);
        REQUIRE(rig.to_zone());
        CHECK(seen_at(rig.conn, net::kInvBank, 3));
    }
    // a damaged file is ignored
    { std::ofstream(file, std::ios::binary | std::ios::trunc) << "not a bank"; }
    Rig rig(MockConfig::standard(), options);
    REQUIRE(rig.to_zone());
    CHECK(rig.conn.bank_seen().items.empty());
    CHECK(rig.conn.bank_seen().seen == 0);
}
