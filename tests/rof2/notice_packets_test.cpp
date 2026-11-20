// Zone notices and offer answers (net/protocol/rof2/packets/notice_packets.h), against EQEmu's structs (common/eq_packet_structs.h; RoF2
// rof2_structs.h for Stun, Resurrect, Disciplines and BindWound).

#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/rof2/opcode_map.h"

using namespace hailstone;
using namespace hailstone::net;

namespace {

AppPacket bytes(rof2::Op op, std::size_t n) { return AppPacket(rof2::to_wire(op), std::vector<std::byte>(n)); }
void put32(AppPacket& p, std::size_t at, std::uint32_t v) { std::memcpy(p.data.data() + at, &v, 4); }
void put16(AppPacket& p, std::size_t at, std::uint16_t v) { std::memcpy(p.data.data() + at, &v, 2); }
void puts(AppPacket& p, std::size_t at, const std::string& s) { std::memcpy(p.data.data() + at, s.data(), s.size()); }
std::uint32_t get32(const AppPacket& p, std::size_t at) {
    std::uint32_t v;
    std::memcpy(&v, p.data.data() + at, 4);
    return v;
}

} // namespace

TEST_CASE("notices: stun, recast, charm, untargetable, yell, disciplines") {
    AppPacket s = bytes(rof2::Op::Stun, 6);
    put32(s, 0, 2500);
    auto ds = decode_stun(s);
    REQUIRE(ds);
    CHECK(ds->duration_ms == 2500);
    CHECK_FALSE(ds->lean);
    s.data[4] = std::byte{1};
    CHECK(decode_stun(s)->lean);
    // EQEmu's 4-byte Stun_Struct (an older patch): no lean byte.
    AppPacket s4 = bytes(rof2::Op::Stun, 4);
    put32(s4, 0, 1000);
    CHECK(decode_stun(s4)->duration_ms == 1000);
    CHECK_FALSE(decode_stun(s4)->lean);

    AppPacket r = bytes(rof2::Op::ItemRecastDelay, 12);
    put32(r, 0, 30);
    put32(r, 4, 7);
    auto dr = decode_item_recast_delay(r);
    REQUIRE(dr);
    CHECK(dr->delay_s == 30);
    CHECK(dr->type == 7);
    CHECK_FALSE(dr->ignore_casting);
    r.data[8] = std::byte{1};
    CHECK(decode_item_recast_delay(r)->ignore_casting);

    AppPacket c = bytes(rof2::Op::Charm, 12);
    put32(c, 0, 1);
    put32(c, 4, 99);
    put32(c, 8, 1);
    auto dc = decode_charm(c);
    REQUIRE(dc);
    CHECK(dc->pet == 99);
    CHECK(dc->on);

    AppPacket u = bytes(rof2::Op::Untargetable, 8);
    put32(u, 0, 5);
    auto du = decode_untargetable(u);
    REQUIRE(du);
    CHECK(du->spawn == 5);
    CHECK_FALSE(du->targetable);

    AppPacket y = bytes(rof2::Op::YellForHelp, 4);
    put32(y, 0, 12);
    CHECK(decode_yell_for_help(y).value_or(0) == 12);

    AppPacket d = bytes(rof2::Op::DisciplineUpdate, 1200); // 300 × u32
    put32(d, 8, 4567);
    auto dd = decode_disciplines(d);
    REQUIRE(dd);
    REQUIRE(dd->size() == 300);
    CHECK((*dd)[2] == 4567);
    CHECK_FALSE(decode_stun(bytes(rof2::Op::Stun, 2)));
}

TEST_CASE("offers: translocate, resurrection and shared task answers") {
    AppPacket t = bytes(rof2::Op::Translocate, 92);
    put32(t, 0, 202);
    puts(t, 12, "The Magic Map");
    auto dt = decode_translocate(t);
    REQUIRE(dt);
    CHECK(dt->zone == 202);
    CHECK(dt->caster == "The Magic Map");
    AppPacket ta = answer_translocate(t, true);
    CHECK(ta.opcode == rof2::to_wire(rof2::Op::Translocate));
    CHECK(get32(ta, 88) == 1);
    CHECK(get32(answer_translocate(t, false), 88) == 0);

    AppPacket z = bytes(rof2::Op::RezzRequest, 236);
    put16(z, 4, 9);
    puts(z, 92, "Healer");
    put32(z, 156, 2050);
    puts(z, 160, "Me's corpse");
    auto dz = decode_rezz_request(z);
    REQUIRE(dz);
    CHECK(dz->zone == 9);
    CHECK(dz->rezzer == "Healer");
    CHECK(dz->spell == 2050);
    CHECK(dz->corpse == "Me's corpse");
    AppPacket za = answer_rezz(z, true);
    CHECK(za.opcode == rof2::to_wire(rof2::Op::RezzAnswer));
    REQUIRE(za.data.size() == 236); // DECODE_LENGTH_EXACT(Resurrect_Struct)
    CHECK(get32(za, 224) == 1);

    AppPacket i = bytes(rof2::Op::SharedTaskInvite, 136);
    put32(i, 4, 77);
    puts(i, 8, "A Task");
    puts(i, 72, "Leader");
    auto di = decode_shared_task_invite(i);
    REQUIRE(di);
    CHECK(di->invite_id == 77);
    CHECK(di->task == "A Task");
    CHECK(di->inviter == "Leader");
    AppPacket ia = encode_shared_task_invite_response(77, true);
    REQUIRE(ia.data.size() == 12);
    CHECK(get32(ia, 4) == 77);
    CHECK(ia.data[8] == std::byte{1});
}

TEST_CASE("notices: group roles, guild roster changes, coin finds, bind wound") {
    AppPacket g = bytes(rof2::Op::GroupRoles, 148);
    puts(g, 0, "Tank");
    puts(g, 64, "Leader");
    put32(g, 0x8c, 1);
    g.data[0x90] = std::byte{1};
    auto dg = decode_group_role(g);
    REQUIRE(dg);
    CHECK(dg->member == "Tank");
    CHECK(dg->leader == "Leader");
    CHECK(dg->role == 1);
    CHECK(dg->on == 1);

    AppPacket add = bytes(rof2::Op::GuildMemberAdd, 104);
    put32(add, 0, 3);
    put32(add, 16, 60);
    put32(add, 20, 2);
    put32(add, 24, 5);
    put32(add, 32, 22);
    puts(add, 40, "Newbie");
    auto da = decode_guild_roster_change(add);
    REQUIRE(da);
    CHECK(da->kind == GuildRosterChange::Add);
    CHECK(da->guild_id == 3);
    CHECK(da->name == "Newbie");
    CHECK(da->level == 60);
    CHECK(da->rank == 5);
    CHECK(da->zone_id == 22);

    AppPacket rank = bytes(rof2::Op::SetGuildRank, 80); // the wire value OP_GuildMemberRankAltBanker shares
    put32(rank, 4, 2);
    puts(rank, 8, "Officer");
    put32(rank, 72, 3);
    auto dk = decode_guild_roster_change(rank);
    REQUIRE(dk);
    CHECK(dk->kind == GuildRosterChange::Rank);
    CHECK(dk->name == "Officer");
    CHECK(dk->alt);
    CHECK(dk->banker);

    AppPacket note = bytes(rof2::Op::GuildMemberPublicNote, 324);
    puts(note, 4, "Someone");
    puts(note, 68, "hello");
    auto dn = decode_guild_roster_change(note);
    REQUIRE(dn);
    CHECK(dn->note == "hello");

    AppPacket beg = bytes(rof2::Op::Begging, 20);
    put32(beg, 12, 4);
    put32(beg, 16, 7);
    auto db = decode_begging(beg);
    REQUIRE(db);
    CHECK(db->kind == 4);
    CHECK(db->amount == 7);

    AppPacket bw = bytes(rof2::Op::Bind_Wound, 8);
    put16(bw, 4, 1);
    CHECK(decode_bind_wound(bw).value_or(0) == 1);
}

TEST_CASE("OP_Weblink: the address up to its NUL; none past the end; empty is nothing") {
    AppPacket w = bytes(rof2::Op::Weblink, 24);
    puts(w, 0, "https://example.org/a");
    CHECK(decode_weblink(w).value_or("") == "https://example.org/a");
    AppPacket open = bytes(rof2::Op::Weblink, 0);
    for (char c : std::string("http://x")) open.data.push_back(static_cast<std::byte>(c));
    CHECK(decode_weblink(open).value_or("") == "http://x"); // no NUL: the packet's end
    CHECK_FALSE(decode_weblink(bytes(rof2::Op::Weblink, 4)));
    CHECK_FALSE(decode_weblink(bytes(rof2::Op::Weblink, 0)));
}
