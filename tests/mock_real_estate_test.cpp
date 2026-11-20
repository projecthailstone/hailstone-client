// Real estate against the mock server: the server's records, catalog and areas reach the manager, occupancy follows
// the player, and the client's requests go out as the windows send them (net/connection/game_connection_real_estate.cpp).

#include <doctest/doctest.h>

#include <algorithm>

#include "core/byte_io.h"
#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/real_estate_packets.h"
#include "net/protocol/rof2/packets/social_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

ReDefinition def(std::uint32_t id, ReType type, std::int32_t area_link = -1) {
    ReDefinition d;
    d.id = id;
    d.type = type;
    d.f01 = area_link;
    d.price_group = 100;
    return d;
}

ReRecord rec(std::uint32_t id, std::uint32_t parent, std::uint32_t definition, std::vector<std::uint32_t> children = {}) {
    ReRecord r;
    r.id = id;
    r.parent = parent;
    r.definition = definition;
    r.children = std::move(children);
    r.name = "Lot " + std::to_string(id);
    return r;
}

const AppPacket* last_sent(const std::vector<AppPacket>& log, std::uint16_t op) {
    for (auto it = log.rbegin(); it != log.rend(); ++it)
        if (it->opcode == op) return &*it;
    return nullptr;
}

} // namespace

TEST_CASE("mock server: real estate arrives, occupancy follows the plot's area, a purchase and an offer go out") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());
    rig.chat();

    // The bring-up order: the catalog, the records (parents first), the current estate, then the areas. The mock's
    // character stands at (-285, -148, 3.5); plot 2's area is around it.
    ReCatalog catalog;
    catalog.definitions = {def(10, ReType::Neighborhood), def(20, ReType::Plot, 7), def(30, ReType::House)};
    catalog.costs = {{100, {{1, {{0, 0, 50000}}}}}};
    server.zone.send_reliable(encode(catalog));
    server.zone.send_reliable(encode_re_record(kOpReRecord, rec(2, 1, 20, {3})));
    server.zone.send_reliable(encode_re_record(kOpReRecord, rec(3, 2, 30)));
    server.zone.send_reliable(encode_re_record(kOpReSetCurrent, rec(1, game::kReNone, 10, {2})));
    ReArea area;
    area.id = 7;
    area.m = {86, 0, 0, 0, 0, 86, 0, 0, 0, 0, 350, 0, -285, -148, 0, 1};
    server.zone.send_reliable(encode(std::vector<ReArea>{area}));
    REQUIRE(rig.until([&] { return conn.real_estate().occupancy().yard == 2; }));
    CHECK(conn.real_estate().occupancy().root == 1);
    CHECK(conn.real_estate().occupancy().current == 2);
    CHECK(conn.real_estate().occupancy().house == 3);

    // The plot window on plot 2, then Purchase: 0x45be with the combos' keys; the window hides itself.
    conn.re_show_plot(2);
    CHECK(conn.re_can_purchase(2, false));
    conn.re_purchase(1, 0);
    REQUIRE(rig.until([&] { return last_sent(server.zone_log, kOpRePurchase) != nullptr; }));
    auto buy = decode_re_transaction(*last_sent(server.zone_log, kOpRePurchase));
    REQUIRE(buy);
    CHECK(buy->re == 2);
    CHECK(buy->ac == 1);
    CHECK_FALSE(conn.real_estate_ui().purchase_open);
    CHECK(conn.real_estate().pending());

    // The server's answer: 0x45be {1} opens the manage window, the record comes back owned, the result code
    // releases the latch.
    ReRecord owned = rec(2, 1, 20, {3});
    owned.owner = "Gennro";
    server.zone.send_reliable(AppPacket(kOpRePurchase, {std::byte{1}}));
    server.zone.send_reliable(encode_re_record(kOpReRecord, owned));
    ByteWriter ok;
    ok.le<std::int32_t>(0);
    server.zone.send_reliable(AppPacket(kOpReResult, ok.take()));
    REQUIRE(rig.until([&] { return !conn.real_estate().pending(); }));
    CHECK(conn.real_estate_ui().manage_open);
    CHECK(conn.real_estate().already_owned(2));
    // Owned now: a second purchase is refused before anything is sent.
    CHECK_FALSE(conn.re_can_purchase(2, false));

    // An unlisted result code prints "request failed" (5223).
    rig.chat();
    ByteWriter bad;
    bad.le<std::int32_t>(1);
    server.zone.send_reliable(AppPacket(kOpReResult, bad.take()));
    std::vector<ChatEvent> lines;
    REQUIRE(rig.until([&] {
        for (auto& e : rig.chat()) lines.push_back(std::move(e));
        return !lines.empty();
    }));

    // The 0x5a63 offer asks; a yes echoes the body with +0xb9 set.
    ReTransaction offer;
    offer.re = 2;
    offer.text0c = "Bob";
    server.zone.send_reliable(encode(kOpReOffer, offer));
    REQUIRE(rig.until([&] { return conn.confirmation().has_value(); }));
    conn.answer_confirmation(true);
    REQUIRE(rig.until([&] { return last_sent(server.zone_log, kOpReOffer) != nullptr; }));
    const AppPacket* echoed = last_sent(server.zone_log, kOpReOffer);
    REQUIRE(echoed->data.size() == kReTransactionSize);
    CHECK(echoed->data[0xb9] == std::byte{1});

    // A withdrawal: the escrow object with the u8 set and action 1 puts the coin on the cursor.
    ReEscrow pay;
    pay.action = 1;
    pay.re = 2;
    pay.b = 1;
    pay.entries = {{0, 3, 12}};
    server.zone.send_reliable(encode(pay));
    REQUIRE(rig.until([&] { return conn.cursor_coins()[3] == 12; }));
    // ... and deposited again from the cursor: one entry {coin, platinum, 12}, the cursor emptied.
    conn.re_escrow(ReEscrowAction::Deposit);
    CHECK(conn.cursor_coins()[3] == 0);
    REQUIRE(rig.until([&] { return last_sent(server.zone_log, kOpReEscrow) != nullptr; }));
    auto dep = decode_re_escrow(*last_sent(server.zone_log, kOpReEscrow));
    REQUIRE(dep);
    CHECK(dep->action == 0);
    CHECK(dep->re == 2);
    REQUIRE(dep->entries.size() == 1);
    CHECK(dep->entries[0].coin_type == 3);
    CHECK(dep->entries[0].amount == 12);

    // A placed object reaches the manager; its removal takes it away.
    RePlacedItem chair;
    chair.tag = "IT63";
    chair.key = "0000000000000009";
    chair.re = 2;
    chair.item_id = 9;
    chair.server_pos = {-280, -150, 3.5f};
    server.zone.send_reliable(encode(chair));
    REQUIRE(rig.until([&] { return conn.real_estate().placed(2, 9) != nullptr; }));
    std::vector<std::byte> gone(0x28, std::byte{0});
    gone[0x20] = std::byte{2};
    gone[0x24] = std::byte{9};
    server.zone.send_reliable(AppPacket(kOpRePlacedItemRemove, gone));
    REQUIRE(rig.until([&] { return conn.real_estate().placed(2, 9) == nullptr; }));
}

TEST_CASE("mock server: a purchase check on a plot whose parents form a cycle ends") {
    // Plot 5 under house 6 under plot 5: neither a guild plot nor a neighborhood, so both walks up used to loop.
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());
    ReCatalog catalog;
    catalog.definitions = {def(20, ReType::Plot), def(30, ReType::House)};
    catalog.costs = {{100, {{1, {{0, 0, 50000}}}}}};
    server.zone.send_reliable(encode(catalog));
    server.zone.send_reliable(encode_re_record(kOpReRecord, rec(5, 6, 20)));
    server.zone.send_reliable(encode_re_record(kOpReRecord, rec(6, 5, 30)));
    REQUIRE(rig.until([&] { return conn.real_estate().record(6) != nullptr; }));
    CHECK(conn.re_can_purchase(5, false));
}

TEST_CASE("mock server: the plot search window needs a neighborhood and asks for the plots when shown") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());
    // Not in a neighborhood: refused (eqstr 8078), nothing sent; /realestate plot asks the same way.
    conn.set_real_estate_window(GameConnection::ReWindow::PlotSearch, true);
    CHECK_FALSE(conn.real_estate_ui().plot_search_open);
    conn.send_command("/realestate plot");
    CHECK_FALSE(conn.real_estate_ui().plot_search_open);
    // /realestate items toggles the items window; /leaverealestate sends 0x1da9 with 4 bytes.
    conn.send_command("/realestate items");
    CHECK(conn.real_estate_ui().items_open);
    conn.send_command("/realestate ITEMS");
    CHECK_FALSE(conn.real_estate_ui().items_open);
    conn.send_command("/leaverealestate");
    REQUIRE(rig.until([&] { return last_sent(server.zone_log, kOpReLeave) != nullptr; }));
    CHECK(last_sent(server.zone_log, kOpReLeave)->data.size() == 4);

    ReCatalog catalog;
    catalog.definitions = {def(10, ReType::Neighborhood)};
    server.zone.send_reliable(encode(catalog));
    server.zone.send_reliable(encode_re_record(kOpReSetCurrent, rec(1, game::kReNone, 10)));
    REQUIRE(rig.until([&] { return conn.real_estate().occupancy().root == 1; }));
    // The server opens it with 0x2b35 {mode}: the window shows and sends the empty name search {4, root}.
    server.zone.send_reliable(AppPacket(kOpRePlotSearchOpen, {std::byte{1}}));
    REQUIRE(rig.until([&] { return conn.real_estate_ui().plot_search_open; }));
    CHECK(conn.real_estate_ui().plot_search_mode == 1);
    REQUIRE(rig.until([&] { return last_sent(server.zone_log, kOpReSearch) != nullptr; }));
    const AppPacket* q = last_sent(server.zone_log, kOpReSearch);
    REQUIRE(q->data.size() == 0xe0);
    ByteReader r(q->data);
    CHECK(r.le<std::uint32_t>() == 4);
    CHECK(r.le<std::uint32_t>() == 1);

    ReSearchReply plots;
    plots.kind = ReSearchReply::PlotList;
    plots.target = 4;
    plots.title = "Plots";
    plots.plots = {{600, ReType::Plot, 2, "", "1 Main"}};
    server.zone.send_reliable(encode(plots));
    REQUIRE(rig.until([&] { return conn.real_estate_ui().plots.size() == 1; }));
    CHECK(conn.real_estate_ui().plot_search_title == "Plots");
}

TEST_CASE("mock server: an instanced guild hall's bank is the guild's whose guild plot holds it") {
    // The character in an instanced Grand Guild Hall (737, bit 15 of the zone word set).
    MockConfig cfg = MockConfig::standard();
    constexpr std::uint16_t kHall = 737 | 0x8000;
    MockZone hall = cfg.zones.at(cfg.characters.at(0).zone_id);
    hall.id = kHall;
    cfg.zones[kHall] = hall;
    cfg.characters.at(0).zone_id = kHall;
    Rig rig(cfg);
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());
    REQUIRE(conn.zone_id() == kHall);
    CHECK(conn.in_guild_hall_zone());
    server.zone.send_reliable(net::encode(net::SpawnAppearance{0, 22, 3}));
    REQUIRE(rig.until([&] { return conn.guild().in_guild(); }));
    CHECK_FALSE(conn.guild_bank_allowed()); // no estate says whose hall it is

    // The hall (a guild hall estate) under guild plot 5 of guild 3: the root is the hall.
    ReCatalog catalog;
    catalog.definitions = {def(80, ReType::GuildPlot), def(20, ReType::GuildHall)};
    server.zone.send_reliable(encode(catalog));
    ReRecord plot = rec(5, game::kReNone, 80, {6});
    plot.guild_id = 3;
    server.zone.send_reliable(encode_re_record(kOpReRecord, plot));
    server.zone.send_reliable(encode_re_record(kOpReSetCurrent, rec(6, 5, 20)));
    REQUIRE(rig.until([&] { return conn.real_estate().occupancy().root == 6; }));
    CHECK(conn.guild_bank_allowed());
    // Another guild's plot: refused.
    plot.guild_id = 4;
    server.zone.send_reliable(encode_re_record(kOpReRecord, plot));
    REQUIRE(rig.until([&] { return conn.real_estate().record(5)->guild_id == 4; }));
    CHECK_FALSE(conn.guild_bank_allowed());
}
