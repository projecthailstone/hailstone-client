// game/zone_routes.h (the map's zone links and routes) and net/protocol/extensions/hailstone/zone_link_packets.h (the server's list).
#include <doctest/doctest.h>

#include <set>
#include <vector>

#include "game/zone_routes.h"
#include "net/protocol/extensions/hailstone/zone_link_packets.h"

using namespace hailstone;
using game::ZoneRoutes;

TEST_CASE("Zone routes: the fewest zone changes, the lowest ids first among equals") {
    ZoneRoutes r;
    CHECK(r.empty());
    // 1 -> 2 -> 4 and 1 -> 3 -> 4: both two changes; 2 is lower.
    CHECK(r.set_server({{1, 2}, {2, 4}, {1, 3}, {3, 4}, {4, 5}, {5, 5}, {0, 1}}));
    CHECK(r.has_server());
    CHECK(r.route(1, 5).zones == std::vector<std::uint32_t>{1, 2, 4, 5});
    CHECK_FALSE(r.route(1, 5).assumed);
    CHECK(r.route(3, 3).zones == std::vector<std::uint32_t>{3});
    CHECK(r.route(1, 99).zones.empty());
    CHECK(r.route(0, 1).zones.empty());
    CHECK(r.neighbours(1) == std::vector<std::uint32_t>{2, 3});
    CHECK(r.zones() == std::vector<std::uint32_t>{1, 2, 3, 4, 5}); // the self link and zone 0 were left out
    CHECK_FALSE(r.set_server({{1, 2}, {2, 4}, {1, 3}, {3, 4}, {4, 5}})); // the same list again
}

TEST_CASE("Zone routes: learned links, and one seen only from the far side marks the route assumed") {
    ZoneRoutes r;
    CHECK(r.learn(10, {11, 12, 10, 0}));
    CHECK_FALSE(r.learn(10, {11}));
    CHECK(r.learn(12, {13}));
    CHECK(r.route(10, 13).zones == std::vector<std::uint32_t>{10, 12, 13});
    CHECK_FALSE(r.route(10, 13).assumed);
    // Nothing leads from 13 or 11 yet: back the way the links came.
    auto back = r.route(13, 11);
    CHECK(back.zones == std::vector<std::uint32_t>{13, 12, 10, 11});
    CHECK(back.assumed);
    // The server's list does not go backwards on its own.
    ZoneRoutes s;
    s.set_server({{1, 2}});
    CHECK(s.route(2, 1).zones.empty());
}

TEST_CASE("Zone routes: the file keeps both sources and skips what it cannot read") {
    ZoneRoutes r;
    r.set_server({{1, 2}, {2, 3}});
    r.learn(7, {8});
    ZoneRoutes back = ZoneRoutes::parse(r.serialize());
    CHECK(back.has_server());
    CHECK(back.route(1, 3).zones == std::vector<std::uint32_t>{1, 2, 3});
    CHECK(back.route(8, 7).assumed); // learned, so it may be used backwards
    CHECK(back.serialize() == r.serialize());
    ZoneRoutes junk = ZoneRoutes::parse("# comment\ns 1\nl x 2\ns 4 4\nq 1 2\ns 0 3\r\nl 5 6\r\ns 99999999999 1\n");
    CHECK_FALSE(junk.has_server());
    CHECK(junk.zones() == std::vector<std::uint32_t>{5, 6});
}

TEST_CASE("Zone link packets: request, reply, bad replies") {
    auto req = net::encode_zone_links_request();
    CHECK(req.opcode == net::kOpHsZoneLinksRequest);
    CHECK(req.data.size() == 6);
    net::ZoneLinks v;
    v.links = {{1, 2}, {2, 300}};
    auto p = net::encode(v);
    CHECK(p.opcode == net::kOpHsZoneLinks);
    CHECK(p.data.size() == 12 + 8);
    auto d = net::decode_zone_links(p);
    REQUIRE(d);
    CHECK(d->links == v.links);
    // Fewer links than the count says.
    auto short_p = p;
    short_p.data.resize(short_p.data.size() - 1);
    CHECK_FALSE(net::decode_zone_links(short_p));
    // Another magic, another opcode.
    auto bad = p;
    bad.data[0] = std::byte{0};
    CHECK_FALSE(net::decode_zone_links(bad));
    auto other = p;
    other.opcode = net::kOpHsZoneLinksRequest;
    CHECK_FALSE(net::decode_zone_links(other));
    // A count over the limit.
    auto big = p;
    big.data[8] = std::byte{0xff};
    big.data[9] = std::byte{0xff};
    big.data[10] = std::byte{0xff};
    CHECK_FALSE(net::decode_zone_links(big));
}

TEST_CASE("Zone routes: only through the zones the server allows") {
    ZoneRoutes r;
    r.set_server({{1, 2}, {2, 4}, {1, 3}, {3, 4}});
    auto no2 = [](std::uint32_t z) { return z != 2; };
    CHECK(r.route(1, 4, no2).zones == std::vector<std::uint32_t>{1, 3, 4});
    CHECK(r.route(1, 2, no2).zones.empty()); // the destination itself
    CHECK(r.route(2, 4, no2).zones == std::vector<std::uint32_t>{2, 4}); // the start may be any
    CHECK(r.set_server_zones({1, 3, 4, 0}, 2));
    CHECK_FALSE(r.set_server_zones({1, 3, 4}, 2));
    CHECK(r.server_zones() == std::set<std::uint32_t>{1, 3, 4});
    ZoneRoutes back = ZoneRoutes::parse(r.serialize());
    CHECK(back.server_zones() == r.server_zones());
    CHECK(back.server_expansion() == 2);
    CHECK(back.serialize() == r.serialize());
}

TEST_CASE("Zone link packets: the allowed zones after the links, and a reply without them") {
    net::ZoneLinks v;
    v.links = {{1, 2}};
    v.has_zones = true;
    v.expansion = -1;
    v.zones = {1, 2, 54};
    auto p = net::encode(v);
    CHECK(p.data.size() == 12 + 4 + 6 + 6);
    auto d = net::decode_zone_links(p);
    REQUIRE(d);
    CHECK(d->has_zones);
    CHECK(d->expansion == -1);
    CHECK(d->zones == v.zones);
    auto cut = p;
    cut.data.resize(cut.data.size() - 1);
    CHECK_FALSE(net::decode_zone_links(cut)); // fewer zones than the count says
    v.has_zones = false;
    auto old = net::decode_zone_links(net::encode(v)); // an older script
    REQUIRE(old);
    CHECK_FALSE(old->has_zones);
}
