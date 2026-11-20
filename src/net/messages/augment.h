#pragma once

// Protocol-independent gameplay values extracted from the RoF2 packet declarations.
// Application codecs translate these values to their own wire layouts.
// Serial lengths, packet opcodes and binary read/write remain in protocol/rof2/.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "net/messages/item.h"

namespace hailstone::net {

// The request's action (+0x24; EQEmu AugmentActions).
enum class AugmentAction : std::uint32_t { Insert = 0, Remove = 1, Swap = 2, Destroy = 3 };

// OP_AugmentItem (0x28 bytes, UdpConnection::Send(4, buf, 0x2a)):
//   +0x00 the host's 64-bit serial (ItemBase +0xf8 / +0xfc: the item header's instance_id, parcel_item_id; the
//         server reads it as dest_inst_id + container_index and ignores it),
//   +0x08 the host's ItemGlobalIndex (+0x38 .. +0x43 copied as three dwords: Location as an int32, the slots, and
//         the two padding bytes, ItemSlot::unknown01),
//   +0x14 the socket 0..5,
//   +0x18 the distiller's ItemGlobalIndex, or ItemSlot::invalid() (-1, -1, 0xffff) when no distiller is used (the
//         last two bytes are left unwritten by the original, stack or heap garbage; sent as 0 here),
//   +0x24 the action.
// The cursor augment is never named: insert and swap take it from possessions slot 33 on both sides.
struct AugmentItem {
    std::uint32_t serial = 0;      // instance_id
    std::uint32_t serial_high = 0; // parcel_item_id
    ItemSlot host;
    std::int32_t socket = 0;
    ItemSlot distiller = ItemSlot::invalid();
    AugmentAction action = AugmentAction::Insert;
};

// OP_AugmentInfo (0x4c bytes; AugmentInfo_Struct). The request (SetItem @006ae4b2, Send(4, buf, 0x4e)) is all zero
// but +0 itemid = the augment's SolventItemID and +4 window = the display's ItemWndIndex (+0x5f0). The reply
// (CEverQuest::HandleWorldMessage case 0x0afb @004cb57c) carries the window, the solvent's name at +0x08 (64
// bytes) and at +0x48 a value the client compares with 55 (EQEmu unknown072, always 0 from the NMS server).
struct AugmentInfo {
    std::uint32_t item_id = 0;
    std::uint32_t window = 0;
    std::string name;
    std::uint32_t unknown072 = 0;
};


} // namespace hailstone::net
