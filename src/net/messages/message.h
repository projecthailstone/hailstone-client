#pragma once

#include <cstdint>
#include <utility>
#include "net/messages/payload.h"
#include <vector>

#include "net/messages/opcode.h"

namespace hailstone::net::messages {

// The envelope owns its semantic payload. The erased storage is deliberately
// independent of every gameplay type: adding an opcode no longer instantiates
// a global std::variant in each consumer of the codec/endpoint interfaces.
struct GameMessage {
    GameOpcode opcode = GameOpcode::Unknown;
    Payload value{};
};

// Payload type checks happen at the accessor boundary; codec-specific opcode
// validation remains in the owning serializer (including polymorphic opcodes).
template<class T> const T* message_get(const GameMessage& message) noexcept {
    return message.value.get_if<T>();
}
template<class T> T* message_get(GameMessage& message) noexcept {
    return message.value.get_if<T>();
}
template<class T> bool message_is(const GameMessage& message) noexcept {
    return message_get<T>(message) != nullptr;
}
// For handlers where one opcode has multiple possible semantic payloads, the
// selected variant must match *both* the opcode and the stored value type.
template<class T> const T* message_get(const GameMessage& message,
                                       GameOpcode expected) noexcept {
    return message.opcode == expected ? message_get<T>(message) : nullptr;
}
template<class T> T* message_get(GameMessage& message,
                                 GameOpcode expected) noexcept {
    return message.opcode == expected ? message_get<T>(message) : nullptr;
}

using MessageBatch = std::vector<GameMessage>;

enum class Reliability : std::uint8_t {
    ReliableOrdered,
    Unreliable,
};

struct DeliveryPolicy {
    Reliability reliability = Reliability::ReliableOrdered;
    bool allow_coalescing = true;
};

struct OutgoingGameMessage {
    GameMessage message;
    DeliveryPolicy delivery{};
};

// Express an outgoing command without constructing a transport packet. Codec
// selection and the message/payload contract are enforced at the endpoint.
inline OutgoingGameMessage outgoing(GameOpcode opcode, Payload value = {},
                                    DeliveryPolicy delivery = {}) {
    return {{opcode, std::move(value)}, delivery};
}

} // namespace hailstone::net::messages
