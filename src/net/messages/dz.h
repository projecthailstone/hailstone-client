#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>
namespace hailstone::net {
enum class DzMemberStatus : std::uint8_t { Unknown=0, Online=1, Offline=2, InDynamicZone=3, LinkDead=4 };
inline constexpr std::uint32_t kDzMembersMax=1000;
struct DzInfo {
    bool assigned = false;
    std::uint32_t max_players = 0;
    std::string name, leader;
};
struct DzInvite {
    std::string inviter, expedition;
    bool swapping = false;
    std::string swap_name;
    std::uint16_t zone = 0, instance = 0;
};
struct DzLockout {
    std::string expedition;
    std::uint32_t seconds = 0;
    std::int32_t event_type = 0;
    std::string event;
};
struct DzMember {
    std::string name;
    DzMemberStatus status = DzMemberStatus::Unknown;
};
struct DzMemberName {
    bool add = false;
    std::string name;
};
struct DzChoice {
    std::uint16_t zone = 0, instance = 0;
    std::uint32_t id1 = 0, type = 0, id2 = 0;
    std::string description, leader;
};
struct DzCompassPoint {
    std::uint32_t type = 0;
    glm::vec3 at{0.0f};
};
struct DzSwapIntent { std::string add, remove; };
struct DzInviteReply { DzInvite invite; bool accept=false; };
} // namespace hailstone::net
