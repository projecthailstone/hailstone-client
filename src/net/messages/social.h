#pragma once

// Protocol-independent gameplay values extracted from the RoF2 packet declarations.
// Application codecs translate these values to their own wire layouts.
// Serial lengths, packet opcodes and binary read/write remain in protocol/rof2/.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hailstone::net {

// OP_WhoAllRequest (0x674b), client → zone, 156 bytes (rof2_structs.h
// Who_All_Struct): char[64] name, 64 unknown bytes, u32 race, class, level
// low, level high, gm lookup, guild id, type. 0xffffffff = no filter. The
// guild field also carries -2 friends, -3 LFG, -4 traders, -5 buyers
// (Who_BuildAndSendRequest @0x524400). Type 0 is a zone /who (EQEmu's
// EntityList::ZoneWho), 3 /who all (world).
// Semantic intents for server-side chat and social requests.
struct FriendsWhoIntent { std::vector<std::string> friends; bool all = false; std::string server; };
struct RandomRollIntent { std::uint32_t low = 0, high = 100; };
struct ConsentIntent { std::string name; bool deny = false; };
struct SocialEmoteIntent { std::string text; };

struct WhoRequest {
    std::string name;
    std::uint32_t race = 0xffffffff, class_ = 0xffffffff;
    std::uint32_t level_low = 0xffffffff, level_high = 0xffffffff;
    std::uint32_t gm = 0xffffffff;
    std::uint32_t guild = 0xffffffff;
    std::uint32_t type = 0;
};

// OP_WhoAllResponse (0x578c), zone → client, as HandleWhoAllResponse
// @0x4bc010 reads it (EQEmu ENCODE(OP_WhoAllResponse) writes the same): a
// 64-byte header (u32 id, u32 title eqstr, char[27] rule line, u8, u32,
// u32 footer eqstr at +0x28, u32 footer count at +0x2c, u32 × 3, u32 entry
// count at +0x3c), then per player: u32 format eqstr, u32 flags, u32 PID
// eqstr, name, u32 rank eqstr, guild, u32, u32 extra eqstr, u32 zone eqstr,
// u32 zone id, [flags 0x200: a zone name], u32 class, u32 level, u32 race,
// account, u32.
struct WhoEntry {
    std::uint32_t format = 0;
    std::uint32_t flags = 0;       // 1 AFK, 8 linkdead, 0x10 trader, 0x20 buyer, 0x40 RIP, 0x200 zone name, 0x400 offline
    std::uint32_t pid_string = 0xffffffff;
    std::string name;
    std::uint32_t rank_string = 0xffffffff;
    std::string guild;
    std::uint32_t unknown1 = 0xffffffff;
    std::uint32_t extra_string = 0xffffffff;
    std::uint32_t zone_string = 0xffffffff;
    std::uint32_t zone_id = 0;
    std::string zone_name; // flags 0x200
    std::uint32_t class_ = 0, level = 0, race = 0;
    std::string account;
    std::uint32_t unknown2 = 0;
};

struct WhoResponse {
    std::uint32_t id = 0;
    std::uint32_t title_string = 0xffffffff;
    std::string line;
    std::uint32_t footer_string = 0xffffffff;
    std::uint32_t footer_count = 0;
    std::uint32_t footer_values[3] = {};
    std::vector<WhoEntry> entries;
};

// OP_RandomReply (0x106b), zone → client, 76 bytes: u32 low, high, result, char[64] name.
struct RandomReply {
    std::uint32_t low = 0, high = 0, result = 0;
    std::string name;
};

// OP_Emote (0x373b), zone → client: u32 type, text ("Name text"); EQEmu
// sends /emote and social text this way. Handle_OP_Emote @0x4b89f0 shows the
// text in colour 0x107.
struct Emote {
    std::uint32_t type = 0;
    std::string text;
};

// The group finder (CGroupSearchWnd; game/group_search.h). EQEmu passes these through untranslated
// (eq_packet_structs.h LFG_Struct, LFP_Struct, LFGGetMatchesRequest_Struct, LFPGetMatchesRequest_Struct;
// world/zone HandleLFGMatches / HandleLFPMatches build the replies).
//
// OP_LFGCommand (0x6060), client → zone, 80 bytes (Send_OP_LFGCommand @0x5191c0): u32 0, u8 on +4, u8 match
// filter +5 (GSFW_ExclusionSearchCheckbox), u32 from level +8, u32 to level +0xc, char[64] comments +0x10.
// Cmd_lfgroup sends {on, 0, 0, 100, ""}; the window's posts (0x6783b0) its fields.
// Searching is an intent, never a pre-encoded request packet. The LFP
// query ignores class_mask, which is meaningful for an LFG player query.
struct GroupSearchQuery {
    std::uint32_t from_level=1, to_level=100, class_mask=0xffffffffu;
};

struct LfgCommand {
    bool on = false, match_filter = false;
    std::uint32_t from_level = 0, to_level = 0;
    std::string comments;
};

// OP_LFPCommand (0x49a9), client → zone, 84 bytes (Send_OP_LFPCommand @0x519280; 0x6791f0): u32 0, u8 on +4,
// u8 match filter +5, u32 from +8, u32 to +0xc, u32 classes +0x10 (1 << class id), char[64] comments +0x14.
struct LfpCommand {
    bool on = false, match_filter = false;
    std::uint32_t from_level = 0, to_level = 0, classes = 0;
    std::string comments;
};

// OP_LFGGetMatchesResponse (0x5048), zone → client (Handle_OP_LFGGetMatchesResponse @0x4b7a30): u32 (skipped),
// then to the end {comments, name (C strings), i16 class, level, zone, guild, anon}. The original copies each string
// into a 64-byte field (unbounded; cut to 63 here) and widens the numbers with their sign.
struct LfgMatch {
    std::string comments, name;
    std::int32_t class_ = 0, level = 0, zone = 0, guild = 0, anon = 0;
};

// OP_LFPGetMatchesResponse (0x22c6) (Handle_OP_LFPGetMatchesResponse @0x4b7b40): u32 (skipped), then to the end
// {comments, i16 from, i16 to, u32 classes, i16 count, count x {name, i16 class, level, zone, guild}}. The
// original's record (0x248 bytes) holds 6 members; more are read past it there and dropped here. A record that
// runs past the packet's end is dropped (the original reads on).
struct LfpMember {
    std::string name;
    std::int32_t class_ = 0, level = 0, zone = 0, guild = 0;
};

struct LfpMatch {
    std::string comments;
    std::int32_t from_level = 0, to_level = 0;
    std::uint32_t classes = 0;
    std::int32_t count = 0; // as sent (the groups list's "#")
    std::vector<LfpMember> members;
};

inline constexpr int kLfpMatchMembers = 6;

// OP_ConsentResponse (0x384a) / OP_DenyResponse (0x2382), zone → client, both read by Handle_OP_DenyResponse
// @0x4ba520 (ConsentResponse_Struct): char[64] the player given or denied permission (+0), char[64] the corpse's
// owner (+0x40), u8 permission (+0x80), the zone's name (+0x81, char[32]).
struct ConsentResponse {
    std::string grant_name;
    std::string owner_name;
    std::uint8_t permission = 0;
    std::string zone;
};

// GuildCommand_Struct: char[64] other name, char[64] my name, u16 guild id,
// 2 bytes, u32 rank. OP_GuildInvite (0x7099) passes through unchanged, so
// it is the server's 136-byte struct (Send_OP_GuildInvite @0x424370 sends
// 136 bytes with rank 8, recruit); OP_GuildRemove (0x1444) goes through
// DECODE(OP_GuildRemove) as RoF2's 140-byte struct (CEverQuest::GuildRemove
// @0x523a10 sends 140).
// Guild management requests are semantic; RoF2's variable-sized record and
// its action/tag fields are constructed by the selected game codec.
struct GuildManagementIntent {
    enum class Action : std::uint8_t { SetUrl, SetChannel, RenameRank, SetRankPermission,
        MakeLeader, ManageBanker, PublicNote, RequestMotd } action = Action::SetUrl;
    std::string member, text, actor;
    std::uint32_t guild_id=0, rank=0, permission=0, flags=0;
    bool enabled=false;
};
struct GuildCommand {
    std::string other_name;
    std::string my_name;
    std::uint32_t guild_id = 0; // u16 on the wire (+ 2 bytes)
    std::uint32_t rank = 0;
};

// OP_GuildInviteAccept (0x7053), client → zone, 136 bytes
// (GuildInviteAccept_Struct): char[64] inviter, char[64] new member, u32
// response (the offered rank to accept; 9 declines), u32 guild id.
struct GuildInviteAccept {
    std::string inviter;
    std::string new_member;
    std::uint32_t response = 0;
    std::uint32_t guild_id = 0;
};

inline constexpr std::uint32_t kGuildInviteDecline = 9;

// Handle_OP_GuildInvite's answer with Options/GuildInvites off (another guild's invitation declined at once).
inline constexpr std::uint32_t kGuildInviteAutoDecline = 0xb;

inline constexpr std::uint32_t kHailstoneGuildMagic = 0x47534d4e; // "NMSG" on the wire

inline constexpr std::uint16_t kHailstoneGuildVersion = 1;

inline constexpr std::uint32_t kHailstoneGuildWorldInvites = 1;

enum class HailstoneGuildAction : std::uint16_t { Hello = 0, Invite = 1, Answer = 2 };

struct HailstoneGuild {
    HailstoneGuildAction action = HailstoneGuildAction::Hello;
    std::string name;
    std::uint32_t guild_id = 0;
    std::uint32_t value = 0;
};

// OP_GuildMOTD (0x3e13), zone → client, and OP_SetGuildMOTD (0x0b0b), client
// → zone (GuildMOTD_Struct): u32, char[64] name, char[64] set by, u32, text.
// The client sends 648 bytes (Send_OP_SetGuildMOTD @0x4f1310: 512 for the text).
struct GuildMotd {
    std::string name;
    std::string set_by;
    std::string motd;
};

// These are values, not the fixed-width RoF2 packet bodies.
struct ServerFilterSelection { std::array<std::uint32_t, 35> values{}; };
struct GuildRankDemotion { std::string actor, member; std::uint32_t new_rank = 0; };
struct RaidNoteCommand { std::string actor, member, note; std::uint32_t raid_id = 0; };
struct RaidMotdCommand { std::string actor, motd; };

// OP_GuildMemberList (0x12a6), zone → client (EQEmu ENCODE(OP_GuildMemberList);
// numbers big-endian): name, u32 guild id, u32 count, count × {name, u32 level,
// banker, class, rank, last on, tribute on, trophy tribute on, total tribute, last tribute, flag, public note, u16
// instance, u16 zone, u32 1, u32 0}. The original's reader (@0x424e80) keeps tribute on, trophy on (EQEmu's
// unknown01, always 0), the donations, the last donation and the flag (EQEmu's unknown_one, always 1; member +0x10,
// which CGuildMgmtWnd_UpdateTributeListMember shows as white tribute cells, yellow when 0).
struct GuildMember {
    std::string name;
    std::uint32_t level = 0, banker = 0, class_ = 0, rank = 0, last_on = 0;
    std::uint32_t tribute_enable = 0, trophy_enable = 0, total_tribute = 0, last_tribute = 0, tribute_flag = 1;
    std::string public_note;
    std::uint16_t zone_instance = 0, zone_id = 0;
};

struct GuildRosterChange {
    enum Kind { Add, Delete, Level, Note, Rank, Details } kind = Add;
    std::uint32_t guild_id = 0;
    std::string name, note;
    std::uint32_t level = 0, class_ = 0, rank = 0, zone_id = 0, last_on = 0;
    bool alt = false, banker = false, offline = false;
};

struct GuildMemberList {
    std::string player_name;
    std::uint32_t guild_id = 0;
    std::vector<GuildMember> members;
};

// OP_GuildMemberUpdate (0x69b9), zone → client, 80 bytes: u32 guild id,
// char[64] member, u16 zone, u16 instance, u32 last seen, u32.
struct GuildMemberUpdate {
    std::uint32_t guild_id = 0;
    std::string member;
    std::uint16_t zone_id = 0, instance_id = 0;
    std::uint32_t last_seen = 0;
};

// The server's OP_GuildUpdate (HandleWorldMessage_Guild @0x425d30) is the same record: at least 0x54 bytes, the action
// at +0; action 5 needs 0x59 bytes and action 4 0x55 (a shorter one gives nothing). The text runs from +0x50 (actions
// 0 and 1) or +0x54 (4) to the first NUL or the end. SendGuildRanks stamps action 5's guild id at +0x4c;
// permission-change broadcasts and rank names leave that field zero. Other actions carry the action alone.
struct GuildUpdate {
    std::uint32_t action = 0;
    std::uint32_t guild_id = 0; // the server's action 5 context; 0 means no context tag
    std::uint32_t rank = 0, permission = 0; // actions 4 and 5
    bool on = false;                        // action 5
    std::string text;                       // actions 0, 1 and 4
};

// OP_GuildPromote (0x6a98), client → zone, 136 bytes: Send_OP_GuildPromote @0x424640 sends char[64] target,
// char[64] own name, u32 guild id, u32 new rank (EQEmu's GuildPromoteStruct names the third "rank";
// Handle_OP_GuildPromote takes the new rank from +132).
struct GuildPromote {
    std::string target;
    std::string name;
    std::uint32_t guild_id = 0;
    std::uint32_t new_rank = 0;
};

// OP_GuildWar (0x1ffb) and OP_GuildPeace (0x67e3), client → zone, 140 bytes (Send_OP_GuildWar @0x525ed0,
// Send_OP_GuildPeace @0x526040): char[64] the other guild's member, char[64] own name, u32 own guild id, u32 days
// (0 for peace), u32 1. The server's offer is the same record (+0x88 nonzero).
struct GuildWarPeace {
    std::string other;
    std::string name;
    std::uint32_t guild_id = 0;
    std::int32_t days = 0;
};

// RaidGeneral_Struct, 140 bytes: u32 action, char[64] player name, u32,
// char[64] leader name, u32 parameter. OP_RaidInvite (0x55ac) carries the
// client's raid commands (CRaid helper @0x47ca90 sends 140 bytes);
// OP_RaidUpdate (0x3973) the server's events, with action-specific tails:
// 0 add (+ u8 class, level, group leader, 5 flags), 10 the zone-in reset
// (136 bytes), 14/30 leadership (392), 35 MOTD (+ text).
struct RaidGeneral {
    std::uint32_t action = 0;
    std::string player_name;
    std::string leader_name;
    std::uint32_t parameter = 0;
    // +0x44: the client's commands carry CRaid +0x2c00 raidId there (Send_OP_RaidInvite_0047ca90 / 0047c430);
    // OP_RaidUpdate 8 brings it (CRaid::RaidCreated).
    std::uint32_t raid_id = 0;
};

// Actions (EQEmu RaidCommand*, common/raid.h).
inline constexpr std::uint32_t kRaidAddMember = 0, kRaidAccept = 1, kRaidRemoveMember = 1, kRaidInvite = 3,
                               kRaidDisband = 5, kRaidMoveGroup = 6, kRaidCreate = 8, kRaidNoRaid = 10,
                               kRaidSetLeaderAbilities = 14, kRaidSetLeaderData = 15, kRaidLock = 17,
                               kRaidUnlock = 18, kRaidInviteSent = 20, kRaidSetLeader = 30, kRaidInviteFail = 31,
                               kRaidSetLootType = 32, kRaidAddLooter = 33, kRaidRemoveLooter = 34, kRaidSetMotd = 35,
                               kRaidSetNote = 36;

// The raid leader's commands as the original sends them (Send_OP_RaidInvite_0047ca90: action, player name, leader
// name, parameter): 7 CRaid_SendRemoveGroupLeader {leader, member}, 8 / 9 CRaid_SendLockRaid / SendUnlockRaid
// {leader, leader}, 0x1e CRaid::SendLeadershipChange {me, member}, 0x20 CRaid::SetLootType {me, me, type}, 0x21 /
// 0x22 CRaid::AddRaidLooter / RemoveRaidLooter {leader, member}; 6 CRaid_SendMoveGroup {leader, member, group
// 0..11 or 0xffffffff}. EQEmu takes 0x20..0x22 as its LootType2 / AddLooter2 / RemoveLooter2 and answers 7 with
// "Raid command (7) NYI".
inline constexpr std::uint32_t kRaidCmdRemoveGroupLeader = 7, kRaidCmdLock = 8, kRaidCmdUnlock = 9,
                               kRaidCmdMakeLeader = 0x1e, kRaidCmdLootType = 0x20, kRaidCmdAddLooter = 0x21,
                               kRaidCmdRemoveLooter = 0x22;

struct RaidUpdate {
    RaidGeneral general;
    // action 0 (add): the member's class, level and group-leader flag.
    std::uint8_t class_ = 0, level = 0, group_leader = 0;
    std::string motd; // action 35
    std::string note; // action 36 (the member is general.leader_name, +0x48)
    // actions 14, 15 and 30 (RaidLeadershipUpdate_Struct): the dwords from +136 (Handle_OP_RaidUpdate's param + 0x22:
    // EQEmu's group block, then the raid leader's 16 raid ability ranks at +200, then 128 bytes); Leadership_
    // SetRaidAbilityRanks copies 64 of them over the character's +0x24c, so ability n's rank is dword n.
    std::array<std::uint32_t, 64> leadership{};
};

// ---- looking for a guild (OP_LFGuild; EQEmu zone/client_packet.cpp Handle_OP_LFGuild, client_process.cpp
// LFGuild replies through QueryServ, rule Guild:EnableLFGuild). The first u32 is the command. Classes are a bit
// per class (bit 0 warrior .. bit 15 berserker).
// C→Z: 0 our player entry (624 bytes: comment at +72, u32 time zone at +596, u8 on at +600), 1 the guild's entry
// (616 bytes: comment at +12, from level +524, to level +528, classes +532, AA +536, time zone +540, u8 on +544),
// 3 search players (28 bytes: u32, u32, from level, to level, min AA, time zone, classes), 4 search guilds (24
// bytes: u32, u32, level, AA, time zone, class).
// Z→C: 0 and 1 our entries as they stand (the same layouts, u32 posted at +608 / +548), 3 player matches (u32, u32
// count, count × {name, level, class, AA, time zone, comment}), 4 guild matches (u32, u32 count, count × {name, time
// zone, comment}).
struct LFGuildPlayerSearch {
    std::uint32_t from_level = 0, to_level = 0, min_aa = 0, time_zone = 0, classes = 0;
};
struct LFGuildGuildSearch {
    std::uint32_t level = 0, aa = 0, time_zone = 0, class_ = 0;
};

struct LFGuildPlayer {
    std::string comment;
    std::uint32_t time_zone = 0, posted = 0;
    bool on = false;
};

struct LFGuildGuild {
    std::string comment;
    std::uint32_t from_level = 1, to_level = 100, classes = 0, aa = 0, time_zone = 0, posted = 0;
    bool on = false;
};

struct LFGuildPlayerMatch {
    std::string name, comment;
    std::uint32_t level = 0, class_ = 0, aa = 0, time_zone = 0;
};

struct LFGuildGuildMatch {
    std::string name, comment;
    std::uint32_t time_zone = 0;
};

struct LFGuildReply {
    std::optional<LFGuildPlayer> player;
    std::optional<LFGuildGuild> guild;
    std::optional<std::vector<LFGuildPlayerMatch>> players;
    std::optional<std::vector<LFGuildGuildMatch>> guilds;
};


struct XTargetEntry {
    std::int32_t slot = 0;
    std::uint8_t status = 0;
    std::uint32_t spawn_id = 0;
    std::string name;
};
struct XTargetResponse {
    std::uint32_t max = 0;
    std::vector<XTargetEntry> entries;
};
} // namespace hailstone::net
