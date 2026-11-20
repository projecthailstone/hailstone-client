#pragma once

// Typed RoF2 world-server packets: authentication, character select,
// character creation and the hand-off to a zone. Layouts follow the NMS
// server's EQEmu RoF2 translator (common/patches/rof2.cpp, rof2_structs.h)
// and world/client.cpp; per-packet notes are in docs/protocol/rof2-packets.md.
//
// encode(x) builds the packet the client sends (or, for server packets that
// tests need, the bytes the server would send); decode_*() returns nullopt on
// a wrong opcode-independent size or a truncated payload. Decoders do not
// check the opcode; the caller dispatches on it.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "net/wire/app_packet.h"

#include "net/messages/world.h"

namespace hailstone::net {

// ---------------------------------------------------------------- client → world

// OP_SendLoginInfo. EQEmu identifies a RoF2 world stream by this being the
// first packet with exactly 464 bytes (LoginInfo_Struct).
inline constexpr std::size_t kLoginInfoSize = 464;
// Whether "<account>\0<key>\0" fits login_info[64]. encode() of one that does not keeps the field's last NUL and
// cuts the key short, which world refuses: the RoF2 codec validates this before encoding.
bool login_info_fits(const LoginInfo& v);
AppPacket encode(const LoginInfo& v);
std::optional<LoginInfo> decode_login_info(const AppPacket& p);

AppPacket encode(const EnterWorld& v);
std::optional<EnterWorld> decode_enter_world(const AppPacket& p);

// Builds the record the original client sends for this file; `seed` drives
// the sample positions (the original uses its own RNG; only the first 8
// bytes are checked by the server).
FileChecksum make_file_checksum(std::span<const std::byte> file, std::uint32_t seed);
AppPacket encode(ChecksumKind kind, const FileChecksum& v);
std::optional<FileChecksum> decode_file_checksum(const AppPacket& p);

AppPacket encode(const NameApproval& v);
std::optional<NameApproval> decode_name_approval(const AppPacket& p);

// OP_ApproveName, world → client (1 byte): 1 = name reserved; 0 = rejected
// (also sent when OP_CharacterCreate fails). The client handles 2..5 as
// specific errors, which EQEmu never sends.
std::optional<std::uint8_t> decode_approve_name_reply(const AppPacket& p);

AppPacket encode(const CharCreate& v);
std::optional<CharCreate> decode_char_create(const AppPacket& p);

AppPacket encode(const CharCreateData& v); // server direction, for tests
std::optional<CharCreateData> decode_char_create_data(const AppPacket& p);


AppPacket encode(const DeleteCharacter& v);

AppPacket encode(const RandomName& v);
std::optional<RandomName> decode_random_name(const AppPacket& p);

// ---------------------------------------------------------------- world → client

std::optional<LogServer> decode_log_server(const AppPacket& p);

// OP_ExpansionInfo (68 bytes): expansion bit mask at +64.
std::optional<std::uint32_t> decode_expansion_info(const AppPacket& p);

// OP_SendMaxCharacters (12 bytes): max_chars at +0.
std::optional<std::uint32_t> decode_max_characters(const AppPacket& p);

AppPacket encode(const Membership& v); // for tests (the server's bytes)
std::optional<Membership> decode_membership(const AppPacket& p);

AppPacket encode(const MembershipDetails& v); // for tests
std::optional<MembershipDetails> decode_membership_details(const AppPacket& p);

std::optional<std::vector<GuildEntry>> decode_guilds_list(const AppPacket& p);

AppPacket encode(const std::vector<CharacterPreview>& v); // server direction, for tests
std::optional<std::vector<CharacterPreview>> decode_character_select(const AppPacket& p);

// OP_EnterWorld, world → client: the character to enter as a NUL-terminated
// name (empty unless auto-login or zoning; while zoning it is the live
// character, which the client then sends back in its own OP_EnterWorld).
std::optional<std::string> decode_enter_world_name(const AppPacket& p);

// OP_MOTD: NUL-terminated text.
std::optional<std::string> decode_motd(const AppPacket& p);

std::optional<ChatServerInfo> decode_chat_server(const AppPacket& p);

AppPacket encode(const ZoneServerInfo& v); // server direction, for tests
std::optional<ZoneServerInfo> decode_zone_server_info(const AppPacket& p);

// OP_ZoneUnavail: zone short name in the first 16 bytes.
std::optional<std::string> decode_zone_unavail(const AppPacket& p);


} // namespace hailstone::net
