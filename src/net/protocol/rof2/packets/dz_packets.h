#pragma once

// Expeditions (dynamic zones) in EQEmu's layouts (common/patches/rof2*.{h,cpp}, zone/dynamic_zone.cpp,
// zone/expedition*.cpp); the original's CDynamicZoneWnd / CDZSwitchListWnd senders are not decoded. Each zone →
// client packet starts with u32 client id, u32 (RoF2's field after Titanium); the decoders skip both.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "net/wire/app_packet.h"
#include "net/messages/dz.h"

namespace hailstone::net {

// OP_DzExpeditionInfo (208 bytes): u32 assigned (0 clears the window), u32 max players, char[128] expedition name,
// char[64] leader.

std::optional<DzInfo> decode_dz_info(const AppPacket& p);
AppPacket encode(const DzInfo& v); // server direction, for tests

// OP_DzExpeditionInvite (272 bytes): char[64] inviter, char[128] expedition, u8 swapping, char[64] the member a
// swap removes, 3 pad, u16 zone, u16 instance (sent back in the answer).

std::optional<DzInvite> decode_dz_invite(const AppPacket& p);
AppPacket encode(const DzInvite& v); // server direction, for tests

// OP_DzExpeditionInviteResponse (80 bytes): u32, u32, u16 zone, u16 instance, u8 accepted, u8 swapping, char[64]
// swap name, 2 pad.
AppPacket encode_dz_invite_response(const DzInvite& invite, bool accepted);

// OP_DzExpeditionLockoutTimers: u32 count, count × {expedition, u32 seconds left, i32 event type (-1 replay), event
// name} (strings NUL-terminated).

std::optional<std::vector<DzLockout>> decode_dz_lockouts(const AppPacket& p);
AppPacket encode(const std::vector<DzLockout>& v); // server direction, for tests

// OP_DzMemberList (and OP_DzMemberListStatus, one member whose status changed): u32 count, count × {name, u8
// status}.


// A client limit: more members than this fails the decode (and the list grows no further). An expedition holds at
// most a raid (72) on stock EQEmu; the status update matches each name against the list, so a count the packet
// alone bounded (millions of 2-byte entries) would stop the frame.

std::optional<std::vector<DzMember>> decode_dz_members(const AppPacket& p);
AppPacket encode(const std::vector<DzMember>& v, bool status_update = false); // server direction, for tests

// OP_DzMemberListName (76 bytes): u32 add (0 removes), char[64] name.

std::optional<DzMemberName> decode_dz_member_name(const AppPacket& p);
AppPacket encode(const DzMemberName& v); // server direction, for tests

// OP_DzSetLeaderName (72 bytes): char[64] leader.
std::optional<std::string> decode_dz_leader(const AppPacket& p);
AppPacket encode_dz_leader(std::string_view leader); // server direction, for tests

// OP_DzExpeditionEndsWarning (12 bytes): u32 minutes left.
std::optional<std::uint32_t> decode_dz_ends_warning(const AppPacket& p);
AppPacket encode_dz_ends_warning(std::uint32_t minutes); // server direction, for tests

// OP_DzChooseZone (the DZ switch list): u32 count, count × {u16 zone, u16 instance, u32 id, u32 type, u32 id,
// description, leader}; the client answers OP_DzChooseZoneReply (52 bytes) with the chosen entry.

std::optional<std::vector<DzChoice>> decode_dz_choose_zone(const AppPacket& p);
AppPacket encode(const std::vector<DzChoice>& v); // server direction, for tests
AppPacket encode_dz_choose_zone_reply(const DzChoice& c);

// OP_DzCompass: u32 count, count × 24 B {u16 zone, u16 instance, u32 type (1 expedition, 2 tutorial, 3 task, 4
// mission, 5 quest), u32 switch id, f32 y, f32 x, f32 z} (server y and x: `at` is in zone-file coordinates).

std::optional<std::vector<DzCompassPoint>> decode_dz_compass(const AppPacket& p);
AppPacket encode(const std::vector<DzCompassPoint>& v); // server direction, for tests

// OP_DzAddPlayer / OP_DzRemovePlayer / OP_DzMakeLeader (72 bytes: u32, u32, char[64] name).
AppPacket encode_dz_add_player(std::string_view name);
AppPacket encode_dz_remove_player(std::string_view name);
AppPacket encode_dz_make_leader(std::string_view name);
// OP_DzSwapPlayer (136 bytes: u32, u32, char[64] added, char[64] removed). /dzswapplayer (Cmd_dzswapplayer
// @0x4ec4c0) takes the removed name first, then the added one.
AppPacket encode_dz_swap_player(std::string_view add, std::string_view remove);
// OP_DzQuit, OP_DzListTimers, OP_DzPlayerList: the original sends 8 bytes it leaves unset (Send_OP_DzQuit_004ec7f0,
// Send_OP_DzListTimers @0x4ec750, Cmd_dzplayerlist @0x4ec6b0); zero here. EQEmu reads no body.
AppPacket encode_dz_quit();
AppPacket encode_dz_list_timers();
AppPacket encode_dz_player_list();

} // namespace hailstone::net
