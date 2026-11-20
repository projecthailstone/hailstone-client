#pragma once

// Independently authored literal wire fixtures, not a round trip through the decoder's encoder.
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include "core/byte_io.h"
#include "net/protocol/rof2/packets/reward_packets.h"

namespace hailstone::test::reward_fixture {
inline void string(ByteWriter& w, std::string_view text) {
    w.bytes({reinterpret_cast<const std::byte*>(text.data()), text.size()});
    w.le<std::uint8_t>(0);
}
inline void tab(ByteWriter& w, std::uint32_t pending = 42, std::uint32_t set = 700,
                std::uint32_t option = 12, std::uint32_t item = 1000) {
    w.le(pending); w.le(set); string(w, "Fixture choices"); w.f32(1.0f); w.le(set); w.le<std::int32_t>(2);
    w.le<std::uint32_t>(1); w.le<std::uint32_t>(1); w.le<std::uint8_t>(1); w.le<std::uint8_t>(0);
    string(w, "Common");
    w.le<std::uint32_t>(0); w.le<std::uint32_t>(10); w.le<std::uint32_t>(0); string(w, "Shared grant");
    w.le(option); w.le<std::uint32_t>(1); w.le<std::uint8_t>(0); w.le<std::uint8_t>(0);
    string(w, "Choose item");
    w.le<std::uint32_t>(2); w.le<std::uint32_t>(20); w.le<std::uint32_t>(0); string(w, "Fixture reward");
    w.le<std::uint32_t>(1); w.le(item); w.le<std::uint32_t>(2); w.le<std::uint8_t>(1);
    w.le<std::uint32_t>(77); string(w, "Synthetic reward");
}
inline net::AppPacket collection(net::RewardLane lane = net::RewardLane::Pending,
                                 std::uint32_t option = 12, std::uint32_t item = 1000) {
    ByteWriter w; w.le<std::uint32_t>(0); w.le<std::uint8_t>(1); tab(w, 42, 700, option, item);
    return {net::reward_opcode(lane), w.take()};
}
inline net::AppPacket single(net::RewardLane lane, std::uint32_t pending, std::uint32_t set,
                             std::uint32_t option, std::uint32_t item = 1000) {
    ByteWriter w; w.le<std::uint32_t>(0); w.le<std::uint8_t>(1); tab(w, pending, set, option, item);
    return {net::reward_opcode(lane), w.take()};
}
inline net::AppPacket clear(net::RewardLane lane, bool bulk) {
    ByteWriter w; w.le<std::uint32_t>(bulk ? 7 : 0);
    if (bulk) w.le<std::int32_t>(0);
    w.le<std::uint8_t>(0);
    return {net::reward_opcode(lane), w.take()};
}
inline net::AppPacket bulk(bool duplicate_ids = true) {
    ByteWriter w; w.le<std::uint32_t>(7); w.le<std::int32_t>(2); w.le<std::uint8_t>(0);
    tab(w, 42, 700, 12); tab(w, duplicate_ids ? 42u : 43u, duplicate_ids ? 700u : 701u, 13, 1001);
    return {net::kOpPendingRewards, w.take()};
}
inline net::AppPacket claim(bool success, std::uint32_t option = 12) {
    ByteWriter w; w.le<std::uint32_t>(3); w.le<std::uint32_t>(42); w.le<std::uint32_t>(700); w.le(option);
    w.le<std::uint8_t>(success ? 1 : 0); w.le<std::uint8_t>(0); w.le<std::uint8_t>(0); w.le<std::uint8_t>(0);
    return {net::kOpPendingRewards, w.take()};
}
inline net::AppPacket inspection(net::RewardLane lane, const net::Item& item, std::uint32_t header_id = 0) {
    ByteWriter w; w.le<std::uint32_t>(1); w.le(header_id ? header_id : item.id); net::write_item(w, item);
    return {net::reward_opcode(lane), w.take()};
}
inline net::RewardPacket decoded(net::AppPacket packet) {
    auto result = net::decode_reward_packet(packet);
    if (!result) throw std::runtime_error("invalid synthetic reward fixture");
    return std::move(*result);
}
} // namespace hailstone::test::reward_fixture
