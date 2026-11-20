// LDoN adventure packets, byte for byte against EQEmu's structs (common/eq_packet_structs.h; the RoF2 translator
// passes them through) and its senders (zone/client.cpp SendAdventureDetails, zone/worldserver.cpp).

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "core/byte_io.h"
#include "net/protocol/rof2/packets/adventure_packets.h"
#include "net/protocol/rof2/opcode_map.h"

using namespace hailstone;
using namespace hailstone::net;

namespace {

std::uint32_t u32(const AppPacket& p, std::size_t at) {
    ByteReader r(p.data);
    r.seek(at);
    return r.le<std::uint32_t>();
}

} // namespace

TEST_CASE("adventure: the client's requests") {
    AppPacket info = encode_adventure_info_request(0x1234);
    CHECK(info.opcode == rof2::to_wire(rof2::Op::AdventureInfoRequest));
    REQUIRE(info.data.size() == 4); // EntityId_Struct
    CHECK(u32(info, 0) == 0x1234);

    AppPacket r = encode_adventure_request(2, 0x55, 3);
    CHECK(r.opcode == rof2::to_wire(rof2::Op::AdventureRequest));
    REQUIRE(r.data.size() == 12); // AdventureRequest_Struct {risk, entity_id, type}
    CHECK(u32(r, 0) == 2);
    CHECK(u32(r, 4) == 0x55);
    CHECK(u32(r, 8) == 3);

    AppPacket b = encode_ldon_button(true);
    CHECK(b.opcode == rof2::to_wire(rof2::Op::LDoNButton));
    REQUIRE(b.data.size() == 1); // Handle_OP_LDoNButton reads a bool
    CHECK(b.data[0] == std::byte{1});
    CHECK(encode_ldon_button(false).data[0] == std::byte{0});

    AppPacket lb = encode_adventure_leaderboard_request(1, 3, 2);
    REQUIRE(lb.data.size() == 12); // AdventureLeaderboardRequest_Struct {type, theme, risk}
    CHECK(u32(lb, 0) == 1);
    CHECK(u32(lb, 4) == 3);
    CHECK(encode_adventure_stats_request().opcode == 0x5a62);
    CHECK(encode_adventure_stats_request().data == std::vector<std::byte>{std::byte{0}});
}

TEST_CASE("adventure: the recruiter's text ends at its NUL or the packet's end") {
    // SendAdventureError: size strlen + 2, strn0cpy.
    std::string t = "You are not currently assigned to an adventure.";
    AppPacket p(rof2::to_wire(rof2::Op::AdventureInfo), {});
    for (char c : t) p.data.push_back(static_cast<std::byte>(c));
    p.data.push_back(std::byte{0});
    p.data.push_back(std::byte{0});
    auto d = decode_adventure_text(p);
    REQUIRE(d);
    CHECK(*d == t);
    p.data.resize(t.size()); // unterminated: the text up to the packet's end
    d = decode_adventure_text(p);
    REQUIRE(d);
    CHECK(*d == t);
    p.data.clear(); // empty: an empty text
    d = decode_adventure_text(p);
    REQUIRE(d);
    CHECK(d->empty());
}

TEST_CASE("adventure: the current adventure is 2084 bytes; a zeroed one is none") {
    AdventureData v;
    v.assigned = true;
    v.text = "Find the lost scout.";
    v.time_left = 7200;
    v.risk = 2;
    v.x = 10.5f;
    v.compass = true;
    AppPacket p = encode(v);
    REQUIRE(p.data.size() == 2084); // AdventureRequestResponse_Struct
    CHECK(u32(p, 2056) == 7200);
    CHECK(u32(p, 2060) == 2);
    CHECK(u32(p, 2076) == 1);
    auto d = decode_adventure_data(p);
    REQUIRE(d);
    CHECK(d->assigned);
    CHECK(d->text == v.text);
    CHECK(d->time_left == 7200);
    CHECK(d->x == doctest::Approx(10.5f));
    CHECK(d->compass);

    AppPacket none(rof2::to_wire(rof2::Op::AdventureData), std::vector<std::byte>(2084));
    auto n = decode_adventure_data(none);
    REQUIRE(n);
    CHECK_FALSE(n->assigned);

    for (std::size_t size : {0u, 4u, 2052u, 2080u, 2083u}) {
        p.data.resize(size);
        CHECK_FALSE(decode_adventure_data(p));
    }
}

TEST_CASE("adventure: points, stats and the leaderboard") {
    AdventurePoints pts;
    pts.available = 500;
    pts.theme = {1, 2, 3, 4, 5};
    AppPacket pp = encode(pts);
    REQUIRE(pp.data.size() == 140); // AdventurePoints_Update_Struct
    CHECK(u32(pp, 56) == 1);
    CHECK(u32(pp, 76) == 2);
    CHECK(u32(pp, 136) == 5);
    auto dp = decode_adventure_points(pp);
    REQUIRE(dp);
    CHECK(dp->available == 500);
    CHECK(dp->theme[3] == 4);

    AdventureStats st;
    st.success = {10, 1, 2, 3, 4, 0};
    st.rank_percent[5] = 77;
    AppPacket sp = encode(st);
    REQUIRE(sp.data.size() == 96); // AdventureStats_Struct
    CHECK(u32(sp, 0) == 10);
    CHECK(u32(sp, 92) == 77);
    auto ds = decode_adventure_stats(sp);
    REQUIRE(ds);
    CHECK(ds->success[2] == 2);

    AdventureLeaderboard lb;
    lb.success = 3;
    lb.rank = 9;
    lb.rows = {{"First", 50, 1}, {"Second", 40, 10}};
    AppPacket lp = encode(lb);
    REQUIRE(lp.data.size() == 7220); // AdventureLeaderboard_Struct
    CHECK(u32(lp, 16) == 9);
    ByteReader names(lp.data);
    names.seek(20 + 72);
    CHECK(names.fixed_string(64) == "Second");
    auto dl = decode_adventure_leaderboard(lp);
    REQUIRE(dl);
    CHECK(dl->rank == 9);
    REQUIRE(dl->rows.size() == 2);
    CHECK(dl->rows[1].failure == 10);
    CHECK(dl->rows[1].position == 2);
    // The native board counts fixed wire slots even when an earlier name is empty.
    std::fill(lp.data.begin() + 20, lp.data.begin() + 84, std::byte{});
    auto sparse = decode_adventure_leaderboard(lp);
    REQUIRE(sparse);
    REQUIRE(sparse->rows.size() == 1);
    CHECK(sparse->rows.front().position == 2);
    lp.data.resize(7219);
    CHECK_FALSE(decode_adventure_leaderboard(lp));
}

#include "net/protocol/rof2/packets/task_packets.h"

TEST_CASE("shared task: the member list and its changes") {
    std::vector<SharedTaskMember> m{{"Leader", true}, {"Other", false}};
    auto d = decode_shared_task_members(encode(m));
    REQUIRE(d);
    REQUIRE(d->size() == 2);
    CHECK((*d)[0].leader);
    CHECK((*d)[1].name == "Other");
    AppPacket c = encode(SharedTaskMemberChange{true, "New"});
    REQUIRE(c.data.size() == 9 + 4);
    auto dc = decode_shared_task_member_change(c);
    REQUIRE(dc);
    CHECK(dc->added);
    CHECK(dc->name == "New");
}

#include "net/protocol/rof2/packets/aggro_packets.h"

TEST_CASE("aggro meter: target info, updates and the lock") {
    auto t = decode_aggro_target_info(encode(AggroTargetInfo{5, 9}));
    REQUIRE(t);
    CHECK(t->lock_id == 5);
    CHECK(t->target_id == 9);

    // ProcessAggroMeter: u8 1 + u32 secondary, u8 count, {u8 type, u16 pct}.
    AggroUpdate u;
    u.secondary_id = 77;
    u.entries = {{kAggroPlayer, 85}, {kAggroSecondary, 100}};
    AppPacket p = encode(u);
    REQUIRE(p.data.size() == 1 + 4 + 1 + 2 * 3);
    auto d = decode_aggro_update(p);
    REQUIRE(d);
    REQUIRE(d->secondary_id);
    CHECK(*d->secondary_id == 77);
    REQUIRE(d->entries.size() == 2);
    CHECK(d->entries[0].second == 85);

    AggroUpdate none;
    none.entries = {{kAggroGroup1, 10}};
    auto dn = decode_aggro_update(encode(none));
    REQUIRE(dn);
    CHECK_FALSE(dn->secondary_id);
    CHECK(encode_aggro_lock(3).data.size() == 4);
}
