// Item placement and placed objects' menus against the mock server (net/connection/game_connection_real_estate.cpp).

#include <doctest/doctest.h>

#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/real_estate_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

const AppPacket* last_sent(const std::vector<AppPacket>& log, std::uint16_t op) {
    for (auto it = log.rbegin(); it != log.rend(); ++it)
        if (it->opcode == op) return &*it;
    return nullptr;
}

} // namespace

TEST_CASE("mock server: a placement reply begins placing, a valid spot confirms, code 0 ends it") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());

    // An area 7 around the spot with code 0 in the reply's map.
    ReArea area;
    area.id = 7;
    area.m = {50, 0, 0, 0, 0, 50, 0, 0, 0, 0, 100, 0, 100, 100, 0, 1};
    server.zone.send_reliable(encode(std::vector<ReArea>{area}));
    RePlaceReply reply;
    reply.item.tag = "IT63";
    reply.item.key = "0000000000000009";
    reply.item.re = 2;
    reply.item.item_id = 9;
    reply.index = {0, 33, -1, -1};
    reply.b1 = 1;
    reply.codes = {{7, 0}, {1, 17}};
    server.zone.send_reliable(encode(reply));
    REQUIRE(rig.until([&] { return conn.placement().active(); }));

    // Out of the area: the root's code (none known: 1); a click sends nothing.
    conn.placement_update({500, 500, 0}, {-1, -1, -1}, {1, 1, 1}, 0);
    CHECK(conn.placement().code() == 1);
    CHECK_FALSE(conn.placement_click({}, {}));
    // In it: valid; a refusal the app found keeps it red.
    conn.placement_update({100, 100, 0}, {-1, -1, -1}, {1, 1, 1}, 0x1c);
    CHECK(conn.placement().code() == 0x1c);
    conn.placement_update({100, 100, 0}, {-1, -1, -1}, {1, 1, 1}, 0);
    CHECK(conn.placement().code() == 0);
    conn.placement_rotate(128, 0, 0);
    REQUIRE(conn.placement_click({1, 1, 1}, {-1, -1, -1}));
    CHECK(conn.real_estate().pending());
    REQUIRE(rig.until([&] { return last_sent(server.zone_log, kOpRePlaceConfirm) != nullptr; }));
    auto c = decode_re_place_confirm(*last_sent(server.zone_log, kOpRePlaceConfirm));
    REQUIRE(c);
    CHECK(c->re == 2);
    CHECK(c->item == 9);
    CHECK(c->heading == doctest::Approx(128));
    CHECK(c->pos.x == doctest::Approx(100));
    CHECK(c->index.slot == 33);

    ByteWriter ok;
    ok.le<std::int32_t>(0);
    server.zone.send_reliable(AppPacket(kOpReResult, ok.take()));
    REQUIRE(rig.until([&] { return !conn.placement().active(); }));
    CHECK_FALSE(conn.real_estate().pending());

    // 0x6213 ends a session too.
    server.zone.send_reliable(encode(reply));
    REQUIRE(rig.until([&] { return conn.placement().active(); }));
    server.zone.send_reliable(AppPacket(kOpRePlaceStop));
    REQUIRE(rig.until([&] { return !conn.placement().active(); }));
}

TEST_CASE("mock server: a placed object's and an NPC item's right click ask for the menu") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    REQUIRE(rig.to_zone());
    const game::Spawn* me = conn.world().player();
    REQUIRE(me);
    // Far from the player and not the current estate: no request.
    CHECK_FALSE(conn.re_request_placed_menu(2, 9, me->position + glm::vec3(5000, 0, 0), 2.0f, 10, 20));
    CHECK(conn.re_request_placed_menu(2, 9, me->position + glm::vec3(10, 0, 0), 2.0f, 10, 20));
    REQUIRE(rig.until([&] { return last_sent(server.zone_log, kOpReContextMenu) != nullptr; }));
    const AppPacket* q = last_sent(server.zone_log, kOpReContextMenu);
    REQUIRE(q->data.size() == kReMenuSize);
    ByteReader r(q->data);
    r.seek(0x20);
    CHECK(r.le<std::uint32_t>() == 2);
    CHECK(r.le<std::uint32_t>() == 9);

    // 0x21ea marks the player's own spawn id as an NPC item of estate 3, item 4 (any spawn will do here).
    std::vector<std::byte> body(0x20, std::byte{0});
    std::uint32_t id = conn.world().player_id;
    for (int i = 0; i < 4; ++i) body[static_cast<std::size_t>(i)] = static_cast<std::byte>((id >> (8 * i)) & 0xff);
    body[0x18] = std::byte{3};
    body[0x1c] = std::byte{4};
    server.zone.send_reliable(AppPacket(kOpReNpcItem, body));
    REQUIRE(rig.until([&] { return conn.re_npc_item(id).has_value(); }));
    CHECK(conn.re_npc_item(id)->second == 4);
    std::size_t before = server.zone_log.size();
    CHECK(conn.re_request_npc_item_menu(id, 1, 1));
    REQUIRE(rig.until([&] {
        for (std::size_t i = before; i < server.zone_log.size(); ++i)
            if (server.zone_log[i].opcode == kOpReContextMenu) return true;
        return false;
    }));
}
