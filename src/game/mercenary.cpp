#include "game/mercenary.h"
#include "net/messages/mercenary.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "core/msvc_atoi.h"

namespace hailstone::game {

namespace {

// The "corrupt" check both list handlers make.

// SecondsToHMS @0x8098d0 with the force flag (always split).
void hms(std::int32_t seconds, std::int32_t& h, std::int32_t& m, std::int32_t& s) {
    h = seconds / 3600;
    m = seconds % 3600 / 60;
    s = seconds % 60;
}

std::string printf_text(const char* fmt, auto... args) {
    char b[256];
    std::snprintf(b, sizeof b, fmt, args...);
    return b;
}

} // namespace

bool merc_inactive(std::uint32_t state) { return state == 1 || state == 0 || state == 2 || state == 4; }

int merc_stricmp(const std::string& a, const std::string& b) {
    std::size_t n = std::min(a.size(), b.size());
    for (std::size_t k = 0; k < n; ++k) {
        int x = std::tolower(static_cast<unsigned char>(a[k])), y = std::tolower(static_cast<unsigned char>(b[k]));
        if (x != y) return x - y;
    }
    return static_cast<int>(a.size()) - static_cast<int>(b.size());
}

void merc_reset(MercManager& m, std::uint32_t fast_time) {
    // Frees both merchant lists, zeroes the state block (0xdc bytes), then sets its fields again.
    m.merchant_list.clear();
    m.merc_types.clear();
    m.active = {};
    m.current_index = -1;
    m.liaison_id = 0;
    m.saved_liaison_id = 0;
    m.slot_count = 1;
    m.has_mercenary = 0;
    m.state = kMercActive;
    m.suspend_time = 0;
    m.active.active_since = fast_time;
    m.timer_interval = 0;
    m.timer_dword_8 = 0;
    m.last_tick = 0;
    m.stance_pointers = 0;
    m.selected = {};
}

void merc_tick_upkeep(MercManager& m, std::uint32_t now) {
    if (!merc_inactive(m.state)) {
        std::uint32_t left = m.active.time_left_ms, elapsed = now - m.last_tick;
        m.active.time_left_ms = left >= elapsed ? left - elapsed : 0;
    }
    m.last_tick = now;
}

void merc_dismiss_locally(MercManager& m, MercHooks& h) {
    m.has_mercenary = 0;
    m.state = kMercActive;
    m.suspend_time = 0;
    h.chat("Your mercenary is being dismissed!", kMercChatDefault);
    m.last_tick = 0;
    h.event(1, 0);
}

void merc_restart_upkeep(MercManager& m, MercHooks& h) {
    m.last_tick = h.tick_count();
    h.event(0x13, 0);
    if (m.state == 0) h.event(5, 0);
    else if (m.state == 1 || m.state == 2 || m.state == 4) h.event(2, 0);
}

// ---------------------------------------------------------------- handlers

void merc_handle_data_response(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    m.merc_types = n.types;
    m.merchant_list = n.mercenaries;
    if (m.merc_types.empty()) {
        if (m.liaison_id != 0) h.chat("You do not meet the requirements to purchase any mercenaries.\n", kMercChatDefault);
        if (h.find_window("MMTW_MerchantWnd")) h.hide_window("MMTW_MerchantWnd");
        return;
    }
    if (m.liaison_id != 0) h.event(0xd, 0);
}

void merc_handle_data_update(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    m.current_index = n.current_index;
    m.owned = n.mercenaries;
    m.slot_count = n.slot_count;
    if (m.owned.empty()) {
        h.chat("You do not currently have a mercenary.\n", kMercChatDefault);
        if (h.find_window("MMGW_ManageWnd")) {
            h.hide_window("MMGW_ManageWnd");
            h.store_window_visibility("MMGW_ManageWnd");
        }
        return;
    }
    if (m.current_index >= 0 && m.current_index < static_cast<std::int32_t>(m.owned.size())) {
        m.active = m.owned[static_cast<std::size_t>(m.current_index)];
        m.stance_pointers = m.active.stance_count;
        merc_restart_upkeep(m, h);
    } else {
        m.current_index = -1;
    }
}

void merc_handle_hire(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    std::uint32_t code = n.result;
    auto say = [&](const char* text) { h.chat(text, kMercChatDefault); };
    switch (code) {
    case 0:
        m.active.time_left_ms = m.timer_interval;
        h.event(0x12, 0);
        break;
    case 1: say("You do not have enough funds to make that purchase!\n"); break;
    case 2: say("Mercenary does not exist!\n"); break;
    case 3: say("Mercenary failed to spawn!\n"); break;
    case 4: say("Mercenaries are not allowed in raids!\n"); break;
    case 5: say("You already have a pending mercenary purchase!\n"); break;
    case 6: say("You have the maximum number of mercenaries.  You must dismiss one before purchasing a new one!\n"); break;
    case 7: say("You can not purchase a mercenary because your group is full!\n"); break;
    case 8: say("You can not purchase a mercenary because you are in combat!\n"); break;
    case 9:
        say("You have recently dismissed a mercenary and must wait a few more seconds before you can purchase a new one!\n");
        break;
    case 10: say("An error occurred created your mercenary!\n"); break;
    case 11:
    case 12:
        h.report_upkeep_charged(code == 12 ? 1 : 0);
        h.event(0, 0);
        break;
    case 13:
        say("You ran out of funds to pay for your mercenary!\n");
        say("Your mercenary has been suspended!\n");
        h.event(2, 0);
        break;
    case 14:
        h.report_upkeep_waived();
        h.event(0, 0);
        break;
    case 15: say("Your mercenary is about to be suspended due to insufficient funds!\n"); break;
    case 16: say("There is no mercenary liaison nearby!\n"); break;
    case 17: say("You are too far from the liaison!\n"); break;
    case 18: say("You do not meet the requirements for that mercenary!\n"); break;
    case 19: say("You are unable to interact with the liaison!\n"); break;
    case 20: say("You do not have a high enough membership level to purchase this mercenary!\n"); break;
    case 21: say("Your purchase has failed because this mercenary requires a Gold membership!\n"); break;
    case 22: say("Your purchase has failed because this mercenary requires at least a Silver membership!\n"); break;
    default: break;
    }
}

void merc_handle_timer(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    std::uint8_t flag = n.flag; // only the low byte of +0 (EQEmu sends the mercenary's entity id there)
    std::uint32_t state = n.state, timer = n.suspend_time;
    m.timer_interval = n.timer_interval;
    m.timer_dword_8 = n.timer_tag;
    if ((m.has_mercenary == 0 && flag != 0) || (!merc_inactive(m.state) && state != kMercActive))
        merc_send_data_update_request(h);
    else if (m.has_mercenary != 0 && flag == 0 && state == kMercActive)
        merc_dismiss_locally(m, h);
    m.has_mercenary = flag;
    m.state = state;
    m.suspend_time = timer;
    merc_restart_upkeep(m, h);
}

void merc_handle_suspend_response(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    m.state = kMercSuspended;

    m.suspend_time = n.suspend_time;
    h.event(2, 0);
}

void merc_handle_unsuspend_response(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    m.state = kMercActive;
    m.active.active_since = h.fast_time();
    h.event(3, 0);
}

void merc_handle_suspended(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    if (m.state != 1 && m.state != 0 && m.state != 4) m.state = kMercSuspendedHere;
    h.chat("Your mercenary has been suspended.", kMercChatDefault);
    m.suspend_time = 0;
    h.event(6, 0);
}

void merc_handle_died(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    m.state = kMercDead;

    m.suspend_time = n.suspend_time;
    h.event(5, 0);
}

void merc_handle_timer_cleared(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    m.suspend_time = 0;
    h.event(8, 0);
}

void merc_handle_event7(MercManager&, const net::messages::MercenaryNotice& n, MercHooks& h) { h.event(7, 0); }

void merc_handle_unknown1(MercManager&, const net::messages::MercenaryNotice& n, MercHooks& h) { h.event(0x19, n.flag); }

void merc_handle_time_left(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    m.active.time_left_ms = n.time_left;
    m.last_tick = h.tick_count();
    h.event(0x13, 0);
}

void merc_handle_upkeep_charged(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {
    m.state = kMercActive;
    m.active.active_since = h.fast_time();
    h.report_upkeep_charged(n.flag);
    h.event(4, 0);
}

void merc_handle_costs(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {

    m.active.purchase_cost = n.purchase_cost;
    m.active.alt_purchase_cost = n.alt_purchase_cost;
    m.active.upkeep_cost = n.upkeep_cost;
    m.active.alt_upkeep_cost = n.alt_upkeep_cost;
    h.event(0x1a, 0);
}

void merc_handle_command(MercManager&, const net::messages::MercenaryNotice& n, MercHooks& h) {
    h.event(0x10, static_cast<std::int32_t>(n.result));
}

void merc_handle_switch(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) {

    std::int32_t seconds = static_cast<std::int32_t>(n.seconds);
    std::uint32_t result = n.result;
    auto say = [&](const std::string& text) { h.chat(text, 15); };
    switch (result) {
    case 0:
        if (seconds > 0) {
            m.state = kMercSuspended;
            m.suspend_time = h.fast_time() + static_cast<std::uint32_t>(seconds);
        }
        break;
    case 1: say("You cannot switch mercenaries because you are not in fast regeneration mode!"); break;
    case 2:
        if (seconds != 0)
            say("You cannot switch mercenaries because you cannot unsuspend the selected mercenary for another " +
                buff_timer_text(seconds) + "!");
        else
            say("You cannot switch mercenaries because you cannot unsuspend the selected mercenary!");
        break;
    case 3: say("You cannot switch to that mercenary because you have lost access to that slot."); break;
    case 4: say("You cannot switch mercenaries while your current mercenary is dead!"); break;
    case 5: say("You cannot switch mercenaries right now."); break;
    default: break;
    }
    if (result != 0) h.event(0xc, 0);
}

void merc_handle_dismiss(MercManager& m, const net::messages::MercenaryNotice& n, MercHooks& h) { merc_dismiss_locally(m, h); }

// ---------------------------------------------------------------- senders

void merc_request_timer_and_data(MercManager& m, MercHooks& h) {
    merc_reset(m, h.fast_time());
    h.send({net::messages::MercenaryAction::RequestTimer});
    h.send({net::messages::MercenaryAction::ZoneEntered});
}

void merc_send_data_request(MercManager& m, MercHooks& h) { h.send({.action = net::messages::MercenaryAction::RequestMerchantList, .liaison_id = m.liaison_id}); }

void merc_send_data_update_request(MercHooks& h) { h.send({net::messages::MercenaryAction::RequestOwnedList}); }

void merc_send_hire(MercManager& m, std::uint32_t merc_id, MercHooks& h) {
    h.send({.action = net::messages::MercenaryAction::Hire, .merc_id = merc_id,
            .liaison_id = m.liaison_id, .owner_id = m.selected.id});
}

void merc_send_suspend(MercHooks& h) { h.send({net::messages::MercenaryAction::Suspend}); }

void merc_send_dismiss(MercManager& m, bool confirm, bool send, MercHooks& h) {
    if (confirm) {
        h.confirm(7612, 2, 10000);
        return;
    }
    if (send) h.send({net::messages::MercenaryAction::Dismiss});
    merc_dismiss_locally(m, h);
}

void merc_send_assist_mode(MercManager& m, std::uint8_t on, MercHooks& h) {
    m.active.assist_mode = on;
    h.event(0x15, on);
    h.send({.action = net::messages::MercenaryAction::SetAssist, .enabled = on != 0});
}

void merc_send_call_for_assist(MercHooks& h) { h.send({net::messages::MercenaryAction::CallForAssist}); }

void merc_send_switch(MercManager& m, std::int32_t index, MercHooks& h) {
    if (index >= static_cast<std::int32_t>(m.owned.size()) || index == m.current_index) {
        h.chat("Already using that mercenary!", 15);
        return;
    }
    h.send({.action = net::messages::MercenaryAction::Switch, .index = index});
}

void merc_request_set_owner(MercManager& m, int target, std::uint32_t my_spawn_id, std::uint32_t target_spawn_id,
                            MercHooks& h) {
    if (m.has_mercenary == 0) return h.chat("You must own a mercenary in order to set a new mercenary owner!\n", 15);
    if (target == 0) return h.chat("You must target a player in order to set a new mercenary owner!\n", 15);
    if (target == 1) return h.chat("You already own this mercenary!\n", 15);
    h.send({.action = net::messages::MercenaryAction::SetOwner, .owner_id = my_spawn_id, .target_id = target_spawn_id});
}

// ---------------------------------------------------------------- commands

bool merc_windows_allowed(const MercManager& m, MercHooks& h) {
    if (m.has_mercenary == 0 && !merc_inactive(m.state)) {
        h.chat("You must own a mercenary to bring this window up!", kMercChatDefault);
        return false;
    }
    return true;
}

void merc_assist_command(MercManager& m, const std::string& args, MercHooks& h) {
    if (m.has_mercenary == 0) return h.chat("You have no mercenary to assist you!", 15);
    if (args.empty()) return merc_send_call_for_assist(h);
    // __strnicmp(args, word, n): a shorter argument differs at its NUL.
    auto prefix = [&](const char* w, std::size_t n) {
        return args.size() >= n && merc_stricmp(args.substr(0, n), std::string(w, n)) == 0;
    };
    if (prefix("on", 2)) {
        merc_send_assist_mode(m, 1, h);
        h.chat("Mercenary auto-assist is now on.", 10);
    } else if (prefix("off", 3)) {
        merc_send_assist_mode(m, 0, h);
        h.chat("Mercenary auto-assist is now off.", 10);
    }
}

void merc_switch_command(MercManager& m, const std::string& args, MercHooks& h) {
    if (m.owned.empty()) return h.chat("You don't have any mercenaries!", 15);
    if (args.empty()) return h.chat("Specify which mercenary you want to switch to.", 15);
    int n = core::msvc_atoi(args);
    if (n > 0 && n <= static_cast<int>(m.owned.size())) return merc_send_switch(m, n - 1, h);
    h.chat("That is an invalid mercenary index.", 15);
}

// ---------------------------------------------------------------- texts

std::string merc_duration_short(std::uint32_t ms) {
    std::int32_t hh, mm, ss;
    hms(static_cast<std::int32_t>(ms / 1000), hh, mm, ss);
    if (hh > 0) return mm == 0 ? printf_text("%uh", hh) : printf_text("%uh %um", hh, mm);
    return ss == 0 ? printf_text("%um", mm) : printf_text("%um %us", mm, ss);
}

std::string merc_duration_long(std::uint32_t ms) {
    std::int32_t hh, mm, ss;
    hms(static_cast<std::int32_t>(ms / 1000), hh, mm, ss);
    if (hh > 0) {
        if (mm > 0) {
            return ss < 1 ? printf_text("%d hours and %d minutes", hh, mm)
                          : printf_text("%d hours, %d minutes, and %d seconds", hh, mm, ss);
        }
        if (ss > 0) return printf_text("%d hours and %d seconds", hh, ss);
    }
    if (mm < 1 || ss < 1) {
        if (hh >= 1) return printf_text("%d hours", hh);
        if (mm >= 1) return printf_text("%d minutes", mm);
        if (ss >= 1) return printf_text("%d seconds", ss);
        return {};
    }
    return printf_text("%d minutes and %d seconds", mm, ss);
}

std::string buff_timer_text(std::int32_t seconds) {
    std::int32_t hh, mm, ss;
    hms(seconds, hh, mm, ss);
    if (hh < 1) {
        if (mm < 1) return printf_text("%ds", ss);
        if (ss < 1) return printf_text("%dm", mm);
        return printf_text("%dm:%ds", mm, ss);
    }
    if (mm > 0) {
        if (ss > 0) return printf_text("%dh:%dm:%ds", hh, mm, ss);
        if (ss == 0) return printf_text("%dh:%dm", hh, mm);
    }
    if (mm == 0 && ss > 0) return printf_text("%dh:%ds", hh, ss);
    return printf_text("%dh", hh);
}

std::string merc_upkeep_charged_text(const MercManager& m, bool alt, const std::string& currency) {
    std::string timer = merc_duration_long(m.timer_interval);
    if (!alt)
        return printf_text("You have been charged a mercenary upkeep cost of %d plat, and %d gold and your mercenary "
                           "upkeep cost timer has been reset to %s.\n",
                           m.active.upkeep_cost / 10, m.active.upkeep_cost % 10, timer.c_str());
    return printf_text("You have been charged a mercenary upkeep cost of %d %s and your mercenary upkeep cost timer "
                       "has been reset to %s.\n",
                       m.active.alt_upkeep_cost, currency.c_str(), timer.c_str());
}

std::string merc_upkeep_waived_text(const MercManager& m, const std::string& currency) {
    std::string timer = merc_duration_long(m.timer_interval);
    std::int32_t plat = m.active.upkeep_cost / 10, gold = m.active.upkeep_cost % 10;
    if (m.active.alt_currency == 0)
        return printf_text("Your mercenary waived an upkeep cost of %d plat, and %d gold and your mercenary upkeep cost "
                           "timer has been reset to %s.\n",
                           plat, gold, timer.c_str());
    if (m.active.byte_0x20 == 0)
        return printf_text("Your mercenary waived an upkeep cost of %d plat, and %d gold or %d %s and your mercenary "
                           "upkeep cost timer has been reset to %s.\n",
                           plat, gold, m.active.alt_upkeep_cost, currency.c_str(), timer.c_str());
    return printf_text("Your mercenary waived an upkeep cost of %d %s and your mercenary upkeep cost timer has been "
                       "reset to %s.\n",
                       m.active.alt_upkeep_cost, currency.c_str(), timer.c_str());
}

std::string merc_time_left_text(const MercManager& m) {
    if (m.state == kMercActive && m.active.time_left_ms > 0) return merc_duration_short(m.active.time_left_ms);
    return "0m 0s";
}

std::string merc_upkeep_tooltip(const MercManager& m) {
    std::int32_t plat = m.active.upkeep_cost / 10, gold = m.active.upkeep_cost % 10;
    switch (m.state) {
    case 5:
        if (plat != 0)
            return gold == 0 ? printf_text("Time left until you get charged upkeep cost: %dp.", plat)
                             : printf_text("Time left until you get charged upkeep cost: %dp %dg.", plat, gold);
        if (gold != 0) return printf_text("Time left until you get charged upkeep cost: %dg.", gold);
        return "This mercenary's upkeep cost is 0.";
    case 0: return "Your mercenary upkeep timer will start after you revive your mercenary.";
    case 1:
    case 2:
    case 4: return "Your mercenary upkeep timer will start after you unsuspend your mercenary.";
    default: return {};
    }
}

std::int32_t merc_cost_platinum(std::int32_t gold) { return gold / 10; }
std::int32_t merc_cost_gold(std::int32_t gold) { return gold % 10; }

std::string merc_description_summary(const std::string& d) {
    // Up to (not including) the fourth '<' (CXStr::FindCharFrom from the position after each one; fewer: the last
    // one found; none: the whole text), then every "<br>" becomes " ".
    std::size_t pos = 0;
    bool found = false;
    for (int k = 0; k < 4; ++k) {
        std::size_t at = d.find('<', pos);
        if (at == std::string::npos) break;
        pos = at + 1;
        found = true;
    }
    std::string s = found ? d.substr(0, pos - 1) : d;
    for (std::size_t at; (at = s.find("<br>")) != std::string::npos;) s = s.substr(0, at) + " " + s.substr(at + 4);
    return s;
}

std::string merc_race_and_type(const std::string& d) {
    std::string s = d;
    std::size_t at = s.find("Race: ");
    s = s.substr(at == std::string::npos ? 0 : std::min(s.size(), at + 6));
    at = s.find("<br>Type: ");
    if (at != std::string::npos) s = s.substr(0, at) + " - " + s.substr(at + 10);
    at = s.find("<br>");
    if (at != std::string::npos) s = s.substr(0, at);
    return s;
}

MercManageButtons merc_manage_buttons(const MercManager& m) {
    MercManageButtons b;
    switch (m.state) {
    case 0:
    case 1:
    case 2:
        if (m.state == 2) {
            b.suspend_label = "Unsuspend";
        } else if (m.suspend_time == 0) {
            b.suspend_label = m.state == 1 ? "Unsuspend" : "Revive";
            b.suspend_enabled = true;
        }
        b.dismiss_enabled = true;
        break;
    case 3:
        b.unchanged = true; // only the suspend button changes: enabled
        b.suspend_enabled = true;
        break;
    case 5:
        b.all_enabled = true;
        b.suspend_enabled = b.dismiss_enabled = true;
        b.suspend_label = "Suspend";
        b.suspend_tooltip = "Suspends your current mercenary.";
        b.call_for_assist_enabled = m.active.assist_mode == 0;
        break;
    default:
        b.unchanged = true;
        break;
    }
    return b;
}

MercSuspendButton merc_suspend_button(const MercManager& m, bool in_combat, bool in_raid) {
    MercSuspendButton b;
    if (in_combat) {
        b.tooltip = "You cannot suspend or un-suspend your mercenary while in combat.";
    } else if (in_raid) {
        b.tooltip = m.state == 0 ? "You can not revive your mercenary because you are in a raid."
                                 : "You can not unsuspend your mercenary because your are in a raid.";
    } else if (m.suspend_time != 0) {
        b.tooltip = m.state == 0 ? "Revive your current mercenary once the restoration timer has expired."
                                 : "Unsuspend your current mercenary once the restoration timer has expired.";
    } else if (m.state == 2) {
        b.tooltip = "You can not unsuspend your current mercenary while in this zone.";
    } else {
        b.enabled = true;
        b.tooltip = m.state != 0 ? "Unsuspend your current mercenary." : "Revive your current mercenary.";
    }
    return b;
}

} // namespace hailstone::game
