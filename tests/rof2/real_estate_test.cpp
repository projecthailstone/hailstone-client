// Real estate: the packets (net/protocol/rof2/packets/real_estate_packets.h) and the manager's state and occupancy (game/real_estate.h);
// sources there.

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "core/byte_io.h"
#include "game/real_estate.h"
#include "net/protocol/rof2/packets/real_estate_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using hailstone::game::RealEstateManager;

namespace {

ReDefinition def(std::uint32_t id, ReType type, std::int32_t area_link = -1) {
    ReDefinition d;
    d.id = id;
    d.type = type;
    d.f01 = area_link;
    d.name = "def";
    return d;
}

ReRecord rec(std::uint32_t id, std::uint32_t parent, std::uint32_t definition, std::vector<std::uint32_t> children = {}) {
    ReRecord r;
    r.id = id;
    r.parent = parent;
    r.definition = definition;
    r.children = std::move(children);
    return r;
}

// An area: a box of half extents h centred at c (server axes), as a row-major local → world matrix.
ReArea box(std::uint32_t id, glm::vec3 c, glm::vec3 h) {
    ReArea a;
    a.id = id;
    a.m = {h.x, 0, 0, 0, 0, h.y, 0, 0, 0, 0, h.z, 0, c.x, c.y, c.z, 1};
    return a;
}

// A neighborhood 1 (def 10) with plot 2 (def 20, area link 7), plot 2's house 3 (def 30) and crate 4 (def 40).
RealEstateManager town() {
    RealEstateManager m;
    ReCatalog c;
    c.definitions = {def(10, ReType::Neighborhood), def(20, ReType::Plot, 7), def(30, ReType::House),
                     def(40, ReType::Crate)};
    m.set_catalog(c);
    m.set_current(rec(1, game::kReNone, 10, {2}));
    m.add_record(rec(2, 1, 20, {3, 4}));
    m.add_record(rec(3, 2, 30));
    m.add_record(rec(4, 2, 40));
    return m;
}

} // namespace

TEST_CASE("real estate: a record keeps its fields through the wire, the name between the count and the ids") {
    ReRecord r = rec(5, 1, 20, {6, 7});
    r.guild_id = 12;
    r.address = "1 Main";
    r.owner = "Bob";
    r.name = "Plot";
    r.escrow = {{0, 100000.0}, {3, 2.0}};
    r.upkeep_set = 3;
    r.opt_out = true;
    AppPacket p = encode_re_record(kOpReRecord, r);
    // id .. f28, then "1 Main" as u32 length + bytes.
    ByteReader peek(p.data);
    peek.skip(24);
    CHECK(peek.le<std::uint32_t>() == 6);
    auto back = decode_re_record(p);
    REQUIRE(back);
    CHECK(back->id == 5);
    CHECK(back->parent == 1);
    CHECK(back->guild_id == 12);
    CHECK(back->address == "1 Main");
    CHECK(back->owner == "Bob");
    CHECK(back->name == "Plot");
    CHECK(back->children == std::vector<std::uint32_t>{6, 7});
    REQUIRE(back->escrow.size() == 2);
    CHECK(back->escrow[0].amount == doctest::Approx(100000.0));
    CHECK(back->upkeep_set == 3);
    CHECK(back->opt_out);
    p.data.pop_back();
    CHECK_FALSE(decode_re_record(p));
}

TEST_CASE("real estate: the catalog needs both counts and replaces the last one") {
    CHECK_FALSE(decode_re_catalog(AppPacket(kOpReCatalog, std::vector<std::byte>(4))));
    auto empty = decode_re_catalog(AppPacket(kOpReCatalog, std::vector<std::byte>(8)));
    REQUIRE(empty);
    CHECK(empty->definitions.empty());
    ReCatalog c;
    c.definitions = {def(20, ReType::Plot)};
    c.costs = {{3, {{1, {{0, 0, 12000}}}}}};
    auto back = decode_re_catalog(encode(c));
    REQUIRE(back);
    REQUIRE(back->definitions.size() == 1);
    CHECK(back->definitions[0].type == ReType::Plot);
    REQUIRE(back->costs.size() == 1);
    CHECK(back->costs[0].entries[0].leaves[0].amount == 12000);
    RealEstateManager m;
    m.set_catalog(*back);
    CHECK(m.definition_type(20) == ReType::Plot);
    m.set_catalog(ReCatalog{});
    CHECK(m.definition_type(20) == ReType::Unknown);
}

TEST_CASE("real estate: 0x3968 carries the estate first and a dword after the table, 0x485d neither after") {
    ReAccessMessage a;
    a.re = 9;
    a.access.members = {{"Ann", 3}, {"_Guild1", 5}};
    a.tail = 77;
    auto back = decode_re_access(encode(kOpReAccess, a));
    REQUIRE(back);
    CHECK(back->re == 9);
    CHECK(back->access.groups[4].bits == 0x1f87);
    REQUIRE(back->access.members.size() == 2);
    CHECK(back->access.members[1].name == "_Guild1");
    CHECK(back->tail == 77u);
    a.tail.reset();
    auto upd = decode_re_access(encode(kOpReAccessUpdate, a));
    REQUIRE(upd);
    CHECK_FALSE(upd->tail);
}

TEST_CASE("real estate: the search reply's forms") {
    ReSearchReply plots;
    plots.kind = ReSearchReply::PlotList;
    plots.target = 4;
    plots.title = "Plots";
    plots.plots = {{600, ReType::Plot, 2, "Bob", "1 Main"}, {600, ReType::Neighborhood, 3, "x", "y"},
                   {600, ReType::GuildPlot, 5, "", "2 Main"}};
    auto p = decode_re_search_reply(encode(plots));
    REQUIRE(p);
    CHECK(p->kind == ReSearchReply::PlotList);
    REQUIRE(p->plots.size() == 2); // the neighborhood row is read and dropped
    CHECK(p->plots[1].re == 5);

    ReSearchReply hoods;
    hoods.kind = ReSearchReply::Neighborhoods;
    hoods.target = 0;
    hoods.show = true;
    hoods.header_id = 1;
    hoods.neighborhoods = {{1, "Hood", -1, 10, 2, 4, 1}};
    auto h = decode_re_search_reply(encode(hoods));
    REQUIRE(h);
    CHECK(h->kind == ReSearchReply::Neighborhoods);
    CHECK(h->show);
    REQUIRE(h->neighborhoods.size() == 1);
    CHECK(h->neighborhoods[0].guild_plots == 4);

    ReSearchReply one;
    one.kind = ReSearchReply::Record;
    one.record = rec(8, 1, 20);
    one.record.owner = "Cy";
    one.auto_open = true;
    auto r = decode_re_search_reply(encode(one));
    REQUIRE(r);
    CHECK(r->kind == ReSearchReply::Record);
    CHECK(r->record.id == 8);
    CHECK(r->record.definition == 20);
    CHECK(r->record.owner == "Cy");
    CHECK(r->auto_open);

    // The caching trap: a known id gets only the owner and guild.
    RealEstateManager m = town();
    ReRecord known = rec(2, 99, 99);
    known.owner = "New";
    m.merge_search_record(known);
    CHECK(m.record(2)->parent == 1);
    CHECK(m.record(2)->owner == "New");
}

TEST_CASE("real estate: the escrow object both ways (coin types 3 platinum .. 0 copper)") {
    AppPacket p = encode_re_escrow(ReEscrowAction::Deposit, 2, {{0, 3, 5}});
    // 12 bytes, two empty strings, u8, u32, the count, one 16-byte entry: the 39 bytes captured.
    CHECK(p.data.size() == 39);
    CHECK(encode_re_escrow(ReEscrowAction::AddDay, 2).data.size() == 23);
    auto back = decode_re_escrow(p);
    REQUIRE(back);
    CHECK(back->re == 2);
    REQUIRE(back->entries.size() == 1);
    CHECK(back->entries[0].coin_type == 3);
    CHECK(back->entries[0].amount == 5);
}

TEST_CASE("real estate: fixed bodies") {
    AppPacket buy = encode_re_purchase(2, 11, 12);
    CHECK(buy.data.size() == kReTransactionSize);
    auto t = decode_re_transaction(buy);
    REQUIRE(t);
    CHECK(t->re == 2);
    CHECK(t->ac == 11);
    CHECK(t->b0 == 12);
    auto sell = decode_re_transaction(encode_re_sell(2));
    REQUIRE(sell);
    CHECK(sell->a == 0xffffffffu);
    auto give = decode_re_transaction(encode_re_transfer(2, "Bob"));
    REQUIRE(give);
    CHECK(give->text6c == "Bob");
    CHECK(encode_re_search(4, 1).data.size() == 0xe0);
    CHECK(encode_re_travel(1, 0xffffffffu).data.size() == 12);
    CHECK(encode_re_place_request("0000000000000001", 30, 1, 2).data.size() == 60);
    AppPacket store = encode_re_to_storage("1234567890123456", 2, 9, 3);
    CHECK(store.data.size() == 48);
    ByteReader sr(store.data);
    sr.skip(0x14);
    CHECK(sr.le<std::uint32_t>() == 0xffffffffu);
    sr.skip(8);
    CHECK(sr.le<std::uint32_t>() == 2); // +0x20 re
    CHECK(sr.le<std::uint32_t>() == 9); // +0x24 item
    sr.skip(4);
    CHECK(sr.le<std::uint32_t>() == 3); // +0x2c the destination
    CHECK(encode_re_to_inventory("1234567890123456", 2, 9).data.size() == 44);
    CHECK(re_container_index(ReType::House) == 2);
    CHECK(re_container_index(ReType::GuildPlot) == 3);
    CHECK(re_container_index(ReType::Crate) == 6);

    ReMenu menu;
    menu.key = "0000000000000042";
    menu.re = 2;
    menu.item = 9;
    menu.entries = {{0, 2, 0, 719, -1}, {3, 1, 0, 720, -1}};
    AppPacket mp = encode(menu);
    CHECK(mp.data.size() == kReMenuSize);
    auto mb = decode_re_menu(mp);
    REQUIRE(mb);
    CHECK(mb->key == "0000000000000042");
    REQUIRE(mb->entries.size() == 2);
    CHECK(mb->entries[0].text == 719);
    CHECK(mb->entries[1].index == 3);
    CHECK(mb->entries[1].type == 1);
    // The request: the key, the ids, 0xff entries from +0x6c, the byte at +0x2ec and the click.
    AppPacket q = encode_re_menu_request("0000000000000042", 2, 9, 10, 20, 1001);
    REQUIRE(q.data.size() == kReMenuSize);
    CHECK(q.data[0x6c] == std::byte{0xff});
    CHECK(q.data[0x2ec] == std::byte{1});
    auto qb = decode_re_menu(q);
    REQUIRE(qb);
    CHECK(qb->entries.empty());
    CHECK(qb->x == 10);
    CHECK(qb->f2f8 == 1001);
}

TEST_CASE("real estate: a placed object and the areas") {
    RePlacedItem it;
    it.tag = "IT63";
    it.key = "0000000000000007";
    it.re = 2;
    it.item_id = 7;
    it.heading = 128;
    it.server_pos = {10, 20, 30};
    it.no_collision = true;
    auto back = decode_re_placed_item(encode(it));
    REQUIRE(back);
    CHECK(back->tag == "IT63");
    CHECK(back->key == "0000000000000007");
    CHECK(back->server_pos.y == doctest::Approx(20));
    CHECK(back->no_collision);
    AppPacket confirm = encode_re_place_confirm(it);
    CHECK(confirm.data.size() == 0x60);

    auto areas = decode_re_areas(encode(std::vector<ReArea>{box(7, {0, 0, 0}, {86, 86, 350})}));
    REQUIRE(areas);
    REQUIRE(areas->size() == 1);
    CHECK(RealEstateManager::point_in_area((*areas)[0], {50, -80, 0}));
    CHECK_FALSE(RealEstateManager::point_in_area((*areas)[0], {90, 0, 0}));
    CHECK_FALSE(decode_re_placed_item_remove(AppPacket(kOpRePlacedItemRemove, std::vector<std::byte>(0x27))));

    RealEstateManager m;
    CHECK(m.add_placed(it));
    it.heading = 0;
    CHECK_FALSE(m.add_placed(it));
    CHECK(m.placed(2, 7)->heading == 0);
    m.remove_placed(2, 7);
    CHECK(m.placed_items().empty());
}

TEST_CASE("real estate: occupancy follows the root, the areas, then the house, yard and crate") {
    RealEstateManager m = town();
    m.process(1000, true, {0, 0, 0});
    // No area: the current estate is the root (the neighborhood), so no yard, house or crate.
    CHECK(m.occupancy().root == 1);
    CHECK(m.occupancy().root_type == ReType::Neighborhood);
    CHECK(m.occupancy().current == 1);
    CHECK(m.occupancy().yard == game::kReNone);

    // Plot 2's definition links it to area 7: standing in it puts the player on the plot.
    m.set_areas({box(7, {100, 100, 0}, {86, 86, 350})});
    m.process(1500, true, {120, 90, 0});
    CHECK(m.occupancy().current == 1); // within the second
    m.process(2000, true, {120, 90, 0});
    CHECK(m.occupancy().current == 2);
    CHECK(m.occupancy().yard == 2);
    CHECK(m.occupancy().house == 3);
    CHECK(m.occupancy().crate == 4);

    // Without records the ids follow the root at once.
    RealEstateManager empty;
    empty.process(0, true, {});
    CHECK(empty.occupancy().current == game::kReNone);
}

TEST_CASE("real estate: a crate search over child lists that repeat and name themselves ends") {
    // 0x5b45b0 walks the child lists depth first, 10 deep. A plot listing itself 1000 times branched 1000 ways
    // at every level (1000^10 calls, a frozen client); each record's children are walked once per depth now.
    RealEstateManager m = town();
    m.set_areas({box(7, {100, 100, 0}, {86, 86, 350})});
    ReRecord loop = rec(2, 1, 20, std::vector<std::uint32_t>(1000, 2));
    m.add_record(loop);
    m.process(1000, true, {120, 90, 0});
    m.process(2000, true, {120, 90, 0});
    CHECK(m.occupancy().yard == 2);
    CHECK(m.occupancy().crate == game::kReNone);
    // The crate after the repeats is still the one found, as before.
    loop.children.push_back(4);
    m.add_record(loop);
    m.process(3000, true, {120, 90, 0});
    CHECK(m.occupancy().crate == 4);
    // A chain of records each listing the next many times.
    for (std::uint32_t id = 50; id < 60; ++id) m.add_record(rec(id, id - 1, 30, std::vector<std::uint32_t>(300, id + 1)));
    loop.children = {50};
    m.add_record(loop);
    m.process(4000, true, {120, 90, 0});
    CHECK(m.occupancy().crate == game::kReNone);
}

TEST_CASE("real estate: the pending latch lets go after 5 s") {
    RealEstateManager m;
    m.set_pending(100);
    m.process(5000, false, {});
    CHECK(m.pending());
    m.process(5100, false, {});
    CHECK_FALSE(m.pending());
}

TEST_CASE("real estate: owned lists and item rows") {
    RealEstateManager m;
    m.set_owned(RealEstateManager::OwnedList::A, {2, 2, 5});
    CHECK(m.owned(RealEstateManager::OwnedList::A) == std::vector<std::uint32_t>{2, 5});
    m.toggle_owned(RealEstateManager::OwnedList::A, 2, false);
    CHECK(m.owned(RealEstateManager::OwnedList::A) == std::vector<std::uint32_t>{5});

    ReItemList list;
    list.re = 2;
    list.name = "Plot";
    ReItemRow row;
    row.re = 2;
    row.item_id = 9;
    row.name = "Chair";
    row.item.id = 1001;
    row.item.name = "Chair";
    list.rows = {row};
    auto back = decode_re_item_list(encode(kOpReItemSummary, list));
    REQUIRE(back);
    REQUIRE(back->rows.size() == 1);
    CHECK(back->rows[0].item.id == 1001);
    m.add_item_rows(*back);
    m.add_item_rows(*back);
    CHECK(m.summary(2)->rows.size() == 1);
    CHECK(m.summary(2)->name == "Plot");
    m.remove_item(2, 9);
    CHECK(m.summary(2)->rows.empty());
}

TEST_CASE("real estate: result codes, sell and transfer messages") {
    CHECK(game::re_result_message(0) == 0);
    CHECK(game::re_result_message(-1) == 0);
    CHECK(game::re_result_message(10) == 5219);
    CHECK(game::re_result_message(82) == 5429);
    CHECK(game::re_result_message(1) == 5223); // unlisted: "request failed"
    CHECK(game::re_sell_message(0) == 0);
    CHECK(game::re_sell_message(4) == 0x149c);
    CHECK(game::re_sell_message(1) == 0x149e);
    CHECK(game::re_transfer_message(3) == 0x1dd5);
    CHECK(game::re_transfer_message(9) == 0x14a0);
}

TEST_CASE("real estate: upkeep time left") {
    RealEstateManager m;
    ReCatalog c;
    ReDefinition plot = def(20, ReType::Plot);
    plot.upkeep_group = 50;
    c.definitions = {plot};
    c.costs = {{50, {{3, {{0, 0, 12000}}}}}};
    m.set_catalog(c);
    ReRecord r = rec(2, 1, 20);
    r.upkeep_set = 3;
    r.escrow = {{0, 100000.0}};
    double days = game::re_upkeep_days(m, r);
    CHECK(days == doctest::Approx(100000.0 / 12000.0));
    // The proven reading: 100000 copper at 12 platinum a day is "1w 1d".
    CHECK(game::re_format_time_left(days) == "1w 1d");
    CHECK(game::re_format_time_left(3.5) == "3d");
    CHECK(game::re_format_time_left(0.5) == "<1d");
    CHECK(game::re_format_time_left(0) == "Expired");
    CHECK(game::re_format_time_left(-1) == "Unlimited");
    // A balance the server sent as 1e300, infinity or NaN: no undefined conversion (UBSan), a saturated count.
    const std::string most = std::to_string(std::numeric_limits<long long>::max());
    CHECK(game::re_format_time_left(1e300) == most + "w " + std::to_string(std::numeric_limits<long long>::max() % 7) + "d");
    CHECK(game::re_format_time_left(std::numeric_limits<double>::infinity()).starts_with(most + "w "));
    CHECK(game::re_format_time_left(std::numeric_limits<double>::quiet_NaN()) == "<1d");
    r.upkeep_set = 4;
    CHECK(game::re_upkeep_days(m, r) == -1.0);
}
