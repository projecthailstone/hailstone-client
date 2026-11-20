// Placed real estate objects and item placement mode (game/placement.h), and the placement packets
// (net/protocol/rof2/packets/real_estate_packets.h); sources there.

#include <doctest/doctest.h>

#include <cstring>

#include "core/byte_io.h"
#include "game/placement.h"
#include "net/protocol/rof2/packets/real_estate_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::game;

namespace {

std::uint32_t bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

RePlacedItem chair() {
    RePlacedItem it;
    it.tag = "IT63";
    it.key = "0000000000000009";
    it.re = 2;
    it.item_id = 9;
    it.scale = 1.0f;
    it.server_pos = {10, 20, 30};
    it.tail[0] = bits(0.5f); // scale min
    it.tail[1] = bits(2.0f); // scale max
    it.tail[2] = bits(1.0f); // Reset Size
    it.tail[4] = bits(64.0f); // heading reset
    it.tail[5] = bits(0.0f);  // pitch reset
    return it;
}

ReArea box(std::uint32_t id, glm::vec3 c, glm::vec3 h) {
    ReArea a;
    a.id = id;
    a.m = {h.x, 0, 0, 0, 0, h.y, 0, 0, 0, 0, h.z, 0, c.x, c.y, c.z, 1};
    return a;
}

} // namespace

TEST_CASE("placement: a placed object's transform: the position as CreateActor takes it, then heading, scale") {
    RePlacedItem o = chair();
    glm::mat4 t = placed_transform(o);
    CHECK(glm::vec3(t[3]).x == doctest::Approx(10));
    CHECK(glm::vec3(t[3]).y == doctest::Approx(20));
    // A quarter turn (128) about +Z, counterclockwise: +x goes to +y.
    o.heading = 128;
    o.scale = 2;
    glm::vec3 p = glm::vec3(placed_transform(o) * glm::vec4(1, 0, 0, 1)) - o.server_pos;
    CHECK(p.x == doctest::Approx(0).epsilon(1e-4));
    CHECK(p.y == doctest::Approx(2));
    o.scale = -1; // not a positive number: drawn at 1
    p = glm::vec3(placed_transform(o) * glm::vec4(1, 0, 0, 1)) - o.server_pos;
    CHECK(p.y == doctest::Approx(1));
    CHECK(placed_solid(chair()));
    o = chair();
    o.no_collision = true;
    CHECK_FALSE(placed_solid(o));
}

TEST_CASE("placement: the right click's reach (height + 50 + radius, or the current estate)") {
    CHECK(placed_menu_in_reach({0, 0, 0}, 5, {60, 0, 0}, 6, 2, 1));
    CHECK_FALSE(placed_menu_in_reach({0, 0, 0}, 5, {62, 0, 0}, 6, 2, 1));
    CHECK(placed_menu_in_reach({0, 0, 0}, 5, {500, 0, 0}, 6, 2, 2));
    CHECK(npc_item_menu_in_reach({0, 0, 0}, {59, 0, 0}, 3, 1));
    CHECK_FALSE(npc_item_menu_in_reach({0, 0, 0}, {61, 0, 0}, 3, 1));
}

TEST_CASE("placement: the menu's entries start at +0x6c, the request is the empty key with the click") {
    AppPacket q = encode_re_menu_request(2, 9, 300, 200);
    REQUIRE(q.data.size() == kReMenuSize);
    ByteReader r(q.data);
    CHECK(r.fixed_string(16) == "0000000000000000");
    r.seek(0x14);
    CHECK(r.le<std::int32_t>() == -1);
    r.seek(0x20);
    CHECK(r.le<std::uint32_t>() == 2);
    CHECK(r.le<std::uint32_t>() == 9);
    r.seek(0x6c);
    CHECK(r.le<std::int32_t>() == -1); // no entries
    r.seek(0x2f0);
    CHECK(r.le<std::int32_t>() == 300);
    CHECK(r.le<std::int32_t>() == 200);
    CHECK(r.le<std::int32_t>() == -1);

    ReMenu reply;
    reply.key = "0000000000000042";
    reply.re = 2;
    reply.item = 9;
    reply.entries = {{0, 2, 0, 719, -1}, {3, 1, 1, 724, 7}};
    AppPacket p = encode(reply);
    ByteReader e(p.data);
    e.seek(0x6c);
    CHECK(e.le<std::int32_t>() == 2);   // entry 0's kind
    CHECK(e.le<std::int32_t>() == 0);   // its flag
    CHECK(e.le<std::uint32_t>() == 719); // its label
    auto back = decode_re_menu(p);
    REQUIRE(back);
    REQUIRE(back->entries.size() == 2);
    CHECK(back->entries[1].index == 3);
    CHECK(back->entries[1].type == 1);
    CHECK(back->entries[1].sub == 7);
}

TEST_CASE("placement: the request, the reply and the confirm on the wire") {
    RePlaceRequest q;
    q.key = "1234567890123456";
    q.index = {0, kReCursorSlot, -1, -1};
    q.x = 5;
    q.y = 6;
    AppPacket qp = encode(q);
    REQUIRE(qp.data.size() == 60);
    auto qb = decode_re_place_request(qp);
    REQUIRE(qb);
    CHECK(qb->key == "1234567890123456");
    CHECK(qb->index.slot == 33);
    CHECK(qb->re == 0xffffffffu);
    CHECK(qb->x == 5);
    CHECK(qp.data[0x2c] == std::byte{1});
    CHECK(qp.data[0x30] == std::byte{1});

    RePlaceReply reply;
    reply.item = chair();
    reply.index = {0, 33, -1, -1};
    reply.b1 = 1;
    reply.codes = {{7, 0}, {1, 17}};
    auto rb = decode_re_place_reply(encode(reply));
    REQUIRE(rb);
    CHECK(rb->item.tag == "IT63");
    CHECK(rb->index.slot == 33);
    CHECK(rb->b1 == 1);
    REQUIRE(rb->codes.size() == 2);
    CHECK(rb->codes[1].second == 17);
    AppPacket cut = encode(reply);
    cut.data.resize(cut.data.size() - 4);
    CHECK_FALSE(decode_re_place_reply(cut));

    RePlacedItem g = chair();
    g.heading = 100;
    g.pitch = 2;
    g.roll = 3;
    g.scale = 1.5f;
    AppPacket cp = encode_re_place_confirm(g, reply.index, {1, 2, 3}, {-1, -2, -3});
    REQUIRE(cp.data.size() == 0x60);
    auto cb = decode_re_place_confirm(cp);
    REQUIRE(cb);
    CHECK(cb->key == "0000000000000009");
    CHECK(cb->index.slot == 33);
    CHECK(cb->re == 2);
    CHECK(cb->heading == doctest::Approx(100)); // heading, pitch, roll, scale, then x, y, z
    CHECK(cb->scale == doctest::Approx(1.5f));
    CHECK(cb->pos.y == doctest::Approx(20));
    CHECK(cb->box_a.z == doctest::Approx(3));
    CHECK(cb->box_b.x == doctest::Approx(-1));
}

TEST_CASE("placement: modes as 0x5ae160 allows them") {
    RePlacedItem o = chair();
    CHECK(place_mode_allowed(0, o));
    CHECK_FALSE(place_mode_allowed(1, o)); // walls need +0x9c == 1
    CHECK(place_mode_allowed(2, o));
    CHECK(place_mode_allowed(3, o));
    CHECK_FALSE(place_mode_allowed(4, o));
    o.wall_only = true;
    o.wall_allowed = 1;
    CHECK_FALSE(place_mode_allowed(0, o));
    CHECK(place_mode_allowed(1, o));
    o = chair();
    o.npc_item = true;
    CHECK_FALSE(place_mode_allowed(3, o));
}

TEST_CASE("placement: a session from the reply, its keys, the area query and the confirm") {
    RePlaceReply reply;
    reply.item = chair();
    reply.index = {0, 33, -1, -1};
    reply.codes = {{7, 0}, {1, 17}};
    Placement pl;
    pl.begin(reply);
    REQUIRE(pl.active());
    CHECK(pl.mode() == 2);
    // Toggle: cursor → free → collision (wall, 4 and 5 not allowed).
    CHECK(pl.set_mode(-1));
    CHECK(pl.mode() == 3);
    CHECK(pl.set_mode(-1));
    CHECK(pl.mode() == 0);
    CHECK_FALSE(pl.set_mode(1));
    CHECK(pl.mode() == 0);

    pl.rotate(128, 0, 0);
    pl.rotate(448, 0, 0);
    CHECK(pl.ghost().heading == doctest::Approx(64)); // kept in 0..512
    pl.rotate(0, 30, 0);
    pl.reset_rotation(false, true, false);
    CHECK(pl.ghost().pitch == doctest::Approx(0));
    pl.adjust_scale(5);
    CHECK(pl.ghost().scale == doctest::Approx(2.0f)); // clamped to +0xa4
    pl.reset_scale();
    CHECK(pl.ghost().scale == doctest::Approx(1.0f));

    // Area 7 (code 0) around (100, 100); the root (estate 1) has code 17 elsewhere.
    std::vector<ReArea> areas{box(7, {100, 100, 0}, {50, 50, 100})};
    pl.move_to({100, 100, 0});
    pl.validate(areas, 1, {-1, -1, -1}, {1, 1, 1});
    CHECK(pl.code() == 0);
    CHECK(pl.confirm({}, {}).has_value());
    pl.move_to({149.5f, 100, 0}); // a corner out of the area: the root's code
    pl.validate(areas, 1, {-1, -1, -1}, {1, 1, 1});
    CHECK(pl.code() == 17);
    CHECK_FALSE(pl.confirm({}, {}).has_value());
    pl.validate(areas, 5, {-1, -1, -1}, {1, 1, 1}); // no code for the root: 1
    CHECK(pl.code() == 1);
    pl.stop();
    CHECK_FALSE(pl.active());
}
