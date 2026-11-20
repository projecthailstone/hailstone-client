#pragma once
// Protocol-independent group membership and leadership values. Wire layouts
// and opcode numeric values belong only to protocol/rof2/packets/.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace hailstone::net {
struct GroupInvite {
    std::string invitee;
    std::string inviter;
    std::uint32_t unknown[5] = {};
    std::uint32_t follow_cookie = 0; // the u32 at +148 when present
};
struct GroupFollow {
    std::string inviter;
    std::string invitee;
    std::uint32_t unknown[5] = {};
    std::uint32_t unknown148 = 0;
};
// Semantic invitation cancellation reasons. They are not tied to a wire codec.
inline constexpr std::uint32_t kCancelDeclined = 3, kCancelAlreadyInvited = 1,
                               kCancelInRaid = 4, kCancelRaidMember = 5;
struct GroupCancel {
    std::string inviter;
    std::string invitee;
    std::uint32_t reason = 0;
};
struct GroupGeneric {
    std::string name1;
    std::string name2;
};
struct GroupJoin {
    std::string owner_name;
    std::string member_name;
    std::uint8_t merc = 0;
    std::uint32_t level = 0;
};
struct GroupUpdateMember {
    std::uint32_t slot = 0;
    std::string name;
    std::uint16_t merc = 0;
    std::string owner_name;
    std::uint32_t level = 0;
    std::uint8_t main_tank = 0, main_assist = 0, puller = 0;
    std::uint32_t offline = 0;
    std::uint32_t timestamp = 0;
};
struct GroupUpdateB {
    std::uint32_t group_id = 0;
    std::string leader;
    std::vector<GroupUpdateMember> members;
};
struct GroupMakeLeader {
    std::string current_leader;
    std::string new_leader;
};
struct MobPercent {
    std::uint16_t spawn_id = 0;
    std::uint8_t percent = 0;
};
struct LeadershipRank {
    std::uint32_t ability = 0, rank = 0, points_left = 0;
};
struct LeadershipExp {
    double group_exp = 0, raid_exp = 0;
    std::uint32_t group_points = 0, raid_points = 0;
};
struct GroupLeadershipUpdate {
    std::array<std::uint32_t, 16> ranks{};
    std::uint32_t npc_marker = 0;
};
struct DelegateAbility {
    std::uint32_t ability = 0, member = 0, action = 0, spawn_id = 0;
    std::string name;
};
struct MarkNpc {
    std::uint32_t spawn_id = 0, number = 0;
    std::string name;
};
struct InspectBuffs {
    std::array<std::int32_t, 42> spells{}, ticks{};
};
struct GroupRole {
    std::string member, leader;
    std::int32_t role = 0;
    std::uint8_t on = 0;
};
struct GroupMentorIntent { std::int32_t percent=0; std::string name; };
struct GroupAbilityIntent { std::uint32_t ability=0, parameter=0; };
} // namespace hailstone::net
