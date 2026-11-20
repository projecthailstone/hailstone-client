// GameConnection: mercenaries. The client mercenary manager (game/mercenary.h) on this connection: the
// HandleWorldMessage_Mercenary @0x569920 opcodes, the zone-in requests (CDisplay::StartWorldDisplay →
// Merc_RequestTimerAndData), the per-frame upkeep countdown (CEverQuest::DoMainLoop → 0x565930), the liaison's
// right click, the three windows' buttons and the /merc* commands.

#include <algorithm>
#include <cstring>
#include <format>

#include "core/game_dir.h"
#include "core/ini_file.h"
#include "core/log.h"
#include "game/eq_names.h"
#include "net/connection/game_connection.h"

namespace hailstone::net {

// MercHooks on the connection: chat lines, packets on the zone connection, the windows' open flags.
struct GameConnection::MercLink : game::MercHooks {
    GameConnection& c;
    explicit MercLink(GameConnection& conn) : c(conn) {}

    void chat(const std::string& text, std::int32_t color) override {
        std::string t = text;
        while (!t.empty() && t.back() == '\n') t.pop_back();
        c.chat(ChatEvent::Kind::System, std::move(t), 0, static_cast<std::uint32_t>(color));
    }
    void event(std::uint32_t id, std::int32_t argument) override {
        log::debug("mercenary event {:#x} ({})", id, argument);
        if (id == 0xd) ++c.merc_list_serial_;
        if (id == 0x17) {
            // /stance's event (0x5671d0): the pair whose +0 is the argument becomes the stance, as the stance
            // list's choice does (Send_OP_MercenaryCommand with the pair's stance).
            std::vector<std::uint32_t> rows;
            std::int32_t row = -1;
            for (const auto& s : c.merc_.active.stances) {
                if (s.index == argument && row < 0) row = static_cast<std::int32_t>(rows.size());
                rows.push_back(static_cast<std::uint32_t>(s.stance));
            }
            if (row >= 0) game::merc_send_command(c.merc_, rows, 0, row, [](std::int32_t, std::int32_t, std::uint32_t) {}, *this);
        }
    }
    void send(const messages::MercenaryRequest& req) override {
        c.send_zone(messages::outgoing(messages::GameOpcode::MercenaryRequest,req));
    }
    void send_stance(std::uint32_t command, std::uint32_t stance) override {
        messages::MercenaryRequest req;
        req.action=messages::MercenaryAction::SetStance;req.command=command;req.stance=stance;
        send(req);
    }
    void debug_log(const std::string& text) override { log::debug("mercenary: {}", text); }
    bool find_window(const char*) override { return true; } // the stock windows always exist
    void hide_window(const char* screen) override {
        std::string_view s = screen;
        c.merc_windows_[s == "MMTW_MerchantWnd" ? 0 : s == "MMGW_ManageWnd" ? 1 : 2] = false;
    }
    void store_window_visibility(const char*) override {} // the Lua layout stores shown states itself
    std::string currency(bool plural) const {
        // CDBStr kind 0x11 (one) or 0x12 (more) of the alternate currency (0x7f7e70's lookup is taken as the id).
        if (c.merc_.active.alt_currency == 0) return {};
        return c.strings_.db_string(c.merc_.active.alt_currency, plural ? 0x12 : 0x11);
    }
    void report_upkeep_charged(std::uint8_t alt) override {
        chat(game::merc_upkeep_charged_text(c.merc_, alt != 0, currency(c.merc_.active.alt_upkeep_cost >= 2)), -1);
    }
    void report_upkeep_waived() override {
        chat(game::merc_upkeep_waived_text(c.merc_, currency(c.merc_.active.alt_upkeep_cost >= 2)), -1);
    }
    std::uint32_t tick_count() override { return GameConnection::eq_time(); }
    // Util::FastTime: seconds (the switch adds its seconds to it).
    std::uint32_t fast_time() override { return static_cast<std::uint32_t>(GameConnection::now_ms() / 1000); }
    void confirm(std::uint32_t eqstr, std::int32_t, std::int32_t) override {
        Confirmation q;
        q.id = kConfirmMercDismiss;
        q.text = c.strings_.format(eqstr, std::vector<std::string>{});
        c.ask(std::move(q)); // queued behind an open question, as every other one is
    }
};

void GameConnection::with_merc(const std::function<void(game::MercManager&, game::MercHooks&)>& fn) {
    if (state_ != ConnectionState::InZone) return;
    MercLink link(*this);
    fn(merc_, link);
}

void GameConnection::merc_zone_in() {
    MercLink link(*this);
    game::merc_request_timer_and_data(merc_, link);
}

void GameConnection::update_merc() {
    // 0x565930 every frame: the upkeep countdown.
    game::merc_tick_upkeep(merc_, eq_time());
}

bool GameConnection::handle_merc(const messages::GameMessage& message) {
    if (message.opcode != messages::GameOpcode::MercenaryNotice) return false;
    const auto* notice=messages::message_get<messages::MercenaryNotice>(message);
    if (!notice) return false;
    MercLink hooks(*this);
    switch(notice->kind) {
#define HAILSTONE_MERC_CASE(kind, fn) case messages::MercenaryNoticeKind::kind: game::merc_handle_##fn(merc_,*notice,hooks);break
        HAILSTONE_MERC_CASE(MerchantList,data_response);
        HAILSTONE_MERC_CASE(OwnedList,data_update);
        HAILSTONE_MERC_CASE(HireResult,hire);
        HAILSTONE_MERC_CASE(Timer,timer);
        HAILSTONE_MERC_CASE(Suspended,suspend_response);
        HAILSTONE_MERC_CASE(Unsuspended,unsuspend_response);
        HAILSTONE_MERC_CASE(SuspensionNotice,suspended);
        HAILSTONE_MERC_CASE(Died,died);
        HAILSTONE_MERC_CASE(TimerCleared,timer_cleared);
        HAILSTONE_MERC_CASE(Event7,event7);
        HAILSTONE_MERC_CASE(Unknown1,unknown1);
        HAILSTONE_MERC_CASE(TimeLeft,time_left);
        HAILSTONE_MERC_CASE(UpkeepCharged,upkeep_charged);
        HAILSTONE_MERC_CASE(Costs,costs);
        HAILSTONE_MERC_CASE(Command,command);
        HAILSTONE_MERC_CASE(SwitchResult,switch);
        HAILSTONE_MERC_CASE(Dismissed,dismiss);
#undef HAILSTONE_MERC_CASE
    }
    return true;
}

bool GameConnection::open_merc_liaison(std::uint32_t npc_id) {
    if (state_ != ConnectionState::InZone || pstatus_.dead) return false;
    auto it = world_.spawns.find(npc_id);
    const game::Spawn* me = world_.player();
    if (it == world_.spawns.end() || !me || it->second.class_id != game::kMercLiaisonClass || it->second.is_corpse)
        return false;
    glm::vec3 at = motion_known_ ? position_ : me->position;
    if (glm::distance(at, it->second.position) >= game::kMercLiaisonRange) return false;
    // 0x5649e0: the liaison's id; the merchant window shows.
    merc_.liaison_id = npc_id;
    set_merc_window_open(0, true);
    return true;
}

void GameConnection::set_merc_window_open(int which, bool open) {
    if (which < 0 || which > 2) return;
    bool was = merc_windows_[static_cast<std::size_t>(which)];
    merc_windows_[static_cast<std::size_t>(which)] = open;
    if (which == 0 && open) {
        // 0x6eca10, the merchant window's show handler: the saved liaison id comes back (0x564a00), then
        // OP_MercenaryDataRequest asks for the list.
        if (merc_.liaison_id == 0) merc_.liaison_id = merc_.saved_liaison_id;
        with_merc([](game::MercManager& m, game::MercHooks& h) { game::merc_send_data_request(m, h); });
    } else if (which == 0 && was && !open) {
        merc_.saved_liaison_id = merc_.liaison_id; // 0x5649f0
        merc_.liaison_id = 0;
    }
}

void GameConnection::merc_windows_command() {
    MercLink link(*this);
    if (!game::merc_windows_allowed(merc_, link)) return;
    set_merc_window_open(1, !merc_windows_[1]);
}

void GameConnection::merc_select_entry(std::size_t index) {
    if (index < merc_.merchant_list.size()) merc_.selected = merc_.merchant_list[index];
}

void GameConnection::merc_hire(std::uint32_t merc_id) {
    with_merc([merc_id](game::MercManager& m, game::MercHooks& h) { game::merc_send_hire(m, merc_id, h); });
}

void GameConnection::merc_dismiss() {
    // CMercenaryManageWnd's Dismiss: Options/DismissMercenaryConfirm (0xde0d2c, loadOptions' ReadClientINIBool, on
    // by default) asks first.
    bool confirm = client_option(game::ClientOption::DismissMercenaryConfirm);
    with_merc([confirm](game::MercManager& m, game::MercHooks& h) { game::merc_send_dismiss(m, confirm, true, h); });
}

void GameConnection::merc_set_assist(bool on) {
    with_merc([on](game::MercManager& m, game::MercHooks& h) { game::merc_send_assist_mode(m, on ? 1 : 0, h); });
}

void GameConnection::merc_switch(std::int32_t index) {
    with_merc([index](game::MercManager& m, game::MercHooks& h) { game::merc_send_switch(m, index, h); });
}

void GameConnection::merc_set_owner() {
    std::uint32_t target = world_.target_id, me = world_.player_id;
    int kind = target == 0 || !world_.spawns.count(target) ? 0 : target == me ? 1 : 2;
    with_merc([&](game::MercManager& m, game::MercHooks& h) { game::merc_request_set_owner(m, kind, me, target, h); });
}

std::vector<std::pair<std::int32_t, std::uint32_t>> GameConnection::merc_choose_stance(
    const std::vector<std::uint32_t>& row_data, std::int32_t row) {
    std::vector<std::pair<std::int32_t, std::uint32_t>> colors;
    with_merc([&](game::MercManager& m, game::MercHooks& h) {
        game::merc_send_command(m, row_data, 0, row,
                                [&](std::int32_t r, std::int32_t, std::uint32_t argb) { colors.emplace_back(r, argb); }, h);
    });
    return colors;
}

bool GameConnection::merc_command(std::string_view handler, const std::string& args) {
    MercLink link(*this);
    if (handler == "Cmd_mercwindows") {
        merc_windows_command();
        return true;
    }
    if (handler == "Cmd_stance") {
        game::merc_stance_command(merc_, args, [&](std::uint32_t stance) { return strings_.db_string(stance, 24); }, link);
        return true;
    }
    if (handler == "Cmd_mercassist") {
        game::merc_assist_command(merc_, args, link);
        return true;
    }
    if (handler == "Cmd_mercswitch") {
        game::merc_switch_command(merc_, args, link);
        return true;
    }
    if (handler == "Cmd_merclog") {
        // Cmd_merclog @0x478a30: the line, then opcode 0xee80 with no payload.
        link.chat("Generating mercenary logfile on the server.", -1);
        link.send({messages::MercenaryAction::RequestServerLog});
        return true;
    }
    return false;
}

} // namespace hailstone::net
