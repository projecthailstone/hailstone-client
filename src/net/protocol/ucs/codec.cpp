#include "net/protocol/ucs/codec.h"
#include "net/protocol/ucs/opcodes.h"
#include "net/wire/byte_codec.h"
#include "core/byte_io.h"
#include <algorithm>
#include <format>
#include <stdexcept>

namespace hailstone::net {
namespace {
std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.find(sep, start);
        if (end == std::string_view::npos) end = s.size();
        out.emplace_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

AppPacket text_packet(std::uint16_t op, std::initializer_list<std::string_view> strings) {
    ByteWriter w;
    w.le<std::uint8_t>(0); // EQEmu's UCS skips the first byte of OP_MailLogin and OP_Mail
    for (auto s : strings) detail::write_cstring(w, s);
    return AppPacket(op, w.take());
}

} // namespace

AppPacket encode_mail_login(const ChatServerInfo& info) {
    std::string key = std::string(1, info.connection_type) + info.mail_key;
    return text_packet(ucs::kMailLogin, {"SOE.EQ." + info.server_short_name + "." + info.character, key});
}

AppPacket encode_mail_command(const std::string& text) { return text_packet(ucs::kMail, {text}); }

AppPacket encode_mail_bracket_command(const std::string& text) {
    ByteWriter w;
    std::string_view t = text;
    if (!t.empty() && t.front() == '[') {
        w.le<std::uint8_t>(';');
        t.remove_prefix(1);
    }
    detail::write_cstring(w, t);
    w.le<std::uint8_t>(0); // the empty argument string
    return AppPacket(ucs::kMail, w.take());
}

AppPacket encode_ucs_strings(std::uint16_t op, const std::vector<std::string>& strings) {
    ByteWriter w;
    for (const auto& s : strings) detail::write_cstring(w, s);
    return AppPacket(op, w.take());
}

std::optional<ChatServerEvent> decode_chat_server_packet(const AppPacket& p) {
    return detail::decode_with(p, [&](ByteReader& r) -> ChatServerEvent {
        ChatServerEvent e;
        switch (p.opcode) {
        case ucs::kChannelMessage: {
            e.kind = ChatServerEvent::Kind::Message;
            e.channel = r.cstring();
            e.sender = r.cstring();
            e.text = r.cstring();
            if (r.remaining()) e.fields.push_back(r.cstring()); // what follows ("SPAM:0:")
            return e;
        }
        case ucs::kMail:
            e.kind = ChatServerEvent::Kind::Channels;
            e.text = r.remaining() ? r.fixed_string(r.remaining()) : std::string();
            return e;
        case ucs::kBuddy:
            e.kind = ChatServerEvent::Kind::Buddy;
            e.added = r.le<std::uint8_t>() == 1;
            e.text = r.cstring();
            return e;
        case ucs::kIgnore:
            e.kind = ChatServerEvent::Kind::Ignore;
            e.added = r.le<std::uint8_t>() == 0;
            e.text = r.cstring();
            return e;
        case ucs::kMailLogin: {
            // Client::SendMailBoxes: u8 1, u32 count, u32 0, the comma list, a NUL.
            e.kind = ChatServerEvent::Kind::MailboxList;
            e.accepted = r.le<std::uint8_t>() != 0;
            if (!e.accepted) return e;
            auto count = r.le<std::uint32_t>();
            r.le<std::uint32_t>();
            std::string list = r.cstring();
            if (count > 128) throw std::out_of_range("too many UCS mailboxes");
            if (!list.empty()) {
                for (auto& m : split(list, ',')) {
                    auto name = trim(m);
                    if (name.empty()) throw std::out_of_range("empty UCS mailbox");
                    e.fields.push_back(std::move(name));
                }
            }
            if (e.fields.size() != count) throw std::out_of_range("UCS mailbox count does not match the list");
            return e;
        }
        case ucs::kMailHeaderCount:
        case ucs::kMailHeader:
        case ucs::kMailSendBody:
        case ucs::kMailNew:
        case ucs::kMailDeliveryStatus:
        case ucs::kMailboxChange: {
            e.kind = p.opcode == ucs::kMailHeaderCount     ? ChatServerEvent::Kind::MailHeaderCount
                     : p.opcode == ucs::kMailHeader        ? ChatServerEvent::Kind::MailHeader
                     : p.opcode == ucs::kMailSendBody      ? ChatServerEvent::Kind::MailBody
                     : p.opcode == ucs::kMailNew           ? ChatServerEvent::Kind::MailNew
                     : p.opcode == ucs::kMailDeliveryStatus ? ChatServerEvent::Kind::MailDelivery
                                                           : ChatServerEvent::Kind::MailboxChange;
            // NUL-separated strings; the last one may lack its NUL (the body's "TO:" line).
            while (!r.empty()) {
                std::string f;
                while (!r.empty()) {
                    auto c = static_cast<char>(r.le<std::uint8_t>());
                    if (c == 0) break;
                    f.push_back(c);
                }
                e.fields.push_back(std::move(f));
            }
            return e;
        }
        case ucs::kAnnounceJoin:
        case ucs::kAnnounceLeave:
            e.kind = p.opcode == ucs::kAnnounceJoin ? ChatServerEvent::Kind::AnnounceJoin
                                                    : ChatServerEvent::Kind::AnnounceLeave;
            e.channel = r.cstring();
            e.sender = r.cstring();
            return e;
        default:
            throw std::out_of_range("unhandled chat server opcode");
        }
    });
}

namespace ucs {

AppPacket Codec::encode(const messages::UcsCommand& command) const {
    using Kind = messages::UcsCommandKind;
    if (command.kind == Kind::ChatText || command.kind == Kind::MailText) {
        if (command.text.find('\0') != std::string::npos)
            throw std::invalid_argument("NUL in UCS command");
    }
    switch (command.kind) {
    case Kind::Login: return encode_mail_login(command.identity);
    case Kind::ChatText: return encode_mail_command(command.text);
    case Kind::MailText: return encode_mail_bracket_command(command.text);
    case Kind::MailRequest: {
        // The UCS codec owns the MailMgr's bracket command grammar.
        const auto& req = command.mail;
        using Action = MailRequest::Action;
        std::string text;
        switch (req.action) {
        case Action::Forwarding: text = std::format("[setmailforwarding {}", req.forwarding ? 1 : 0); break;
        case Action::SelectMailbox: text = std::format("[selectmailbox {}", req.mailbox); break;
        case Action::Headers: text = "[getheaders"; break;
        case Action::Body: text = std::format("[getbody {}", req.message_id); break;
        case Action::Status:
            text = std::string("[setmessagestatus ") + game::mail_status_word(req.status);
            for (const auto id : req.ids) text += std::format(" {}", id);
            break;
        case Action::Send: text = req.command; break;
        }
        if (text.find('\0') != std::string::npos) throw std::invalid_argument("NUL in UCS mail command");
        return encode_mail_bracket_command(text);
    }
    }
    throw std::invalid_argument("unknown UCS semantic command");
}

messages::CodecResult<ChatServerEvent> Codec::decode(const AppPacket& packet) const {
    // The stand-alone packet parser is shared with codec fixtures; the
    // selected application codec is responsible for classifying its outcome.
    switch (packet.opcode) {
    case kChannelMessage: case kMail: case kBuddy: case kIgnore: case kMailLogin:
    case kMailHeaderCount: case kMailHeader: case kMailSendBody: case kMailNew:
    case kMailDeliveryStatus: case kMailboxChange: case kAnnounceJoin: case kAnnounceLeave:
        break;
    default:
        return messages::CodecResult<ChatServerEvent>::failure({
            messages::CodecErrorKind::UnsupportedOpcode, "unknown UCS application opcode", {}, {}, {}});
    }
    if (auto event = decode_chat_server_packet(packet))
        return messages::CodecResult<ChatServerEvent>::success(std::move(*event));
    return messages::CodecResult<ChatServerEvent>::failure({
        messages::CodecErrorKind::MalformedPacket, "invalid UCS application packet", {}, {}, {}});
}

std::size_t Codec::wire_cost(const messages::UcsCommand& command) const {
    const auto packet = encode(command);
    return sizeof(AppPacket) + packet.data.size();
}

} // namespace ucs

} // namespace hailstone::net
