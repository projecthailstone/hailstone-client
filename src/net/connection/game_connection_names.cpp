// GameConnection: the name change windows (CNameChangeWnd, CNameChangePetWnd, CNameChangeMercWnd, their
// commands and the rename answer's handler). The
// three are one window with its own strings, opcodes and ini key; the window models are app/lua_ui_names.cpp.

#include <format>

#include "game/slash_commands.h"
#include "net/connection/game_connection.h"

namespace hailstone::net {

namespace {

struct NameStrings {
    const char* ini_key;        // Preferences/<key> (0x6fb160 and the window's dialog handler 0x6fb6a0)
    std::uint32_t remind, open; // the prompt Open(1) / Open(0) shows (0x144a / 0x144b ...)
    std::uint32_t disable;      // the Disable Reminder question (dialog 100, 20 s)
    std::uint32_t confirm;      // the command's question (%1 the name; dialog 0x7c / 0x8c / 0x8d, 10 s)
    std::uint32_t dialog;
};
constexpr NameStrings kNames[GameConnection::kNameKinds] = {
    {"DisableNameChangeReminder", 5194, 5195, 5192, 5971, kConfirmNameChange},
    {"DisablePetNameChangeReminder", 4627, 4625, 4630, 4633, kConfirmPetNameChange},
    {"DisableMercNameChangeReminder", 4628, 4629, 4631, 4634, kConfirmMercNameChange},
};

} // namespace

void GameConnection::load_name_change_flags() {
    // The windows' constructors: CharacterIni_GetInt("Preferences", key, 0) > 0.
    for (int k = 0; k < kNameKinds; ++k)
        name_change_[static_cast<std::size_t>(k)].reminder_disabled =
            char_ini_get_ && char_ini_get_("Preferences", kNames[k].ini_key, 0) > 0;
}

void GameConnection::set_name_reminder_disabled(int kind, bool disabled) {
    // 0x6fb160 (and the dialog handler's inline copy): a change is written as "%u".
    auto& w = name_change_[static_cast<std::size_t>(kind)];
    if (w.reminder_disabled == disabled) return;
    w.reminder_disabled = disabled;
    if (char_ini_put_) char_ini_put_("Preferences", kNames[kind].ini_key, disabled ? "1" : "0");
}

void GameConnection::open_name_change(int kind, bool remind) {
    // CNameChangeWnd_Open @0x6fae40: the Disable Reminder button shown by `remind`, the first button "Remind Me
    // Later" or "Close" and the prompt to match (the window model shows them); centred and shown.
    auto& w = name_change_[static_cast<std::size_t>(kind)];
    w.remind = remind;
    w.open = true;
}

bool GameConnection::handle_name_change(const messages::GameMessage& message) {
    if (message.opcode != messages::GameOpcode::NameChangeNotice) return false;
    const auto* notice = messages::message_get<messages::NameChangeNotice>(message);
    if (!notice) return false;
    const int kind = notice->subject == messages::NameSubject::Character ? kNameCharacter :
                     notice->subject == messages::NameSubject::Pet ? kNamePet : kNameMerc;
    if (notice->prompt) {
        if (notice->immediate) set_name_reminder_disabled(kind, false);
        if (notice->immediate || !name_change_[static_cast<std::size_t>(kind)].reminder_disabled)
            open_name_change(kind, true);
        return true;
    }
    if (!notice->has_result) return true;
    const auto result = notice->result;
    struct Answer {
        std::int32_t result;
        std::uint32_t eqstr;
        bool character_only;
    };
    static constexpr Answer kAnswers[] = {
        {1, 5976, false}, {0, 5167, false}, {-1, 5977, true}, {-2, 5978, true},
        {-3, 5979, false}, {-4, 5980, false}, {-5, 5193, false},
    };
    std::uint32_t id = 0;
    for (const Answer& ans : kAnswers) {
        if (ans.result != result) continue;
        if (!ans.character_only || kind == kNameCharacter) id = ans.eqstr;
        break;
    }
    if (id) system(id, {}, 0xf);
    auto& w = name_change_[static_cast<std::size_t>(kind)];
    w.pending = false;
    if (id) {
        Confirmation c;
        c.id = kConfirmNameResult;
        c.text = strings_.format(id, std::vector<std::string>{});
        c.timeout_ms = 10000;
        c.ok_only = true;
        ask(std::move(c));
    }
    if (w.open && result == 1) w.open = false;
    return true;
}

void GameConnection::name_change_command(int kind, const std::string& args) {
    // ChangeName (name_commands.cpp): no argument opens the window (Open(0)); a name of 4..15 characters (else 5983,
    // or 5972 in colour 0xf) is cut to its first word (19 characters) and asked about (10 s); the yes is
    // answer_name_change.
    if (args.empty()) {
        open_name_change(kind, false);
        return;
    }
    if (args.size() < 4) {
        system(5983);
        return;
    }
    if (args.size() > 0xf) {
        system(5972, {}, 0xf);
        return;
    }
    std::string_view rest = args;
    std::string name = game::next_cmd_arg(rest).substr(0, 0x13);
    Confirmation c;
    c.id = kNames[kind].dialog;
    c.text = strings_.format(kNames[kind].confirm, std::vector<std::string>{name});
    c.value = name;
    c.timeout_ms = 10000;
    ask(std::move(c));
}

void GameConnection::ask_disable_name_reminder(int kind) {
    // CNameChangeWnd::WndNotification's Disable Reminder: ConfirmationDialog(window, 100, string, Yes / No, 20 s).
    Confirmation c;
    c.id = kConfirmNameReminderOff;
    c.text = strings_.format(kNames[kind].disable, std::vector<std::string>{});
    c.timeout_ms = 20000;
    c.data = static_cast<std::uint32_t>(kind);
    ask(std::move(c));
}

void GameConnection::answer_name_change(const Confirmation& c, bool yes) {
    if (!yes) return;
    if (c.id == kConfirmNameReminderOff) {
        // 0x6fb6a0 (the window's dialog handler, dialog 100): the reminder off, saved, and the window closed.
        int kind = static_cast<int>(c.data);
        if (kind < 0 || kind >= kNameKinds) return;
        set_name_reminder_disabled(kind, true);
        name_change_[static_cast<std::size_t>(kind)].open = false;
        return;
    }
    // ConfirmationDialog_HandleResponse 0x7c / 0x8c / 0x8d: the window's request pending (0x6fb130(1)), then
    // {new name[64], the character's name[64], i32 0} as OP_GMNameChange, OP_ChangePetName or 0x36db; the
    // character's rename also prints 5973.
    if (state_ != ConnectionState::InZone) return;
    int kind = c.id == kConfirmNameChange ? kNameCharacter : c.id == kConfirmPetNameChange ? kNamePet : kNameMerc;
    name_change_[static_cast<std::size_t>(kind)].pending = true;
    messages::NameChangeRequest request;
    request.subject = kind == kNameCharacter ? messages::NameSubject::Character :
                      kind == kNamePet ? messages::NameSubject::Pet : messages::NameSubject::Mercenary;
    request.requested_name = c.value;
    request.character_name = character_;
    send_zone(messages::outgoing(messages::GameOpcode::NameChangeRequest,std::move(request)));
    if (kind == kNameCharacter) system(5973, {}, 0xf);
}

} // namespace hailstone::net
