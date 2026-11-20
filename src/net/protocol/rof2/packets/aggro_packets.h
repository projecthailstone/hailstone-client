#pragma once

// The aggro meter in EQEmu's layouts (zone/client.cpp Client::ProcessAggroMeter, zone/aggromanager.h; RoF2 and
// later only, behind the server's Character:EnableAggroMeter rule). The original's CAggroMeterWnd is not decoded.

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "net/messages/zone.h"
#include "net/wire/app_packet.h"

namespace hailstone::net {

// OP_AggroMeterTargetInfo (Z→C, 8 bytes): u32 the locked target, u32 the target the meter follows.

std::optional<AggroTargetInfo> decode_aggro_target_info(const AppPacket& p);
AppPacket encode(const AggroTargetInfo& v); // server direction, for tests

// OP_AggroMeterUpdate (Z→C): u8 secondary changed, then (when set) u32 the secondary's id; u8 count, count × {u8
// type, u16 percent} (only the entries that changed).

std::optional<AggroUpdate> decode_aggro_update(const AppPacket& p);
AppPacket encode(const AggroUpdate& v); // server direction, for tests

// OP_AggroMeterLockTarget (C→Z, 4 bytes): the spawn to lock the meter on (0 unlocks).
AppPacket encode_aggro_lock(std::uint32_t spawn_id);

} // namespace hailstone::net
