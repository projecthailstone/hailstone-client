#pragma once

// The augment packets of the item display: OP_AugmentItem (client → zone,
// the insert / remove / swap / destroy request) and OP_AugmentInfo (both directions, the name of an augment's
// solvent). Layouts follow the client's sends (CItemDisplayWnd::InsertAugmentRequest @006a8190 and its siblings,
// CItemDisplayWnd::SetItem @006ae100) and the NMS server's RoF2 translator (rof2_structs.h AugmentItem_Struct,
// AugmentInfo_Struct; DECODE(OP_AugmentItem), ENCODE(OP_AugmentInfo)).

#include <cstdint>
#include <optional>
#include <string>

#include "net/protocol/rof2/packets/item_packets.h"
#include "net/wire/app_packet.h"

#include "net/messages/augment.h"

namespace hailstone::net {


inline constexpr std::size_t kAugmentItemSize = 0x28;
AppPacket encode(const AugmentItem& v);
std::optional<AugmentItem> decode_augment_item(const AppPacket& p); // the server's view, for tests

inline constexpr std::size_t kAugmentInfoSize = 0x4c;
AppPacket encode_augment_info_request(std::uint32_t solvent_id, std::uint32_t window);
std::optional<AugmentInfo> decode_augment_info(const AppPacket& p);
AppPacket encode(const AugmentInfo& v); // server direction, for tests

} // namespace hailstone::net
