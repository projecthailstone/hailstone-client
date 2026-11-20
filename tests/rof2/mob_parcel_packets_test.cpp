// EQEmu common/eq_packet_structs.h: MobRename_Struct and ParcelIcon_Struct.
// Wire values from the stock patch_RoF2.conf; no server-specific extensions.
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "core/byte_io.h"
#include "core/game_dir.h"
#include "game/world_state.h"
#include "net/connection/game_connection.h"
#include "net/messages/message.h"
#include "net/protocol/rof2/codec.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/packets/merchant_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

using namespace hailstone;
using namespace hailstone::net;
namespace msg = hailstone::net::messages;

namespace {
AppPacket mob_rename(std::string_view old_name, std::string_view new_name) {
    ByteWriter w;
    w.fixed_string(old_name, 64);
    w.fixed_string(old_name, 64);
    w.fixed_string(new_name, 64);
    w.le<std::uint32_t>(0);
    w.le<std::uint32_t>(1);
    return AppPacket(rof2::to_wire(rof2::Op::MobRename), w.take());
}

AppPacket parcel_icon(std::uint32_t status) {
    ByteWriter w;
    w.le(status);
    return AppPacket(rof2::to_wire(rof2::Op::ShopParcelIcon), w.take());
}
} // namespace

TEST_CASE("RoF2 OP_MobRename decodes its three fixed names and rejects malformed records") {
    CHECK(rof2::to_wire(rof2::Op::MobRename) == 0x2c57);
    rof2::Codec codec;
    CHECK(codec.supports(msg::ServiceScope::Zone, msg::GameOpcode::MobRename,
                         protocol::CodecDirection::Inbound));
    CHECK_FALSE(codec.supports(msg::ServiceScope::World, msg::GameOpcode::MobRename,
                               protocol::CodecDirection::Inbound));
    const AppPacket packet = mob_rename("a_rat00", "a_rat01");
    REQUIRE(packet.data.size() == kMobRenameSize);
    REQUIRE(decode_mob_rename(packet));
    CHECK(decode_mob_rename(packet)->old_name == "a_rat00");
    CHECK(decode_mob_rename(packet)->new_name == "a_rat01");

    const auto decoded = codec.decode(msg::ServiceScope::Zone, packet);
    REQUIRE(decoded);
    REQUIRE(decoded.value()->size() == 1);
    CHECK(decoded.value()->front().opcode == msg::GameOpcode::MobRename);
    const auto* rename = msg::message_get<MobRename>(decoded.value()->front());
    REQUIRE(rename);
    CHECK(rename->old_name == "a_rat00");
    CHECK(rename->new_name == "a_rat01");

    AppPacket short_record = packet;
    short_record.data.pop_back();
    CHECK_FALSE(codec.decode(msg::ServiceScope::Zone, short_record));
    AppPacket long_record = packet;
    long_record.data.push_back(std::byte{0});
    CHECK_FALSE(codec.decode(msg::ServiceScope::Zone, long_record));
    AppPacket no_terminator = packet;
    for (std::size_t i = 0; i < 64; ++i) no_terminator.data[i] = std::byte{'A'};
    CHECK_FALSE(codec.decode(msg::ServiceScope::Zone, no_terminator));
    CHECK_FALSE(codec.decode(msg::ServiceScope::Zone, mob_rename("", "new")));
    CHECK_FALSE(codec.decode(msg::ServiceScope::Zone, mob_rename("old", "")));
}

TEST_CASE("RoF2 mob rename changes the actor name without retiring its identity or target") {
    GameDir files;
    GameConnection conn(files);
    auto& world = conn.world_for_test();
    game::Spawn rat;
    rat.id = 77;
    rat.name = "a_rat00";
    rat.incarnation = 42;
    world.spawns.emplace(rat.id, rat);
    world.target_id = 77;
    const auto generation = world.spawn_set_serial;
    const auto decoded = rof2::Codec{}.decode(msg::ServiceScope::Zone, mob_rename("a_rat00", "a_rat01"));
    REQUIRE(decoded);
    REQUIRE(decoded.value()->size() == 1);
    conn.receive_zone_message_for_test(decoded.value()->front());
    REQUIRE(world.spawns.contains(77));
    CHECK(world.spawns.at(77).name == "a_rat01");
    CHECK(world.spawns.at(77).incarnation == 42);
    CHECK(world.target_id == 77);
    CHECK(world.spawn_set_serial == generation + 1);
    // A repeated old-name notification cannot rename an unrelated actor.
    conn.receive_zone_message_for_test(decoded.value()->front());
    CHECK(world.spawn_set_serial == generation + 1);
    CHECK(world.spawns.size() == 1);
}

TEST_CASE("RoF2 OP_ShopParcelIcon decodes all statuses and rejects invalid values") {
    CHECK(rof2::to_wire(rof2::Op::ShopParcelIcon) == 0x46f0);
    rof2::Codec codec;
    CHECK(codec.supports(msg::ServiceScope::Zone, msg::GameOpcode::ShopParcelIcon,
                         protocol::CodecDirection::Inbound));
    CHECK_FALSE(codec.supports(msg::ServiceScope::World, msg::GameOpcode::ShopParcelIcon,
                               protocol::CodecDirection::Inbound));
    for (std::uint32_t status = 0; status <= 2; ++status) {
        auto packet = parcel_icon(status);
        REQUIRE(packet.data.size() == kParcelIconSize);
        auto decoded = codec.decode(msg::ServiceScope::Zone, packet);
        REQUIRE(decoded);
        REQUIRE(decoded.value()->size() == 1);
        CHECK(decoded.value()->front().opcode == msg::GameOpcode::ShopParcelIcon);
        const auto* icon = msg::message_get<ParcelIconStatus>(decoded.value()->front());
        REQUIRE(icon);
        CHECK(static_cast<std::uint32_t>(*icon) == status);
    }
    CHECK_FALSE(codec.decode(msg::ServiceScope::Zone, parcel_icon(3)));
    CHECK_FALSE(codec.decode(msg::ServiceScope::Zone, parcel_icon(0xffffffffu)));
    AppPacket short_record = parcel_icon(1);
    short_record.data.pop_back();
    CHECK_FALSE(codec.decode(msg::ServiceScope::Zone, short_record));
    AppPacket long_record = parcel_icon(1);
    long_record.data.push_back(std::byte{0});
    CHECK_FALSE(codec.decode(msg::ServiceScope::Zone, long_record));
}

TEST_CASE("RoF2 parcel notification follows server updates and retires at disconnect") {
    GameDir files;
    GameConnection conn(files);
    CHECK_FALSE(conn.parcel_icon_status().has_value());
    rof2::Codec codec;
    for (const auto status : {ParcelIconStatus::On, ParcelIconStatus::OverLimit, ParcelIconStatus::Off}) {
        const auto decoded = codec.decode(msg::ServiceScope::Zone,
                                          parcel_icon(static_cast<std::uint32_t>(status)));
        REQUIRE(decoded);
        REQUIRE(decoded.value()->size() == 1);
        conn.receive_zone_message_for_test(decoded.value()->front());
        REQUIRE(conn.parcel_icon_status());
        CHECK(*conn.parcel_icon_status() == status);
        CHECK_FALSE(conn.merchant()); // icon does not require a merchant to be open
    }
    conn.disconnect();
    CHECK_FALSE(conn.parcel_icon_status().has_value());
}
