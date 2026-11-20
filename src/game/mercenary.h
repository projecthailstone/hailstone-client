#pragma once

// Mercenaries as the original client keeps them: the client mercenary manager (pinstCMercenaryClientManager
// 0xe63e90, CMercenaryClientManager), its packet handlers and senders, the per-frame upkeep timer, and the texts the
// three mercenary windows (MMTW_MerchantWnd, MMGW_ManageWnd, MIW_InfoWnd) show. Addresses
// at each function; checked against the 25 vector files recorded from the original
// (tests/merc_vectors_test.cpp).
//
// Nothing here does networking or UI: the handlers and senders report what they do through MercHooks (chat lines,
// the manager's events, packets, window lookups), net/connection/game_connection_merc.cpp connects them to the game.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hailstone::net::messages { struct MercenaryRequest; struct MercenaryNotice; }

namespace hailstone::game {

// The client mercenary states (state block +4, manager +0x110). 3 and 4 only come from OP_MercenaryTimer +0xc.
inline constexpr std::uint32_t kMercDead = 0, kMercSuspended = 1, kMercSuspendedHere = 2, kMercActive = 5;

// The stance pairs that fit in the 0xd0-byte record (+0x7c .. +0xcb).
inline constexpr std::size_t kMercMaxStances = 10;
// The NPC class of a mercenary liaison (RightClickedOnPlayer) and the reach of that right click.
inline constexpr std::uint8_t kMercLiaisonClass = 71;
inline constexpr float kMercLiaisonRange = 20.0f;
// The default chat colour of dsp_chat_2 (CEverQuest::dsp_chat_2 prints in colour 0x111... the default; -1 here).
inline constexpr std::int32_t kMercChatDefault = -1;
// The stance list's colours (Send_OP_MercenaryCommand): the default row colour and the active stance's green.
inline constexpr std::uint32_t kMercRowDefault = 0xffffffffu, kMercRowActive = 0xff00ff00u;
// Item data of the owned list's rows (the manage window's fill, code at 0x6e6c70): index + 1 for an owned
// mercenary, 0 an empty slot, -1 a locked one.

struct MercStance {
    std::int32_t index = 0;  // the pair's +0 (what /stance queues)
    std::int32_t stance = 0; // dbstr kind 24 name, 25 long and 26 short description
};

// The 0xd0-byte mercenary record (merchant entries, owned mercenaries and the active one). Wire order in
// merc_read_record.
struct MercRecord {
    std::uint32_t id = 0;          // +0x00
    std::uint32_t type = 0;        // +0x04 dbstr kind 21 ("Apprentice Mercenaries (Guktan)")
    std::uint32_t subtype = 0;     // +0x08 dbstr kind 22 ("Tier V") and 23 (the description)
    std::int32_t purchase_cost = 0; // +0x0c gold (shown as cost / 10 platinum and cost % 10 gold)
    std::int32_t upkeep_cost = 0;   // +0x10 gold
    std::int32_t alt_purchase_cost = 0; // +0x14
    std::int32_t alt_upkeep_cost = 0;   // +0x18
    std::uint32_t alt_currency = 0;     // +0x1c alternate currency id (0 none)
    std::uint8_t byte_0x20 = 0;         // +0x20 (read by the upkeep waived report only)
    std::uint32_t time_left_ms = 0;     // +0x24 until the next upkeep charge
    std::uint32_t command = 0;          // +0x28 echoed as OP_MercenaryCommand +0
    std::uint32_t stance = 0;           // +0x2c the current stance (dbstr kind 24)
    std::uint32_t stance_count = 0;     // +0x30 as the wire says (the pairs kept are at most kMercMaxStances)
    std::uint32_t active_since = 0;     // +0x34 Util::FastTime when it became active
    std::uint8_t assist_mode = 0;       // +0x38 auto-assist
    std::string name;                   // +0x39 at most 63 bytes
    std::vector<MercStance> stances;    // +0x7c
    std::uint32_t required_membership = 0; // +0xcc 0 free, 1 silver, 2 gold
};

// The manager's fields (offsets on CMercenaryClientManager).
struct MercManager {
    std::vector<MercRecord> merchant_list; // +0x1c the liaison's mercenaries
    std::vector<std::uint32_t> merc_types; // +0x2c the liaison's types (dbstr kind 21 ids)
    MercRecord selected;                   // +0x3c the merchant entry picked in the subtype list
    std::uint8_t has_mercenary = 0;        // +0x10c (the state block +0)
    std::uint32_t state = kMercActive;     // +0x110
    std::uint32_t suspend_time = 0;        // +0x114 the restoration timer
    MercRecord active;                     // +0x118 the active mercenary
    std::vector<MercRecord> owned;         // +0x1e8
    std::int32_t slot_count = 1;           // +0x1f8
    std::int32_t current_index = -1;       // +0x1fc
    std::uint32_t liaison_id = 0;          // +0x200 the open liaison's spawn id
    std::uint32_t saved_liaison_id = 0;    // +0x204
    std::uint32_t timer_interval = 0;      // +0x208 OP_MercenaryTimer +4 (ms)
    std::uint32_t timer_dword_8 = 0;       // +0x20c OP_MercenaryTimer +8
    std::uint32_t last_tick = 0;           // +0x210 GetTickCount of the last upkeep tick
    std::uint32_t stance_pointers = 0;     // +0x238's count: the active mercenary's stance pairs (0x5669b0)
};

// What the handlers and senders do outside the manager, in the original's order.
class MercHooks {
public:
    virtual ~MercHooks() = default;
    // CEverQuest::dsp_chat (colour) or dsp_chat_2 (kMercChatDefault); the text as the original has it (some lines
    // end with "\n").
    virtual void chat(const std::string& text, std::int32_t color) = 0;
    // An event to the manager's state machine (Merc_DispatchEvent or the inline queue): its id and the argument the
    // events 0x10, 0x15, 0x17 and 0x19 carry.
    virtual void event(std::uint32_t id, std::int32_t argument) = 0;
    // Semantic mercenary command. The selected application codec owns its wire layout.
    virtual void send(const net::messages::MercenaryRequest& command) = 0;
    virtual void send_stance(std::uint32_t command, std::uint32_t stance) = 0;
    // DebugLog_Printf.
    virtual void debug_log(const std::string& text) = 0;
    // UIWindowList_FindWindowByName: whether the window exists; hide_window is its Show(0, 1, 1), store_visibility
    // CSidlScreenWnd::StoreIniVis.
    virtual bool find_window(const char* screen) = 0;
    virtual void hide_window(const char* screen) = 0;
    virtual void store_window_visibility(const char* screen) = 0;
    // Merc_ReportUpkeepCharged @0x569f00 / Merc_ReportUpkeepWaived @0x56a0a0 (merc_upkeep_charged_text,
    // merc_upkeep_waived_text).
    virtual void report_upkeep_charged(std::uint8_t alt_currency) = 0; // the packet's byte; nonzero: alternate currency
    virtual void report_upkeep_waived() = 0;
    // GetTickCount and Util::FastTime.
    virtual std::uint32_t tick_count() = 0;
    virtual std::uint32_t fast_time() = 0;
    // ConfirmationDialog on the manager: the eqstr text, the dialog id (2 dismiss), how long it waits (ms).
    virtual void confirm(std::uint32_t eqstr, std::int32_t dialog, std::int32_t milliseconds) = 0;
};

// States 0, 1, 2 and 4: no active (unsuspended) mercenary (0x564820).
bool merc_inactive(std::uint32_t state);

// Merc_ResetState (0x566620): the zone-in reset; the owned list is kept.
void merc_reset(MercManager& m, std::uint32_t fast_time);
// Merc_TickUpkeepTimer (0x564d70), every frame: while active the time left goes down by the ticks since the last
// one, stopping at 0 (unsigned compare); the last tick becomes now either way.
void merc_tick_upkeep(MercManager& m, std::uint32_t now);
// The local dismissal (Send_OP_MercenaryDismiss without confirm, Handle_OP_MercenaryDismiss, OP_MercenaryTimer).
void merc_dismiss_locally(MercManager& m, MercHooks& h);
// 0x566f70: last tick = GetTickCount, event 0x13, then event 5 for state 0 or 2 for states 1, 2 and 4.
void merc_restart_upkeep(MercManager& m, MercHooks& h);

// The codec supplies typed notice values; no wire buffers enter the game manager.
void merc_handle_data_response(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_data_update(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_hire(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_timer(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_suspend_response(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_unsuspend_response(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_suspended(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_died(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_timer_cleared(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_event7(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_unknown1(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_time_left(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_upkeep_charged(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_costs(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_command(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_switch(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);
void merc_handle_dismiss(MercManager& m, const net::messages::MercenaryNotice& notice, MercHooks& h);

// ---- senders
// Merc_RequestTimerAndData @0x566740 (zone-in): the reset, OP_MercenaryTimerRequest and 0x4820 {8}.
void merc_request_timer_and_data(MercManager& m, MercHooks& h);
// Send_OP_MercenaryDataRequest @0x564770: {liaison id}.
void merc_send_data_request(MercManager& m, MercHooks& h);
// Send_OP_MercenaryDataUpdateRequest @0x5646f0: no payload.
void merc_send_data_update_request(MercHooks& h);
// The hire button (code at 0x564dd0): {selected entry id, merc id, liaison id, u8 0 + 3 bytes the original leaves
// uninitialised (0 here)}.
void merc_send_hire(MercManager& m, std::uint32_t merc_id, MercHooks& h);
// Send_OP_MercenarySuspendRequest @0x565290: {'0'} (suspend, unsuspend and revive).
void merc_send_suspend(MercHooks& h);
// Send_OP_MercenaryDismiss @0x566e00: with `confirm` only asks (eqstr 7612, dialog 2, 10 s); else sends when `send`
// and dismisses locally.
void merc_send_dismiss(MercManager& m, bool confirm, bool send, MercHooks& h);
// Send_Opcode_0x0b72 @0x5670a0: auto-assist (the record's byte, event 0x15, {byte}).
void merc_send_assist_mode(MercManager& m, std::uint8_t on, MercHooks& h);
// Send_Opcode_0x7e6f @0x564e90: call for assistance {'0'}.
void merc_send_call_for_assist(MercHooks& h);
// Send_OP_MercenarySwitch @0x564a70: {index}, refused for an index >= the owned count or the current one.
void merc_send_switch(MercManager& m, std::int32_t index, MercHooks& h);
// Merc_RequestSetOwner @0x564f20: target 0 none, 1 self, 2 another spawn.
void merc_request_set_owner(MercManager& m, int target, std::uint32_t my_spawn_id, std::uint32_t target_spawn_id,
                            MercHooks& h);
// Send_OP_MercenaryCommand @0x6e5ee0: row `row` of the stance list chosen. `row_data` is the list's item data per
// row (CListWnd::GetItemData; `outside` answers a row beyond the list). The old stance's row (the first whose data
// is the record's stance, row 0 if none) gets the default colour, the chosen row green, the record's stance becomes
// the row's data, then {record +0x28, stance}. `set_row_color(row, column, colour)` is CListWnd::SetItemColor.
template <typename SetColor>
void merc_send_command(MercManager& m, const std::vector<std::uint32_t>& row_data, std::uint32_t outside,
                       std::int32_t row, SetColor&& set_row_color, MercHooks& h);

// ---- the commands
// Cmd_mercwindows @0x4e2d20: false (and "You must own a mercenary to bring this window up!") without a mercenary
// and with an active one; "m" with the debug byte toggles the merchant window instead (not here).
bool merc_windows_allowed(const MercManager& m, MercHooks& h);
// Cmd_stance @0x4e3230 / Merc_SetStanceByName @0x567280: the pair whose stance's dbstr kind 24 name matches
// (case-insensitive); returns its index (queued as event 0x17) or -1. `stance_name(stance)` gives the name.
template <typename Name>
std::int32_t merc_stance_command(const MercManager& m, const std::string& args, Name&& stance_name, MercHooks& h);
// Cmd_mercassist @0x4e3290: no argument calls for assistance, "on" / "off" set auto-assist.
void merc_assist_command(MercManager& m, const std::string& args, MercHooks& h);
// Cmd_mercswitch @0x478ac0: "n" switches to mercenary n (1-based).
void merc_switch_command(MercManager& m, const std::string& args, MercHooks& h);

// ---- texts
// FormatDurationShort @0x564b60 ("%uh %um", "%uh", "%um %us", "%um"), FormatDurationLong @0x564c40 ("%d hours,
// %d minutes, and %d seconds" ...; the original sprintf's into 20 bytes, so a longer text overflows its buffer; here
// the whole text) and GetBuffTimerTextString @0x809a30 ("%dh:%dm:%ds" ..., from seconds).
std::string merc_duration_short(std::uint32_t ms);
std::string merc_duration_long(std::uint32_t ms);
std::string buff_timer_text(std::int32_t seconds);
// Merc_ReportUpkeepCharged / Merc_ReportUpkeepWaived: `currency` is the alternate currency's name (dbstr kind 0x11,
// or 0x12 for more than one), "" without one.
std::string merc_upkeep_charged_text(const MercManager& m, bool alt_currency, const std::string& currency);
std::string merc_upkeep_waived_text(const MercManager& m, const std::string& currency);
// The manage window's TimeLeftText (0x6e5c70): state 5 and time left > 0 → merc_duration_short, else "0m 0s".
std::string merc_time_left_text(const MercManager& m);
// Its tooltip (CMercenaryWnd_FormatUpkeepTimerText @0x6e5d80); state 3 formats an uninitialised buffer in the
// original ("" here).
std::string merc_upkeep_tooltip(const MercManager& m);
// A cost as the windows show it: platinum (cost / 10) and gold (cost % 10), signed.
std::int32_t merc_cost_platinum(std::int32_t gold);
std::int32_t merc_cost_gold(std::int32_t gold);
// 0x565e00: the owned list's tooltip from the description (dbstr kind 23): up to its fourth '<', "<br>" → " ".
std::string merc_description_summary(const std::string& description);
// 0x566040: "race - type" from the description ("Race: X<br>Type: Y<br>..." → "X - Y").
std::string merc_race_and_type(const std::string& description);

// The manage window's buttons (CMercenaryManageWnd_UpdateButtons @0x6e5770) and the suspend button (@0x6e63b0).
struct MercManageButtons {
    bool all_enabled = false;       // every child enabled (state 5); else every child disabled (states 0..2)
    bool unchanged = false;         // states 4 and others: nothing changes
    bool suspend_enabled = false, dismiss_enabled = false, call_for_assist_enabled = false;
    std::string suspend_label;      // "" keeps the label
    std::string suspend_tooltip;
};
MercManageButtons merc_manage_buttons(const MercManager& m);
// The suspend button's availability and tooltip (@0x6e63b0): in combat (or, with the PvP rule, not resting for fast
// regeneration), in a raid, while the restoration timer runs, in state 2.
struct MercSuspendButton {
    bool enabled = false;
    std::string tooltip;
};
MercSuspendButton merc_suspend_button(const MercManager& m, bool in_combat, bool in_raid);

// One row of the manage window's owned list (6 rows; the fill at 0x6e6c70).
struct MercOwnedRow {
    std::string marker;  // column 0: "X" for the current mercenary
    std::string text;    // column 1: "race - type", "Empty", "Locked"
    std::string tooltip; // the description summary, "Empty Mercenary Slot", "Unlock access to this slot on the marketplace!"
    std::int32_t data = 0; // index + 1, 0 empty, -1 locked
};
inline constexpr int kMercOwnedRows = 6;
template <typename Description>
std::vector<MercOwnedRow> merc_owned_rows(const MercManager& m, Description&& description);

// ---- template definitions

template <typename SetColor>
void merc_send_command(MercManager& m, const std::vector<std::uint32_t>& row_data, std::uint32_t outside,
                       std::int32_t row, SetColor&& set_row_color, MercHooks& h) {
    auto data = [&](std::int32_t r) {
        return r >= 0 && static_cast<std::size_t>(r) < row_data.size() ? row_data[static_cast<std::size_t>(r)] : outside;
    };
    std::int32_t old_row = 0;
    for (std::size_t k = 0; k < row_data.size(); ++k) {
        if (row_data[k] == m.active.stance) {
            old_row = static_cast<std::int32_t>(k);
            break;
        }
    }
    set_row_color(old_row, 0, kMercRowDefault);
    set_row_color(row, 0, kMercRowActive);
    m.active.stance = data(row);
    h.send_stance(m.active.command, m.active.stance);
}

int merc_stricmp(const std::string& a, const std::string& b);

template <typename Name>
std::int32_t merc_stance_command(const MercManager& m, const std::string& args, Name&& stance_name, MercHooks& h) {
    if (m.has_mercenary == 0) {
        h.chat("You can only switch between stances when you have a mercenary!", 15);
        return -1;
    }
    if (args.empty()) {
        h.chat("You must specify a stance name to set!", 15);
        return -1;
    }
    for (const MercStance& s : m.active.stances) {
        if (merc_stricmp(args, stance_name(static_cast<std::uint32_t>(s.stance))) == 0) {
            h.event(0x17, s.index);
            return s.index;
        }
    }
    h.chat("Your mercenary does not have stance: " + args + "!", 15);
    return -1;
}

template <typename Description>
std::vector<MercOwnedRow> merc_owned_rows(const MercManager& m, Description&& description) {
    std::vector<MercOwnedRow> rows;
    for (int k = 0; k < kMercOwnedRows; ++k) {
        MercOwnedRow r;
        if (k < static_cast<int>(m.owned.size())) {
            std::string d = description(m.owned[static_cast<std::size_t>(k)].subtype);
            r.tooltip = merc_description_summary(d);
            r.text = merc_race_and_type(d);
            r.data = k + 1;
        } else if (k < m.slot_count) {
            r.tooltip = "Empty Mercenary Slot";
            r.text = "Empty";
            r.data = 0;
        } else {
            r.tooltip = "Unlock access to this slot on the marketplace!";
            r.text = "Locked";
            r.data = -1;
        }
        if (k == m.current_index) r.marker = "X";
        rows.push_back(std::move(r));
    }
    return rows;
}

} // namespace hailstone::game
