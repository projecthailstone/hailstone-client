#pragma once
// Native gameplay fellowship values. Binary action codes and wire offsets
// are RoF2 codec details, not part of the game-message contract.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace hailstone::net {
// Maximum supported semantic fellowship roster size, including extended rosters.
inline constexpr std::size_t kMaxFellowshipMembers = 64;

enum class FellowshipAction : std::uint8_t {
    Create, Motd, Accept, Decline, Invite, Remove, Promote, CreateCamp,
    DestroyCamp, Leave, End, ToggleSharing
};
struct FellowshipMember {
    std::uint32_t character_id = 0;
    std::string name;
    std::uint32_t zone_id = 0, level = 0, class_ = 0, last_seen = 0;
    bool sharing = false, capped = false, offline = false;
};

struct FellowshipCamp {
    float x = 0, y = 0, z = 0;
    std::uint32_t zone_id = 0, expires = 0;
    bool active = false;
};

struct FellowshipKit {
    bool available = false;
    std::uint32_t camp_type_id = 0, description_id = 0;
    std::string name;
};

struct FellowshipInvite {
    std::uint32_t inviter_id = 0;
    std::string inviter_name;
    // The 1080-byte form (an extension beyond RoF2); false for RoF2's 0x434 bytes.
    bool extended = false;
};

struct FellowshipVitality {
    std::uint64_t normal = 0, normal_cap = 0;
    std::uint32_t aa = 0, aa_cap = 0;
};

struct FellowshipState {
    std::uint32_t id = 0;
    std::string name, motd;
    std::vector<FellowshipMember> members;
    FellowshipCamp camp;
    std::vector<FellowshipKit> kits;
    std::optional<FellowshipInvite> invite;
    FellowshipVitality vitality;

    bool active() const { return id != 0 && !members.empty(); }
    const FellowshipMember* member(std::uint32_t character_id) const;
    const FellowshipMember* member(std::string_view name) const;
    bool is_leader(std::string_view name) const;
};
struct FellowshipIntent {
    FellowshipAction action = FellowshipAction::Create;
    std::uint32_t target = 0, camp_type = 0;
    std::string motd;
};
struct FellowshipKitQuery {
    bool available_only = false;
    std::uint32_t category_mask = 0;
};
} // namespace hailstone::net
