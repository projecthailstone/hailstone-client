#pragma once

// OP_EvolveItem: RoF2's experience-transfer requests and server-owned result previews. Action 0 (progress) and the
// Final Result request (action 4) are net/protocol/rof2/packets/item_packets.h's EvolvingItemUpdate and EvolvingFinalResult.
// Layout reference: EQEmu common/patches/rof2_structs.h and ENCODE(OP_EvolveItem),
// consulted as a protocol specification, with no implementation copied.

#include "net/messages/evolving.h"
#include "net/protocol/rof2/packets/item_packets.h"

namespace hailstone::net {

inline constexpr std::size_t kEvolvingRequestSize = 24, kEvolvingDetailsHeaderSize = 30;
AppPacket encode(const EvolvingRequest& value);
AppPacket encode(const EvolvingUpdate& value); // server direction, for tests
AppPacket encode(const EvolvingTransferDetails& value); // server direction, for tests
std::optional<EvolvingRequest> decode_evolving_request(const AppPacket& p);
std::optional<EvolvingUpdate> decode_evolving_open(const AppPacket& p); // action 1 only
std::optional<EvolvingTransferDetails> decode_evolving_details(const AppPacket& p);

} // namespace hailstone::net
