#pragma once

// Glue between net::GameConnection's group and pet state and the group and
// pet windows (ui/group_windows.h): builds the view models each frame, draws
// them and passes the player's clicks back to the connection.

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/group_windows.h"

namespace hailstone::net { class GameConnection; }
namespace hailstone::game {
struct BuffSlot;
struct Spawn;
struct SpellData;
struct WorldState;
} // namespace hailstone::game

namespace hailstone::app {

// Which spawn each group member is, by name (a spawn's may carry EQEmu's trailing digits; no case), kept from frame
// to frame: a member's spawn is checked by its id, and a member with none (in another zone) is looked for again
// among all the spawns only when their number changed or a second has passed. Each frame: begin(), find() per member,
// end() (forgets those not asked for).
class GroupMemberSpawns {
public:
    void begin() { ++frame_; }
    const game::Spawn* find(const game::WorldState& world, const std::string& name, std::int64_t now_ms);
    void end();
    void clear() { members_.clear(); }
    std::size_t scans() const { return scans_; } // full searches so far (tests)

private:
    struct Entry {
        std::uint32_t id = 0;     // the spawn found, 0 none
        std::size_t spawns = 0;   // how many spawns there were at the last search
        std::int64_t looked_ms = 0;
        bool looked = false;
        std::uint32_t frame = 0;  // the frame it was last asked for
    };
    std::unordered_map<std::string, Entry> members_;
    std::uint32_t frame_ = 0;
    std::size_t scans_ = 0;
};

// The pet window's time left on one of the pet's buffs, as "1m05s" or "42s"; empty for a permanent buff or one run out.
// A suspendable buff waits at its count as sent in a zone that suspends buffs (game::buff_countdown_held), as the
// server's does; otherwise it counts down from when the packet arrived.
std::string pet_buff_time_left(const game::BuffSlot& b, const game::SpellData* spell, bool zone_suspends_buffs,
                               std::int64_t now_ms);

// Whether a group member's name (clean) is the spawn's (which may carry EQEmu's trailing digits), ignoring case.
bool group_member_is(std::string_view member, std::string_view spawn);

class GroupUi {
public:
    void draw(net::GameConnection& conn);
    void toggle_group() { group_.open = !group_.open; }
    void toggle_pet() { pet_.open = !pet_.open; }
    // Testing (--auto-commands, --auto-accept): chat lines typed one every 3 s
    // after zoning in, and accepting group invitations.
    void set_testing(const std::string& commands, bool accept_invites);

private:
    ui::GroupWindowView group_;
    ui::PetWindowView pet_;
    GroupMemberSpawns member_spawns_;
    std::vector<std::string> auto_commands_;
    std::size_t auto_next_ = 0;
    bool auto_accept_ = false;
    std::chrono::steady_clock::time_point in_zone_since_{}, invite_seen_{}, last_command_{};
};

} // namespace hailstone::app
