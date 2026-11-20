#pragma once

// The stock RoF2 Select Reward managers (PendingRewardMgr / RewardSelectionMgr).
// The same payloads use independent claimable (0x6411) and preview (0x6471) lanes.
// Wire layouts checked against EQEmu common/achievements.cpp and the native senders
// 0x463b50, 0x463cd0, 0x463d80 and 0x464cc0. No provider-specific authorization lives here.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/protocol/rof2/packets/item_packets.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/wire/app_packet.h"

#include "net/messages/reward.h"

namespace hailstone::net {

inline constexpr std::uint16_t kOpPendingRewards = 0x6411, kOpRewardPreview = 0x6471;
inline std::uint16_t reward_opcode(RewardLane lane) {
    return rof2::to_wire(lane == RewardLane::Pending ? rof2::Op::AchievementReward : rof2::Op::RewardSelection);
}


// Host resource bounds, not original-client capacities. All counts are also checked against
// the bytes remaining before allocation; limits apply across the whole collection.
inline constexpr std::size_t kMaxRewardPacketBytes = 8u << 20;
std::optional<RewardPacket> decode_reward_packet(const AppPacket& p);

AppPacket encode_pending_rewards_request(); // action 6, native 0x463cd0
AppPacket encode_task_reward_preview(std::uint32_t task_id); // action 4, preview lane only
AppPacket encode_achievement_reward_view(std::uint32_t definition_index); // action 5, not achievement ID
AppPacket encode_reward_inspect(RewardLane lane, std::uint32_t set_id, std::uint32_t wire_option_id,
                               std::uint32_t entry_id, std::uint32_t item_id);
AppPacket encode_reward_claim(std::uint32_t pending_id, std::uint32_t set_id, std::uint32_t wire_option_id);

} // namespace hailstone::net
