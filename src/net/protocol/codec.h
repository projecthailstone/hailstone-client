#pragma once

// Application codec boundary. No sockets, SOE framing, or connection lifecycle:
// one semantic opcode with a typed payload becomes zero or more wire app
// packets, and a wire app packet becomes zero or more semantic messages.
// Protocol-specific wire opcodes and payload layouts never escape this layer.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "net/messages/message.h"
#include "net/messages/item.h"
#include "net/messages/result.h"
#include "net/wire/app_packet.h"

namespace hailstone::net::protocol {

using messages::CodecResult;
using messages::GameMessage;
using messages::GameOpcode;
using messages::MessageBatch;
using messages::ServiceScope;
using WirePacketBatch = std::vector<AppPacket>;

enum class CodecDirection : std::uint8_t { Outbound, Inbound };

struct CodecCapabilities {
    bool world = false;
    bool zone = false;
    // No claim of opcode support follows merely from a wire opcode existing.
    // An actual serializer/deserializer must exist for a message to be supported.
    bool accepts(ServiceScope scope) const {
        return (scope == ServiceScope::World && world) ||
               (scope == ServiceScope::Zone && zone);
    }
};

class IGameCodec {
public:
    virtual ~IGameCodec() = default;
    virtual CodecCapabilities capabilities() const noexcept = 0;
    virtual bool supports(ServiceScope scope, GameOpcode opcode, CodecDirection direction) const = 0;
    virtual CodecResult<WirePacketBatch> encode(ServiceScope scope, const GameMessage& message) const = 0;
    virtual CodecResult<MessageBatch> decode(ServiceScope scope, const AppPacket& packet) const = 0;
    // Identify only wire opcodes with one unambiguous semantic meaning.
    // Ambiguous mappings are reported without guessing the payload.
    virtual std::optional<GameOpcode> identify_wire(ServiceScope, std::uint16_t) const { return std::nullopt; }
    // Encoding is side-effect free: a one-use reply is consumed only after the
    // complete encoded command is admitted to the session.  The selected codec
    // receives this notification (not every installed extension).
    virtual void on_send_admitted(ServiceScope, const GameMessage&) {}

    // A new player lifetime invalidates codec-owned offers and echo tokens.
    virtual void invalidate_transient_requests() {}

    // Optional diagnostic capability: decode an item instance from a wire
    // payload supplied by a native plugin. This does not route game commands,
    // and a codec without this capability returns no item.
    virtual std::optional<Item> inspect_item_wire(std::span<const std::byte>) const { return std::nullopt; }
};

} // namespace hailstone::net::protocol
