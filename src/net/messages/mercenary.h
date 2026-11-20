#pragma once
#include <cstdint>
#include <vector>
#include "game/mercenary.h"
namespace hailstone::net::messages {
enum class MercenaryAction : std::uint8_t {
    RequestTimer, ZoneEntered, RequestMerchantList, RequestOwnedList,
    Hire, Suspend, Dismiss, SetAssist, CallForAssist, Switch,
    SetOwner, SetStance, RequestServerLog
};
struct MercenaryRequest {
    MercenaryAction action{};
    std::uint32_t merc_id = 0, liaison_id = 0, owner_id = 0, target_id = 0;
    std::uint32_t command = 0, stance = 0;
    std::int32_t index = 0;
    bool enabled = false;
};
enum class MercenaryNoticeKind : std::uint8_t {
    MerchantList, OwnedList, HireResult, Timer, Suspended, Unsuspended,
    SuspensionNotice, Died, TimerCleared, Event7, Unknown1, TimeLeft,
    UpkeepCharged, Costs, Command, SwitchResult, Dismissed
};
struct MercenaryNotice {
    MercenaryNoticeKind kind{};
    std::vector<std::uint32_t> types;
    std::vector<game::MercRecord> mercenaries;
    std::int32_t current_index = -1, slot_count = 1;
    std::uint32_t result = 0, seconds = 0, timer_interval = 0, timer_tag = 0;
    std::uint32_t state = 0, suspend_time = 0, time_left = 0;
    std::int32_t purchase_cost = 0, alt_purchase_cost = 0, upkeep_cost = 0, alt_upkeep_cost = 0;
    std::uint8_t flag = 0;
};
} // namespace hailstone::net::messages
