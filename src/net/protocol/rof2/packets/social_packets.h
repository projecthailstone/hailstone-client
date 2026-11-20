#pragma once

// Typed RoF2 packets for the social commands: /who, /random, emotes,
// /assist, /lfg, /consent and /deny, and the guild and raid packets. Layouts
// follow the NMS server's RoF2 translator (common/patches/rof2.cpp,
// rof2_structs.h) and, for pass-through opcodes, common/eq_packet_structs.h;
// what the client sends and when follows the original (addresses at each
// struct). Per-packet notes are in
// docs/protocol/rof2-packets.md ("Social, guilds and raids").

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "net/protocol/rof2/opcode_map.h"
#include "net/wire/app_packet.h"

#include "net/messages/social.h"

namespace hailstone::net {

// ---------------------------------------------------------------- /who

inline constexpr std::size_t kWhoRequestSize = 156;
AppPacket encode(const WhoRequest& v);
std::optional<WhoRequest> decode_who_request(const AppPacket& p);

// OP_FriendsWho (0x3956), client → zone (Who_BuildAndSendRequest @0x524a8a):
// ASCII '0' (zone) or '1' (all), then each saved friend name prefixed by a comma,
// then NUL; no trailing comma. CChatService::GetFriendName @0x7efa90 strips
// the current world's prefix ("World.", matched case-insensitively with
// __strnicmp) and keeps other worlds.
// Replies use OP_WhoAllResponse.
AppPacket encode_friends_who(const std::vector<std::string>& friends, bool all,
                            std::string_view server_short_name = {});

AppPacket encode(const WhoResponse& v); // server direction, for tests
std::optional<WhoResponse> decode_who_response(const AppPacket& p);

// ---------------------------------------------------------------- /random

// OP_RandomReq (0x7b10), client → zone, 8 bytes: u32 low, u32 high (Cmd_random @0x4df6c0).
AppPacket encode_random_request(std::uint32_t low, std::uint32_t high);
AppPacket encode(const RandomReply& v); // server direction, for tests
std::optional<RandomReply> decode_random_reply(const AppPacket& p);

// ---------------------------------------------------------------- emotes

AppPacket encode(const Emote& v);
// A social's text as send_social sends it: 1028 bytes, u32 0xffffffff, char[1024].
AppPacket encode_social_emote(const std::string& text);
std::optional<Emote> decode_emote(const AppPacket& p);

// ---------------------------------------------------------------- /assist

// OP_Assist (0x4478), both directions, 4 bytes: u32 spawn id. The client
// sends whom it assists; the server answers with that spawn's target.
AppPacket encode_assist(std::uint32_t spawn_id);
std::optional<std::uint32_t> decode_assist(const AppPacket& p);

// ---------------------------------------------------------------- /lfg

AppPacket encode(const LfgCommand& v);
AppPacket encode(const LfpCommand& v);
// OP_LFGGetMatchesRequest (0x0340), 16 bytes: u32 0, from, to, classes (0xffffffff: Show All).
// OP_LFPGetMatchesRequest (0x4d7d), 12 bytes: u32 0, from, to.
AppPacket encode_lfg_matches_request(std::uint32_t from_level, std::uint32_t to_level, std::uint32_t classes);
AppPacket encode_lfp_matches_request(std::uint32_t from_level, std::uint32_t to_level);
std::optional<std::vector<LfgMatch>> decode_lfg_matches(const AppPacket& p);
AppPacket encode(const std::vector<LfgMatch>& v); // server direction, for tests
std::optional<std::vector<LfpMatch>> decode_lfp_matches(const AppPacket& p);
AppPacket encode(const std::vector<LfpMatch>& v); // server direction, for tests

// ---------------------------------------------------------------- consent

// OP_Consent (0x1fd1) / OP_ConsentDeny (0x7a45), client → zone: the name, NUL-terminated.
AppPacket encode_consent(bool deny, const std::string& name);

std::optional<ConsentResponse> decode_consent_response(const AppPacket& p);
AppPacket encode(const ConsentResponse& v, bool deny_opcode = false); // server direction, for tests

// ---------------------------------------------------------------- guilds

// OP_GuildsList is decoded by net/protocol/rof2/packets/world_packets.h (decode_guilds_list).

inline constexpr std::size_t kGuildInviteSize = 136, kGuildRemoveSize = 140;
AppPacket encode_guild_invite(const GuildCommand& v);
AppPacket encode_guild_remove(const GuildCommand& v);
std::optional<GuildCommand> decode_guild_command(const AppPacket& p);

AppPacket encode(const GuildInviteAccept& v);
std::optional<GuildInviteAccept> decode_guild_invite_accept(const AppPacket& p);


AppPacket encode_set_guild_motd(const GuildMotd& v);
AppPacket encode_guild_motd(const GuildMotd& v); // server direction, for tests
std::optional<GuildMotd> decode_guild_motd(const AppPacket& p);

// OP_GetGuildMOTD (0x36e0), client → zone (Cmd_getguildmotd @0x4f14d0): a
// zeroed 0x288-byte record with the guild id and the player's name; EQEmu
// reads nothing from it.
AppPacket encode_get_guild_motd(std::uint32_t guild_id, const std::string& name);

AppPacket encode(const GuildMemberList& v); // server direction, for tests
std::optional<GuildMemberList> decode_guild_member_list(const AppPacket& p);

std::optional<GuildMemberUpdate> decode_guild_member_update(const AppPacket& p);

// OP_GuildLeader (0x7e09), client → zone, 128 bytes (GuildMakeLeader:
// char[64] name, char[64] new leader; Send_OP_GuildLeader @0x524070).
AppPacket encode_guild_leader(const std::string& me, const std::string& new_leader);

// OP_GuildPublicNote (388 bytes, EQEmu GuildUpdate_PublicNote, passed through): u32, char[64] me, char[64] the
// member, char[256] note. OP_GuildManageBanker (136 bytes, GuildManageBanker_Struct): u32, char[64] me, char[64]
// the member, u32 flags (1 banker, 2 alt). The original's senders (the guild window) are not decoded.
// Send_OP_GuildPublicNote @0x424240: {u32 guild id, char[64] own name, char[64] member, char[256] note} (388 B).
AppPacket encode_guild_public_note(std::uint32_t guild_id, const std::string& me, const std::string& member,
                                   const std::string& note);
// Send_OP_GuildManageBanker @0x424bb0: 0x44 zero bytes, char[64] member, u32 flags (1 banker, 2 alt) (136 B).
AppPacket encode_guild_manage_banker(const std::string& member, std::uint32_t flags);
// OP_GuildUpdate (0x2958) from CGuildMgmtWnd::WndNotification, zeroed first: action 0 / 1 the web page / chat
// channel (0x1050 B, the text at +0x50); 4 a rank's name (0xb8 B: u32 rank at +0x50, char[100] name at +0x54);
// 5 a rank's permission (0x5c B: u32 rank +0x50, u32 permission +0x54, u8 on +0x58).
AppPacket encode_guild_update_text(std::uint32_t action, const std::string& text);
AppPacket encode_guild_rank_name(std::uint32_t rank, const std::string& name);
AppPacket encode_guild_rank_permission(std::uint32_t rank, std::uint32_t permission, bool on,
                                     std::uint32_t guild_id = 0);
std::optional<GuildUpdate> decode_guild_update(const AppPacket& p);

inline constexpr std::size_t kGuildPromoteSize = 136;
AppPacket encode(const GuildPromote& v);
std::optional<GuildPromote> decode_guild_promote(const AppPacket& p);

inline constexpr std::size_t kGuildWarSize = 140;
AppPacket encode_guild_war(const GuildWarPeace& v);
AppPacket encode_guild_peace(const GuildWarPeace& v);
std::optional<GuildWarPeace> decode_guild_war_peace(const AppPacket& p);

// ---------------------------------------------------------------- raids

inline constexpr std::size_t kRaidGeneralSize = 140;
AppPacket encode(const RaidGeneral& v);
// The raid MOTD (1164 bytes, RaidMOTD_Struct): RaidGeneral {35, me, me}, char[1024] text.
AppPacket encode_set_raid_motd(const std::string& me, const std::string& motd);
// A member's raid note (204 bytes, RaidNote_Struct; Send_OP_RaidInvite_0047c430, the raid window's note Update):
// RaidGeneral {0x24, me, raid id, member}, char[64] note. EQEmu keeps it and sends it to the raid as OP_RaidUpdate
// 0x24 {member at +0x48, note at +0x8c}.
AppPacket encode_set_raid_note(const std::string& me, std::uint32_t raid_id, const std::string& member,
                               const std::string& note);
AppPacket encode_raid_update(const RaidUpdate& v); // server direction, for tests
std::optional<RaidUpdate> decode_raid_update(const AppPacket& p);

std::optional<LFGuildReply> decode_lfguild(const AppPacket& p);
AppPacket encode(const LFGuildPlayer& v);
AppPacket encode(const LFGuildGuild& v);
AppPacket encode_lfguild_search_players(std::uint32_t from_level, std::uint32_t to_level, std::uint32_t min_aa,
                                        std::uint32_t time_zone, std::uint32_t classes);
AppPacket encode_lfguild_search_guilds(std::uint32_t level, std::uint32_t aa, std::uint32_t time_zone, std::uint32_t class_);
AppPacket encode(const std::vector<LFGuildPlayerMatch>& v); // server direction, for tests
AppPacket encode(const std::vector<LFGuildGuildMatch>& v);  // server direction, for tests

} // namespace hailstone::net
