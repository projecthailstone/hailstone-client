#pragma once

// Native RoF2 fellowship messages. Extensions beyond RoF2 (a server may offer
// more; a RoF2 server's packets behave exactly as in RoF2): rosters of 13 to 64
// members, and the 1080-byte invitation form. Membership and gameplay limits
// remain the server's decision.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "net/wire/app_packet.h"
#include "net/messages/fellowship.h"

namespace hailstone::net {

// Extension beyond RoF2: RoF2's roster has exactly twelve slots.
inline constexpr std::size_t kNativeFellowshipMembers = 12;
// RoF2's invitation body (Handle_Opcode_0x62ab @0x667db0 reads the inviter at 12).
inline constexpr std::size_t kNativeFellowshipInviteSize = 0x434;
// Extension beyond RoF2: the invitation of EQEmu PR #5134 (unmerged), 1080 bytes.
inline constexpr std::size_t kExtendedFellowshipInviteSize = 1080;
inline constexpr std::uint16_t kOpFellowshipRequest = 0x7121;
inline constexpr std::uint16_t kOpFellowshipInvite = 0x62ab;
inline constexpr std::uint16_t kOpFellowshipRoster = 0x6e4d;
inline constexpr std::uint16_t kOpCampsiteKitRequest = 0x0c12;
inline constexpr std::uint16_t kOpCampsiteKitList = 0x074d;
inline constexpr std::uint16_t kOpFellowshipVitality = 0x2cc6;










AppPacket encode_fellowship_request(FellowshipAction action, std::uint32_t target = 0,
                                    std::string_view motd = {}, std::uint32_t camp_type = 0);
AppPacket encode_fellowship_kit_request(bool available_only, std::uint32_t category_mask);
std::optional<FellowshipState> decode_fellowship_roster(const AppPacket& p);
std::optional<FellowshipInvite> decode_fellowship_invite(const AppPacket& p);
std::optional<std::vector<FellowshipKit>> decode_fellowship_kits(const AppPacket& p);
std::optional<FellowshipVitality> decode_fellowship_vitality(const AppPacket& p);

} // namespace hailstone::net
