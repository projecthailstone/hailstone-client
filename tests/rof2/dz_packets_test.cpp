// Expedition (dynamic zone) packets, byte for byte against the server's RoF2 translator (common/patches/rof2.cpp
// ENCODE / DECODE OP_Dz*, rof2_structs.h) and its pass-through structs (eq_packet_structs.h).

#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "core/byte_io.h"
#include "net/protocol/rof2/packets/dz_packets.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/wire_io.h"

using namespace hailstone;
using namespace hailstone::net;

namespace {

std::string field(const AppPacket& p, std::size_t at) { return std::string(reinterpret_cast<const char*>(p.data.data() + at)); }
std::uint32_t u32(const AppPacket& p, std::size_t at) {
    std::uint32_t v;
    std::memcpy(&v, p.data.data() + at, 4);
    return v;
}
std::uint16_t u16(const AppPacket& p, std::size_t at) {
    std::uint16_t v;
    std::memcpy(&v, p.data.data() + at, 2);
    return v;
}

} // namespace

TEST_CASE("dz: expedition info is 208 bytes and round-trips") {
    DzInfo v{true, 6, "Deathknell, Tower of Dissonance", "Leader"};
    AppPacket p = encode(v);
    CHECK(p.opcode == rof2::to_wire(rof2::Op::DzExpeditionInfo));
    REQUIRE(p.data.size() == 208);
    CHECK(u32(p, 8) == 1);
    CHECK(u32(p, 12) == 6);
    CHECK(field(p, 16) == v.name);
    CHECK(field(p, 144) == "Leader");
    auto d = decode_dz_info(p);
    REQUIRE(d);
    CHECK(d->assigned);
    CHECK(d->max_players == 6);
    CHECK(d->name == v.name);
    CHECK(d->leader == "Leader");
    p.data.resize(207);
    CHECK_FALSE(decode_dz_info(p));
}

TEST_CASE("dz: an invitation and its answer keep the zone, instance and swap") {
    DzInvite v{"Inviter", "An Expedition", true, "Leaving", 0x1234, 0x0042};
    AppPacket p = encode(v);
    REQUIRE(p.data.size() == 272);
    CHECK(field(p, 8) == "Inviter");
    CHECK(field(p, 72) == "An Expedition");
    CHECK(p.data[200] == std::byte{1});
    CHECK(field(p, 201) == "Leaving");
    CHECK(u16(p, 268) == 0x1234);
    CHECK(u16(p, 270) == 0x0042);
    auto d = decode_dz_invite(p);
    REQUIRE(d);
    CHECK(d->inviter == "Inviter");
    CHECK(d->swapping);
    CHECK(d->swap_name == "Leaving");
    CHECK(d->zone == 0x1234);

    AppPacket r = encode_dz_invite_response(*d, true);
    CHECK(r.opcode == rof2::to_wire(rof2::Op::DzExpeditionInviteResponse));
    REQUIRE(r.data.size() == 80); // DECODE_LENGTH_EXACT(ExpeditionInviteResponse_Struct)
    CHECK(u16(r, 8) == 0x1234);
    CHECK(u16(r, 10) == 0x0042);
    CHECK(r.data[12] == std::byte{1});
    CHECK(r.data[13] == std::byte{1});
    CHECK(field(r, 14) == "Leaving");
    CHECK(encode_dz_invite_response(*d, false).data[12] == std::byte{0});
}

TEST_CASE("dz: lockouts, members and the switch list are counted string lists") {
    std::vector<DzLockout> l{{"Expedition A", 3600, -1, "Replay Timer"}, {"Expedition B", 60, 1, "Boss"}};
    auto dl = decode_dz_lockouts(encode(l));
    REQUIRE(dl);
    REQUIRE(dl->size() == 2);
    CHECK((*dl)[0].event == "Replay Timer");
    CHECK((*dl)[0].event_type == -1);
    CHECK((*dl)[1].seconds == 60);

    std::vector<DzMember> m{{"Alpha", DzMemberStatus::Online}, {"Beta", DzMemberStatus::InDynamicZone}};
    AppPacket mp = encode(m);
    CHECK(mp.data.size() == 8 + 6 + 1 + 5 + 1);
    auto dm = decode_dz_members(mp);
    REQUIRE(dm);
    CHECK((*dm)[1].name == "Beta");
    CHECK((*dm)[1].status == DzMemberStatus::InDynamicZone);
    CHECK(encode(m, true).opcode == rof2::to_wire(rof2::Op::DzMemberListStatus));

    std::vector<DzChoice> c{{10, 20, 40, 1, 0x140010, "Somewhere", "Leader"}};
    auto dc = decode_dz_choose_zone(encode(c));
    REQUIRE(dc);
    REQUIRE(dc->size() == 1);
    CHECK((*dc)[0].description == "Somewhere");
    CHECK((*dc)[0].id2 == 0x140010);

    // A count past the data fails rather than reading on.
    AppPacket bad = encode(m);
    bad.data[4] = std::byte{3};
    CHECK_FALSE(decode_dz_members(bad));
}

TEST_CASE("dz: a member list longer than kDzMembersMax fails the decode") {
    // Millions of 2-byte members fit in a fragmented packet; each status update would match every name against the
    // list. An expedition is a raid at most.
    std::vector<DzMember> many(kDzMembersMax);
    for (std::size_t i = 0; i < many.size(); ++i) many[i].name = "m" + std::to_string(i);
    auto ok = decode_dz_members(encode(many, true));
    REQUIRE(ok);
    CHECK(ok->size() == kDzMembersMax);
    many.push_back({"one_more", DzMemberStatus::Online});
    CHECK_FALSE(decode_dz_members(encode(many, true)));
    ByteWriter w;
    w.le<std::uint32_t>(0);
    w.le<std::uint32_t>(0x00200000u);
    for (int i = 0; i < 0x00200000; ++i) {
        w.le<std::uint8_t>(0); // an empty name
        w.le<std::uint8_t>(1);
    }
    CHECK_FALSE(decode_dz_members(make_packet(rof2::Op::DzMemberListStatus, std::move(w))));
}

TEST_CASE("dz: the choose-zone reply is 52 bytes with the entry's ids") {
    DzChoice c{10, 20, 40, 1, 77, "x", "y"};
    AppPacket p = encode_dz_choose_zone_reply(c);
    CHECK(p.opcode == rof2::to_wire(rof2::Op::DzChooseZoneReply));
    REQUIRE(p.data.size() == 52); // DECODE_LENGTH_EXACT(DynamicZoneChooseZoneReply_Struct)
    CHECK(u32(p, 12) == 40);
    CHECK(u16(p, 16) == 10);
    CHECK(u16(p, 18) == 20);
    CHECK(u32(p, 20) == 1);
    CHECK(u32(p, 24) == 77);
}

TEST_CASE("dz: the leader's commands and the small notices") {
    AppPacket a = encode_dz_add_player("Somebody");
    CHECK(a.opcode == rof2::to_wire(rof2::Op::DzAddPlayer));
    REQUIRE(a.data.size() == 72); // ExpeditionCommand_Struct
    CHECK(field(a, 8) == "Somebody");
    CHECK(encode_dz_remove_player("x").opcode == rof2::to_wire(rof2::Op::DzRemovePlayer));
    CHECK(encode_dz_make_leader("x").opcode == rof2::to_wire(rof2::Op::DzMakeLeader));
    // A 64-character name stays NUL-terminated inside its field.
    AppPacket longname = encode_dz_add_player(std::string(80, 'a'));
    CHECK(field(longname, 8).size() == 63);

    AppPacket s = encode_dz_swap_player("In", "Out");
    REQUIRE(s.data.size() == 136); // ExpeditionCommandSwap_Struct
    CHECK(field(s, 8) == "In");
    CHECK(field(s, 72) == "Out");

    CHECK(encode_dz_quit().data.size() == 8);
    CHECK(encode_dz_player_list().data.size() == 8);
    CHECK(encode_dz_list_timers().opcode == rof2::to_wire(rof2::Op::DzListTimers));

    auto leader = decode_dz_leader(encode_dz_leader("NewLeader"));
    REQUIRE(leader);
    CHECK(*leader == "NewLeader");
    auto warn = decode_dz_ends_warning(encode_dz_ends_warning(5));
    REQUIRE(warn);
    CHECK(*warn == 5);
    auto name = decode_dz_member_name(encode(DzMemberName{true, "Gamma"}));
    REQUIRE(name);
    CHECK(name->add);
    CHECK(name->name == "Gamma");
}

TEST_CASE("dz: compass points are 24-byte entries in zone-file order") {
    std::vector<DzCompassPoint> v{{1, {10.0f, -20.0f, 3.0f}}};
    AppPacket p = encode(v);
    REQUIRE(p.data.size() == 8 + 24);
    float y = 0;
    std::memcpy(&y, p.data.data() + 8 + 12, 4); // the server's y first: the zone file's x
    CHECK(y == doctest::Approx(10.0f));
    auto d = decode_dz_compass(p);
    REQUIRE(d);
    REQUIRE(d->size() == 1);
    CHECK((*d)[0].type == 1);
    CHECK((*d)[0].at.y == doctest::Approx(-20.0f));
    p.data[4] = std::byte{9};
    CHECK_FALSE(decode_dz_compass(p));
}
