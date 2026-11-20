// GameConnection: expeditions over EQEmu's packets (net/protocol/rof2/packets/dz_packets.h). The server owns the expedition: the
// leader's commands go to it as typed and its answers (the info, member list and leader packets, and its own chat
// lines) update the window (app/lua_ui_dz.cpp). Its expiry warning comes with the server's own chat line.

#include <algorithm>
#include <cctype>

#include "core/log.h"
#include "game/coords.h"
#include "game/slash_commands.h"
#include "net/connection/game_connection.h"

namespace hailstone::net {

namespace {

bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

} // namespace

bool GameConnection::dz_is_leader() const {
    return expedition_.info.assigned && !expedition_.info.leader.empty() &&
           same_name(expedition_.info.leader, character_);
}

std::vector<glm::vec3> GameConnection::compass_points() const {
    std::vector<glm::vec3> v;
    for (const auto& c : expedition_.compass) v.push_back(c.at);
    // EQEmu sets the compass only when the adventure's way in is in this zone; the packet's x, y are already
    // zone-file order (it swaps the server's).
    if (adventure_.data.assigned && adventure_.data.compass) v.push_back({adventure_.data.x, adventure_.data.y, adventure_.data.z});
    if (path_.active() && !path_.points().empty()) v.push_back(path_.points().front()); // the path's destination end
    if (task_compass_) v.push_back(task_compass_->position); // the task NPC (0x76ff70)
    return v;
}

void GameConnection::dz_add_player(const std::string& name) {
    if (state_ == ConnectionState::InZone && !name.empty()) send_zone(messages::outgoing(messages::GameOpcode::DzAddPlayer,name));
}

void GameConnection::dz_remove_player(const std::string& name) {
    if (state_ == ConnectionState::InZone && !name.empty()) send_zone(messages::outgoing(messages::GameOpcode::DzRemovePlayer,name));
}

void GameConnection::dz_make_leader(const std::string& name) {
    if (state_ == ConnectionState::InZone && !name.empty()) send_zone(messages::outgoing(messages::GameOpcode::DzMakeLeader,name));
}

void GameConnection::dz_swap_player(const std::string& add, const std::string& remove) {
    if (state_ == ConnectionState::InZone && !add.empty() && !remove.empty()) send_zone(messages::outgoing(messages::GameOpcode::DzSwapPlayer,DzSwapIntent{add,remove}));
}

void GameConnection::dz_quit() {
    if (state_ == ConnectionState::InZone && expedition_.info.assigned) send_zone(messages::outgoing(messages::GameOpcode::DzQuit));
}

void GameConnection::dz_list_timers() {
    if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(messages::GameOpcode::DzListTimers));
}

void GameConnection::dz_player_list() {
    if (state_ == ConnectionState::InZone && expedition_.info.assigned) send_zone(messages::outgoing(messages::GameOpcode::DzPlayerList));
}

void GameConnection::dz_choose_zone(int index) {
    auto& c = expedition_.choices;
    if (index >= 0 && index < static_cast<int>(c.size()) && state_ == ConnectionState::InZone) {
        if (!submit_zone_action(messages::outgoing(messages::GameOpcode::DzChooseZoneReply,
                                                   c[static_cast<std::size_t>(index)]),
                                "Choose expedition zone")) return;
        c.clear();
    }
}

void GameConnection::answer_dz_invite(bool accept) {
    if (!expedition_.invite) return;
    if (state_ != ConnectionState::InZone) return;
    DzInvite inv = *expedition_.invite;
    if (!submit_zone_action(messages::outgoing(messages::GameOpcode::DzExpeditionInviteResponse,
                                               DzInviteReply{inv,accept}),
                            "Answer expedition invitation")) return;
    expedition_.invite.reset();
}

template<class T> static std::optional<T> dz_value(const messages::GameMessage& message) {
    if (const auto* value=messages::message_get<T>(message)) return *value;
    return std::nullopt;
}

void GameConnection::handle_dz(const messages::GameMessage& message) {
    const auto op=message.opcode;
    auto& e = expedition_;
    switch (op) {
    case messages::GameOpcode::DzExpeditionInfo:
        if (auto v = dz_value<DzInfo>(message)) {
            bool fresh = v->assigned && !e.info.assigned;
            e.info = std::move(*v);
            if (!e.info.assigned) {
                e.members.clear();
                e.window_open = false;
            } else if (fresh) {
                e.window_open = true; // the window shows when an expedition is assigned
            }
        }
        break;
    case messages::GameOpcode::DzExpeditionInvite:
        if (auto v = dz_value<DzInvite>(message)) {
            if (e.invite) send_zone(messages::outgoing(messages::GameOpcode::DzExpeditionInviteResponse,DzInviteReply{*e.invite,false})); // a newer one replaces it
            std::string text = v->inviter + " invites you to join the expedition " + v->expedition; // this client's line
            if (v->swapping && !v->swap_name.empty()) text += ", replacing " + v->swap_name;
            text += ". Join?";
            e.invite = std::move(*v);
            ask(Confirmation{kConfirmDzInvite, std::move(text), {}});
        }
        break;
    case messages::GameOpcode::DzExpeditionLockoutTimers:
        if (auto v = dz_value<std::vector<DzLockout>>(message)) {
            e.lockouts = std::move(*v);
            e.lockouts_at = now_ms();
        }
        break;
    case messages::GameOpcode::DzMemberList:
        if (auto v = dz_value<std::vector<DzMember>>(message)) e.members = std::move(*v);
        break;
    case messages::GameOpcode::DzMemberListStatus:
        if (auto v = dz_value<std::vector<DzMember>>(message))
            for (auto& m : *v) {
                auto it = std::find_if(e.members.begin(), e.members.end(),
                                       [&](const DzMember& x) { return same_name(x.name, m.name); });
                if (it != e.members.end()) it->status = m.status;
                else if (e.members.size() < kDzMembersMax) e.members.push_back(std::move(m));
            }
        break;
    case messages::GameOpcode::DzMemberListName:
        if (auto v = dz_value<DzMemberName>(message)) {
            auto it = std::find_if(e.members.begin(), e.members.end(),
                                   [&](const DzMember& x) { return same_name(x.name, v->name); });
            if (v->add && it == e.members.end() && e.members.size() < kDzMembersMax)
                e.members.push_back(DzMember{v->name, DzMemberStatus::Unknown});
            else if (!v->add && it != e.members.end()) e.members.erase(it);
        }
        break;
    case messages::GameOpcode::DzSetLeaderName:
        if (auto v = dz_value<std::string>(message)) e.info.leader = std::move(*v);
        break;
    case messages::GameOpcode::DzExpeditionEndsWarning:
        // EQEmu sends its own chat line (DZ_MINUTES_REMAIN) with it; nothing else to show.
        if (auto v = dz_value<std::uint32_t>(message)) log::debug("dz: {} minutes left", *v);
        break;
    case messages::GameOpcode::DzChooseZone:
        if (auto v = dz_value<std::vector<DzChoice>>(message)) e.choices = std::move(*v);
        break;
    case messages::GameOpcode::DzCompass:
        if (auto v = dz_value<std::vector<DzCompassPoint>>(message)) e.compass = std::move(*v);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- commands

bool GameConnection::dz_command(std::string_view h, const std::string& args) {
    if (state_ != ConnectionState::InZone) return false;
    std::string_view rest = args;
    if (h == "Cmd_dzhelp") {
        // Cmd_dzhelp @0x4ec890: seven help lines.
        for (std::uint32_t id : {0xdd1u, 0xdd0u, 0xdcfu, 0xdcbu, 0xdccu, 0xdcdu, 0xdceu}) system(id);
        return true;
    }
    // The name commands take the first word (63 characters), an empty one too: their usage lines (0xdcb..0xdce) only
    // show for a null argument, which InterpretCmd never passes.
    if (h == "Send_OP_DzAddPlayer") { send_zone(messages::outgoing(messages::GameOpcode::DzAddPlayer,game::next_cmd_arg(rest).substr(0,63))); return true; }
    if (h == "Cmd_dzremoveplayer") { send_zone(messages::outgoing(messages::GameOpcode::DzRemovePlayer,game::next_cmd_arg(rest).substr(0,63))); return true; }
    if (h == "Cmd_dzmakeleader") { send_zone(messages::outgoing(messages::GameOpcode::DzMakeLeader,game::next_cmd_arg(rest).substr(0,63))); return true; }
    if (h == "Cmd_dzswapplayer") {
        // Cmd_dzswapplayer @0x4ec4c0: the first word is the member to remove, the second the one to add.
        std::string remove = game::next_cmd_arg(rest).substr(0, 63);
        std::string add = game::next_cmd_arg(rest).substr(0, 63);
        send_zone(messages::outgoing(messages::GameOpcode::DzSwapPlayer,DzSwapIntent{add,remove}));
        return true;
    }
    if (h == "Cmd_dzplayerlist") { send_zone(messages::outgoing(messages::GameOpcode::DzPlayerList)); return true; }
    if (h == "Send_OP_DzListTimers") { send_zone(messages::outgoing(messages::GameOpcode::DzListTimers)); return true; }
    if (h == "Send_OP_DzQuit_004ec7f0") { send_zone(messages::outgoing(messages::GameOpcode::DzQuit)); return true; }
    if (h == "Cmd_compareexpedraid") {
        // Cmd_compareexpedraid @0x4eda40: the expedition's members not in the raid, then the raid's members not in
        // the expedition (the exe's own English lines).
        for (const auto& m : expedition_.members)
            if (!raid_.is_member(m.name))
                chat(ChatEvent::Kind::System, m.name + " is in your current expedition but not in your raid.");
        for (const auto& r : raid_.members)
            if (std::none_of(expedition_.members.begin(), expedition_.members.end(),
                             [&](const DzMember& m) { return same_name(m.name, r.name); }))
                chat(ChatEvent::Kind::System, r.name + " is not in your current expedition but is in your raid.");
        return true;
    }
    return false;
}

} // namespace hailstone::net
