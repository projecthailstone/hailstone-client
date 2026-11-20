// GameConnection: the original's client, help and report commands the connection keeps:
// /help, /autojoin (and the chat server's auto join), /uptime, /servertransfer, /report,
// /vreport, /feedback's window, /guidehelp, /veteranReward, /acompare, /aclearcompare, /lfguild and /paperdoll. The
// ones that act on the app (/channel, /url, /www, /outputfile) are handed to it (set_client_commands;
// app/client_app_commands.cpp).

#include "game/coords.h"
#include "game/option_commands.h"
#include "game/slash_commands.h"
#include "net/connection/chat_client.h"
#include "net/connection/game_connection.h"

namespace hailstone::net {

bool GameConnection::client_info_command(std::string_view h, const std::string& args) {
    if (h == "Cmd_help") {
        help_command(args);
        return true;
    }
    if (h == "Cmd_autojoin") {
        // Cmd_autojoin @0x4ebde0: the channels go to the UI ini's ChatManager / ChannelAutoJoin (CEverQuest::
        // WriteIniString) for the next chat server connection; no argument: the three usage lines. Colour 0x111.
        if (args.empty()) {
            show_chat("Usage: /autojoin <channel name>", 0x111);
            show_chat("   or  /autojoin <channel name>:<password>", 0x111);
            show_chat("You can also specify multiple channels using commas (e.g. /autojoin <channel name>,<channel name>).",
                      0x111);
            return true;
        }
        if (ui_ini_put_) ui_ini_put_("ChatManager", "ChannelAutoJoin", args);
        show_chat("Set channels to be joined at login.", 0x111);
        return true;
    }
    if (h == "Cmd_realestate") {
        real_estate_command(args); // net/connection/game_connection_real_estate.cpp
        return true;
    }
    if (h == "Cmd_leaverealestate") {
        // Cmd_leaverealestate @0x4df640: opcode 0x1da9 with 4 bytes the original leaves uninitialised (zero here).
        if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(messages::GameOpcode::ReLeave));
        return true;
    }
    if (h == "Cmd_uptime") {
        // Cmd_uptime @0x4785b0: opcode 0x344f alone; the answer (decode_uptime) says how long the world and the zone
        // have been running. EQEmu has no such opcode and does not answer.
        if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(messages::GameOpcode::UptimeRequest));
        return true;
    }
    if (h == "Cmd_servertransfer") {
        // Cmd_servertransfer @0x4e82d0: opcode 0x2b8e alone (the server list request of the server transfer; the
        // answer, 0x5824, is the server list window's). EQEmu has no such opcode and does not answer.
        if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(messages::GameOpcode::ServerTransferRequest));
        return true;
    }
    if (h == "Cmd_report") {
        report_command(args);
        return true;
    }
    if (h == "Cmd_vreport") {
        // Cmd_vreport @0x4fb700: the voice chat manager's report, only while voice chat (Vivox) is up; this client
        // has no voice chat, so nothing.
        return true;
    }
    if (h == "Cmd_feedback") {
        // Cmd_feedback @0x4e7320: the feedback window's Activate (vtable +0x90).
        feedback_open_ = true;
        return true;
    }
    if (h == "Cmd_guidehelp") {
        // Cmd_guidehelp @0x4e4f70: no text: 3555 (colour 0xf); else Cmd_guidehelp_SendRequest @0x52b530(guide):
        // OP_Petition with the text (Send_OP_Petition) and 3560 "Your request for guidehelp, %1" (colour 0).
        if (args.empty()) return system(3555, {}, 0xf), true;
        send_zone(messages::outgoing(messages::GameOpcode::Petition, args));
        system(3560, {args});
        return true;
    }
    if (h == "Cmd_veteranReward") {
        // Cmd_veteranReward @0x4e5c90: dialog 0x7e with 3599 (60 s); answer_confirmation answers it.
        Confirmation c;
        c.id = kConfirmVeteranReward;
        c.text = strings_.format(3599, {});
        c.timeout_ms = 60000;
        ask(std::move(c));
        return true;
    }
    if (h == "Cmd_achievements") {
        // Cmd_achievements @0x4edb00: hide a visible window; otherwise request
        // the server snapshot with 0x47cb / byte 0x30. No HP/death gate.
        show_achievements(!achievements_.shown());
        return true;
    }
    if (h == "Cmd_acompare") {
        // Cmd_acompare @0x4e7c40: a target (5203), within 20 (CDisplay::PlayerDistance, 5204), a player (5205), each
        // refusal colour 0x10e; then Send_AchievementCompareRequest @0x51fb90.
        auto it = world_.spawns.find(world_.target_id);
        const game::Spawn* me = world_.player();
        if (it == world_.spawns.end() || !me) return system(5203, {}, 0x10e), true;
        if (distance_to(world_.target_id) > 20.0f) return system(5204, {}, 0x10e), true;
        if (it->second.is_npc || it->second.is_corpse) return system(5205, {}, 0x10e), true;
        compare_achievements();
        return true;
    }
    if (h == "Cmd_aclearcompare") {
        // Cmd_aclearcompare @0x4e7ce0: AchievementManager::ResetComparisonStates.
        clear_achievement_comparison();
        return true;
    }
    if (h == "Cmd_lfguild") {
        // Cmd_lfguild @0x4e53f0: the Looking for Guild window's Activate (vtable +0x90).
        set_lfguild_open(true);
        return true;
    }
    if (h == "Cmd_paperdoll") return true; // Cmd_paperdoll @0x4ecff0 does nothing
    return false;
}

void GameConnection::help_command(const std::string& args) {
    // Cmd_help @0x4f4520: a class (13120..13124 normal, emote, guild, voice, chat; case ignored) lists the table's
    // commands of that /help category (1, 2, 3, 4, 6) after 13131 and a rule, each "  %s" with its localised name, else
    // its own; anything else gives the help text 13125..13130. Colour 0x111.
    struct HelpTopic {
        std::uint32_t eqstr;
        int category;
    };
    static constexpr HelpTopic kTopics[] = {{13120, 1}, {13121, 2}, {13122, 3}, {13123, 4}, {13124, 6}};
    const HelpTopic* topic = nullptr;
    for (const HelpTopic& t : kTopics) {
        if (!game::same_no_case(args, strings_.format(t.eqstr, {}))) continue;
        topic = &t;
        break;
    }
    if (!topic) {
        for (std::uint32_t id = 13125; id <= 13130; ++id) system(id, {}, 0x111);
        return;
    }
    system(13131, {}, 0x111);
    show_chat(std::string(80, '-'), 0x111);
    for (const auto& c : game::slash_commands()) {
        if (c.category != topic->category) continue;
        std::string name = c.name;
        if (c.eqstr >= 0) {
            // LoadStringTables @0x51f14d: the entry's localised name.
            std::string s = strings_.format(static_cast<std::uint32_t>(c.eqstr), {});
            if (!s.empty() && s[0] != '[') name = s;
        }
        show_chat("  " + name, 0x111);
    }
}

void GameConnection::report_command(const std::string& args) {
    // Cmd_report @0x4eff50: no player: 12943; once per zone-in (0xddf640: 1, then 2; else 12945). The report is
    // "<player>|<our name>|<GetAllVisibleText>" cut at 0x2004 characters, as OP_Report; then 12944. Colour 0xf.
    if (args.empty()) return system(12943, {}, 0xf);
    if (report_state_ != 1) return system(12945, {}, 0xf);
    report_state_ = 2;
    std::string text = args + "|" + character_ + "|" + (visible_chat_text_ ? visible_chat_text_() : std::string());
    if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(messages::GameOpcode::Report, std::move(text)));
    system(12944, {args}, 0xf);
}

void GameConnection::send_feedback(const std::string& text) {
    // CFeedbackWnd::WndNotification @0x667830, the Send button: with text, OP_Feedback (the name, the position and
    // heading, the text) and the text cleared; the window closes either way (Deactivate).
    const game::Spawn* me = world_.player();
    if (!text.empty() && me && state_ == ConnectionState::InZone) {
        Feedback f;
        f.name = character_;
        glm::vec3 at = game::world_to_server(motion_known_ ? position_ : me->position);
        f.y = at.y;
        f.x = at.x;
        f.z = at.z;
        f.heading = static_cast<std::int32_t>(me->heading);
        f.text = text;
        send_zone(messages::outgoing(messages::GameOpcode::Feedback, f));
    }
    feedback_open_ = false;
}

void GameConnection::autojoin_channels() {
    // CEverQuest::ChatServerGiveTime @0x5382d0: once the chat server is connected (and once per connect), the UI ini's
    // ChatManager / ChannelAutoJoin; when set, "Autojoining channels..." (colour 0x122) and ";join <channels>" to the
    // chat server. (Its default, eqclient.ini's Defaults / ChannelAutoJoin, has no counterpart in hailstone.ini.)
    if (autojoined_ || !chat_server_ || !chat_server_->connected()) return;
    autojoined_ = true;
    auto channels = ui_ini_get_ ? ui_ini_get_("ChatManager", "ChannelAutoJoin") : std::nullopt;
    if (!channels || channels->empty()) return;
    show_chat("Autojoining channels...", 0x122);
    chat_server_->send_command("join " + *channels);
}

void GameConnection::show_with_line_breaks(const std::string& text) {
    // DisplayChatWithLineBreaks @0x478090: a line ends at a newline or "\n" (both dropped) or after 0x200 characters
    // (the next character is dropped); "\\" is one backslash; each line through dsp_chat_2.
    std::string line;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (line.size() >= 0x200 || c == '\n') {
            show_chat(std::exchange(line, {}));
        } else if (c == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
            show_chat(std::exchange(line, {}));
            ++i;
        } else if (c == '\\' && i + 1 < text.size() && text[i + 1] == '\\') {
            line += '\\';
            ++i;
        } else {
            line += c;
        }
    }
    if (!line.empty()) show_chat(line);
}

} // namespace hailstone::net
