#pragma once
// Shared task state and commands. RoF2-specific layouts are in protocol/rof2/packets/task_packets.*.
#include <cstdint>
#include <string>
#include "game/tasks.h"
namespace hailstone::net {
struct SharedTaskInvite {
    std::int32_t invite_id = 0;
    std::string task, inviter;
};
struct SharedTaskInviteAnswer { std::int32_t invite_id = 0; bool accepted = false; };

struct TaskActivity {
    std::uint32_t sequence = 0, type = 0, task_id = 0;
    game::TaskElement element;
};

struct TaskActivityComplete {
    std::uint32_t index = 0, type = 0, task_id = 0, activity_id = 0, completed = 0, stage_complete = 0;
};

struct CancelTask {
    std::uint32_t sequence = 0, type = 0;
};

struct SharedTaskMember {
    std::string name;
    bool leader = false;
    std::uint32_t template_id = 0;
};

struct SharedTaskMemberChange {
    bool added = false;
    std::string name;
};
struct TaskAcceptIntent { std::uint32_t type=0, id=0, giver=0; };
struct SharedTaskAcceptIntent { std::uint32_t giver=0, id=0; float multiplier=1.0f; };
struct TaskMemberIntent { std::string name; };
} // namespace hailstone::net
