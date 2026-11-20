#pragma once

#include "net/protocol/codec.h"
#include <optional>
#include <map>
#include <cstdint>
#include "net/messages/achievement.h"

namespace hailstone::net::rof2 {

// RoF2's world/zone packet translator. Other protocol families provide their
// own implementations; no RoF2 type appears in the generic connection API.
class Codec final : public protocol::IGameCodec {
public:
private:
    // Per-zone decode schema; snapshots are decoded here, never passed as packet bytes.
    // Keep the previous schema for late comparison/inspection responses across refresh.
    mutable std::optional<AchievementCatalog> achievement_catalog_;
    mutable std::optional<AchievementCatalog> previous_achievement_catalog_;
    // RoF2 echo replies must repeat the original, including fields unknown to the
    // gameplay model. Only an opaque, per-codec token escapes to GameConnection.
    mutable std::map<std::uint64_t, AppPacket> zone_offers_;
    mutable std::uint64_t next_offer_token_ = 1;
public:
    protocol::CodecCapabilities capabilities() const noexcept override { return {true, true}; }
    bool supports(messages::ServiceScope, messages::GameOpcode, protocol::CodecDirection) const override;
    messages::CodecResult<protocol::WirePacketBatch> encode(messages::ServiceScope, const messages::GameMessage&) const override;
    messages::CodecResult<messages::MessageBatch> decode(messages::ServiceScope, const AppPacket&) const override;
    std::optional<messages::GameOpcode> identify_wire(messages::ServiceScope, std::uint16_t) const override;
    std::optional<Item> inspect_item_wire(std::span<const std::byte> bytes) const override;
    void on_send_admitted(messages::ServiceScope, const messages::GameMessage&) override;
    void invalidate_transient_requests() override { zone_offers_.clear(); }
};

} // namespace hailstone::net::rof2
