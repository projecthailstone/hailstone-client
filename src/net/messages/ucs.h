#pragma once

// Service-domain UCS commands and events. No wire application packets or
// numeric UCS opcode declarations are present in this model.
#include <cstdint>
#include <string>
#include <vector>

#include "game/mail.h"
#include "net/messages/world.h"

namespace hailstone::net {

struct MailRequest {
    enum class Action : std::uint8_t { Forwarding, SelectMailbox, Headers, Body, Status, Send };
    Action action = Action::Headers;
    bool forwarding = false;
    std::int32_t mailbox = 0;
    std::uint32_t message_id = 0;
    game::MailStatus status = game::MailStatus::New;
    std::vector<std::uint32_t> ids;
    std::string command; // MailMgr's already-expanded recipient/subject/body text for Send
};

struct ChatServerEvent {
    enum class Kind : std::uint8_t {
        Message, Channels, Buddy, Ignore, AnnounceJoin, AnnounceLeave,
        // Mail (EQEmu ucs/clientlist.cpp and ucs/database.cpp; mail_opcodes.conf):
        MailboxList,    // OP_MailLogin back: fields = the mailbox addresses
        MailHeaderCount,// fields = mailbox, a constant, the success flag ("1"), the count
        MailHeader,     // fields = mailbox, a constant, the row index, id, time, status, from, subject
        MailBody,       // fields = mailbox, id, body, "1" (fetched), then the recipients ("TO:" line)
        MailNew,        // fields = mailbox, id, time, status, from, subject
        MailDelivery,   // fields = a sequence, the recipient (failure), subject, "1" sent / "0" failed
        MailboxChange,  // fields = the new mailbox number
        Disconnected,  // the authenticated state and per-session lists have been retired
    };
    Kind kind = Kind::Message;
    std::string channel; // Message: "" for the server's own text
    std::string sender;  // Message: "short.Name"
    std::string text;    // Message text; Channels: the list; Buddy/Ignore: the name
    bool added = false;  // Buddy / Ignore
    bool accepted = false; // MailboxList: the server's authentication result
    std::vector<std::string> fields; // the mail packets' NUL-separated strings
};

namespace messages {

enum class UcsCommandKind : std::uint8_t { Login, ChatText, MailText, MailRequest };

struct UcsCommand {
    UcsCommandKind kind = UcsCommandKind::ChatText;
    ChatServerInfo identity; // only for the one-shot login intent
    std::string text;
    MailRequest mail;
};

} // namespace messages

} // namespace hailstone::net
