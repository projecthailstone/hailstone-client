#pragma once
// The login server has its own wire protocol; it is not the RoF2 world/zone codec.
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include "net/messages/login.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "net/wire/app_packet.h"
namespace hailstone::net {
struct LoginReplyBase;
struct LoginResult;
struct ServerInfo;
struct PlayResult;
// Packet builders and parsers, exposed for tests. A login message is an application packet: opcode u16, then
// (the AppPacket's data) sequence u32, compressed u8, encrypt type u8 (2 = DES), unused u32, body.
namespace login_codec {

// This object owns wire sequence allocation and the wire-to-semantic request
// correlation table for one login connection attempt. No pending sequence or
// numeric login opcode belongs to LoginClient.
class Codec {
public:
    AppPacket encode(const messages::LoginCommand& command);
    std::variant<messages::LoginEvent, messages::LoginCodecIssue> decode(const AppPacket& packet);
    void cancel(std::uint64_t request_id);
    std::uint32_t last_sequence() const { return sequence_; }
private:
    std::uint32_t sequence_ = 1;
    std::map<std::uint32_t, std::uint64_t> pending_;
};

// Sequences at or above this skip the pending-request check, and the counter wraps to 1 when it reaches it.
constexpr std::uint32_t kSequenceLimit = 0xffffff;
// The handshake's body (CSHandshake): the protocol version.
constexpr std::uint16_t kHandshakeVersion = 11;

AppPacket session_ready(std::uint32_t sequence);
// CSPlayerLogin: DES-CBC of "user\0password\0token\0" and the token flag, zero-padded to 8 bytes.
AppPacket login_request(std::uint32_t sequence, std::string_view user, std::string_view password);
AppPacket token_login_request(std::uint32_t sequence, std::string_view token);
AppPacket server_list_request(std::uint32_t sequence);
AppPacket play_request(std::uint32_t sequence, std::uint32_t server_id);
AppPacket kick_request(std::uint32_t sequence);

// The header's sequence (0 when the packet is too short).
std::uint32_t sequence_of(std::span<const std::byte> data);
// Server pushes that need no pending request: 0x25-0x2d, 0x30, 0x31.
bool is_push(std::uint16_t opcode);
// The reply's body after the header: inflated when byte 6 of the packet (the compressed flag) is 1, then DES-CBC
// decrypted (rounded up to whole blocks) for the two replies whose class sets encrypt type 2 (0x18, 0x2f).
std::vector<std::byte> reply_body(std::uint16_t opcode, std::span<const std::byte> data);

// Throw std::runtime_error (or std::out_of_range) on malformed data. They take the whole AppPacket data.
LoginReplyBase parse_handshake_reply(std::span<const std::byte> data);
LoginResult parse_login_reply(std::span<const std::byte> data);
std::vector<ServerInfo> parse_server_list(std::span<const std::byte> data);
PlayResult parse_play_reply(std::span<const std::byte> data);
LoginReplyBase parse_kick_reply(std::span<const std::byte> data);
} // namespace login_codec

} // namespace hailstone::net
