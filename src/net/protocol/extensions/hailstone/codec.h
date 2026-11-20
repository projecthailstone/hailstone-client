#pragma once
// Optional server-script codec. Never part of stock RoF2 opcode translation.
#include "net/protocol/codec.h"
namespace hailstone::net::hailstone_extension {
class Codec final : public protocol::IGameCodec {
public:
    protocol::CodecCapabilities capabilities() const noexcept override { return {false, true}; }
    bool supports(messages::ServiceScope scope, messages::GameOpcode opcode,
                  protocol::CodecDirection direction) const override;
    messages::CodecResult<protocol::WirePacketBatch> encode(
        messages::ServiceScope scope, const messages::GameMessage& message) const override;
    messages::CodecResult<messages::MessageBatch> decode(
        messages::ServiceScope scope, const AppPacket& packet) const override;
};
}
