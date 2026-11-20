#include <doctest/doctest.h>

#include "net/messages/message.h"
#include "net/protocol/rof2/codec.h"
#include "net/protocol/rof2/codec_dispatch.h"
#include "net/protocol/rof2/codec_mapping.h"

namespace hm = hailstone::net::messages;
namespace detail = hailstone::net::rof2::detail;

TEST_CASE("RoF2 semantic map has one shared source of wire identities") {
    const auto first = detail::mappings();
    const auto second = detail::mappings();
    CHECK_FALSE(first.empty());
    CHECK(first.data() == second.data());
    CHECK(first.size() == second.size());
}

TEST_CASE("RoF2 domain dispatch rejects an incorrect semantic payload") {
    // Invalid semantic commands must not turn into wire packets, even if an
    // unrelated domain's encoder happens to receive the same command.
    const hm::GameMessage bad_item{hm::GameOpcode::MoveItem, std::monostate{}};
    CHECK_FALSE(detail::encode_items(bad_item.opcode, bad_item).has_value());
    CHECK_FALSE(detail::make_packet(bad_item.opcode, bad_item).has_value());
    hailstone::net::rof2::Codec codec;
    const auto result = codec.encode(hm::ServiceScope::Zone, bad_item);
    CHECK_FALSE(result.ok());
}

TEST_CASE("RoF2 domain decode rejects missing wire data") {
    const hailstone::net::AppPacket empty{};
    CHECK_FALSE(detail::decode_world_zone(hm::GameOpcode::NewZone, empty).has_value());
    CHECK_FALSE(detail::parse_packet(hm::GameOpcode::NewZone, empty).has_value());
}
