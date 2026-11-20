// Guild bank packets, byte for byte against EQEmu (zone/guild_mgr.cpp SendGuildBank's RoF+ list,
// common/patches/rof2.cpp ENCODE / DECODE OP_GuildBank, rof2_structs.h GuildBank*_Struct).

#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "net/protocol/rof2/packets/guild_bank_packets.h"
#include "net/protocol/rof2/opcode_map.h"

using namespace hailstone;
using namespace hailstone::net;

namespace {

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

TEST_CASE("guild bank: the item list is 40 deposit then 200 main entries") {
    GuildBankList v;
    v.deposit.resize(kGuildBankDepositSlots);
    v.main.resize(kGuildBankMainSlots);
    v.deposit[3] = GuildBankItem{GuildBankPermission::BankerOnly, "", "Giver", 1001, 500, 1, false, true, "Short Sword"};
    v.main[0] = GuildBankItem{GuildBankPermission::SingleMember, "Taker", "Giver", 13005, 777, 20, true, false, "Iron Ration"};
    AppPacket p = encode(v);
    CHECK(p.opcode == rof2::to_wire(rof2::Op::GuildBankItemList));
    auto d = decode_guild_bank_list(p);
    REQUIRE(d);
    REQUIRE(d->deposit.size() == 40);
    REQUIRE(d->main.size() == 200);
    REQUIRE(d->deposit[3]);
    CHECK(d->deposit[3]->name == "Short Sword");
    CHECK(d->deposit[3]->donator == "Giver");
    CHECK_FALSE(d->deposit[2]);
    REQUIRE(d->main[0]);
    CHECK(d->main[0]->permission == GuildBankPermission::SingleMember);
    CHECK(d->main[0]->who_for == "Taker");
    CHECK(d->main[0]->quantity == 20);
    CHECK(d->main[0]->allow_merge);
    CHECK_FALSE(d->main[0]->usable);

    // A list cut short leaves the rest empty; a cut inside an entry fails.
    AppPacket shorter(rof2::to_wire(rof2::Op::GuildBankItemList), std::vector<std::byte>(5));
    auto s = decode_guild_bank_list(shorter);
    REQUIRE(s);
    CHECK(s->main.size() == 200);
    p.data.resize(10);
    CHECK_FALSE(decode_guild_bank_list(p));
}

TEST_CASE("guild bank: the server's updates and acknowledgements in RoF2's actions") {
    GuildBankMessage u;
    u.kind = GuildBankMessage::Update;
    u.slot = 7;
    u.area = GuildBankArea::Main;
    u.item = GuildBankItem{GuildBankPermission::Public, "", "Giver", 5, 6, 3, true, true, "Thing"};
    AppPacket p = encode(u);
    REQUIRE(p.data.size() == 230); // the emu struct + 4 (ENCODE OP_GuildBank)
    CHECK(u32(p, 0) == 0);         // GuildBankItemUpdate (1) less one
    CHECK(u16(p, 12) == 7);
    CHECK(u16(p, 14) == 1);
    CHECK(u32(p, 16) == 1);
    CHECK(u32(p, 32) == 3);
    CHECK(std::string(reinterpret_cast<const char*>(p.data.data() + 38)) == "Thing");
    CHECK(std::string(reinterpret_cast<const char*>(p.data.data() + 102)) == "Giver");
    auto d = decode_guild_bank(p);
    REQUIRE(d);
    CHECK(d->kind == GuildBankMessage::Update);
    CHECK(d->slot == 7);
    REQUIRE(d->item);
    CHECK(d->item->permission == GuildBankPermission::Public);

    u.item.reset(); // shown 0: the slot empties
    auto e = decode_guild_bank(encode(u));
    REQUIRE(e);
    CHECK_FALSE(e->item);

    GuildBankMessage ack;
    ack.kind = GuildBankMessage::Ack;
    AppPacket a = encode(ack);
    REQUIRE(a.data.size() == 12);
    CHECK(u32(a, 0) == 9);
    CHECK(decode_guild_bank(a)->kind == GuildBankMessage::Ack);

    GuildBankMessage dep;
    dep.kind = GuildBankMessage::DepositAck;
    dep.slot = 1; // EQEmu's failed flag lands in the slot word
    AppPacket da = encode(dep);
    REQUIRE(da.data.size() == 16);
    CHECK(u32(da, 0) == 4);
    auto dd = decode_guild_bank(da);
    REQUIRE(dd);
    CHECK(dd->kind == GuildBankMessage::DepositAck);
    CHECK(dd->key() == guild_bank_key(GuildBankArea::Deposit, 1));
}

TEST_CASE("guild bank: the client's requests as CGuildBankWnd sends them, in DECODE's exact sizes") {
    AppPacket pr = encode_guild_bank_item(GuildBankAction::Promote, 1234, guild_bank_key(GuildBankArea::Deposit, 4), 13005, 0);
    CHECK(pr.opcode == rof2::to_wire(rof2::Op::GuildBank));
    REQUIRE(pr.data.size() == 24); // GuildBankPromote_Struct
    CHECK(u32(pr, 0) == 2);
    CHECK(u32(pr, 4) == 1234); // the banker
    CHECK(u32(pr, 12) == 4);   // EQEmu's Slot
    CHECK(u32(pr, 16) == 13005);

    AppPacket v = encode_guild_bank_item(GuildBankAction::View, 1234, guild_bank_key(GuildBankArea::Main, 9), 13005, 1);
    REQUIRE(v.data.size() == 24); // GuildBankViewItem_Struct
    CHECK(u32(v, 0) == 3);
    CHECK(u16(v, 12) == 9);
    CHECK(u16(v, 14) == 1);
    CHECK(u32(v, 20) == 1);

    AppPacket d = encode_guild_bank_deposit(1234);
    REQUIRE(d.data.size() == 16);
    CHECK(u32(d, 0) == 4);
    CHECK(u32(d, 4) == 1234);

    AppPacket pm = encode_guild_bank_permissions(1234, guild_bank_key(GuildBankArea::Main, 11), 13005,
                                                 GuildBankPermission::SingleMember, "Member");
    REQUIRE(pm.data.size() == 88); // GuildBankPermissions_Struct
    CHECK(u32(pm, 0) == 5);
    CHECK(u16(pm, 12) == 11);
    CHECK(u16(pm, 14) == 1);
    CHECK(u32(pm, 16) == 13005);
    CHECK(u32(pm, 20) == 1);
    CHECK(std::string(reinterpret_cast<const char*>(pm.data.data() + 24)) == "Member");

    AppPacket w = encode_guild_bank_item(GuildBankAction::Withdraw, 1234, guild_bank_key(GuildBankArea::Deposit, 2), 13005, 5);
    REQUIRE(w.data.size() == 24); // GuildBankWithdrawItem_Struct
    CHECK(u32(w, 0) == 6);
    CHECK(u16(w, 12) == 2);
    CHECK(u16(w, 14) == 0);
    CHECK(u32(w, 20) == 5);
    CHECK(u32(encode_guild_bank_item(GuildBankAction::Split, 1, 1, 2, 3), 0) == 7);
    CHECK(u32(encode_guild_bank_item(GuildBankAction::Merge, 1, 1, 2, 0), 0) == 8);
}
