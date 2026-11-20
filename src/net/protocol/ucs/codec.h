#pragma once
// All UCS wire packet framing and one-byte app opcode interpretation lives here.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "net/wire/app_packet.h"
#include "net/messages/ucs.h"
#include "net/messages/result.h"
namespace hailstone::net {
namespace ucs {
class Codec {
public:
    AppPacket encode(const messages::UcsCommand& command) const;
    // Unsupported wire values are not malformed known messages. An invalid
    // OP_MailLogin is an error, not an optional event to silently ignore.
    messages::CodecResult<ChatServerEvent> decode(const AppPacket& packet) const;
    // Accounting uses the exact codec wire length, but never exports bytes.
    std::size_t wire_cost(const messages::UcsCommand& command) const;
};
} // namespace ucs

struct ChatServerInfo;
struct ChatServerEvent;
// Packet encode/decode (tests).
// OP_MailLogin: "SOE.EQ.<short>.<name>" and the connection type with the key.
AppPacket encode_mail_login(const ChatServerInfo& info);
AppPacket encode_mail_command(const std::string& text);
// UniversalChatProxy_SendCommand @0x7ef870 for a mail command: OP_Mail with
// the command, its leading '[' replaced by ';', a NUL and the (empty)
// argument string with its NUL ("[getheaders" → ";getheaders\0\0").
AppPacket encode_mail_bracket_command(const std::string& text);
std::optional<ChatServerEvent> decode_chat_server_packet(const AppPacket& p);
// Server direction, for tests: one opcode with NUL-terminated strings.
AppPacket encode_ucs_strings(std::uint16_t op, const std::vector<std::string>& strings);

} // namespace hailstone::net
