#pragma once
#include <cstdint>
namespace hailstone::net {
// Login opcodes of EQEmu's SoD/RoF2 login port, from
// loginserver/login_util/login_opcodes_sod.conf (the RoF2 client uses the
// SoD login protocol).
namespace login_op {
constexpr std::uint16_t SessionReady = 0x0001; // CSHandshake
constexpr std::uint16_t Logout = 0x0003;
constexpr std::uint16_t Login = 0x0002;
constexpr std::uint16_t ServerListRequest = 0x0004;
constexpr std::uint16_t PlayEverquestRequest = 0x000d;
constexpr std::uint16_t EnterChat = 0x000f;
constexpr std::uint16_t PollResponse = 0x0011;
// eqmain's kick-my-player request (class at 0x100209b0, header only), sent from ServerSelectWnd after a Yes to the
// 302 / 114 question, and its reply (class at 0x10021c70, a base reply). EQEmu's login server has neither.
constexpr std::uint16_t KickPlayer = 0x0016;
constexpr std::uint16_t ChatMessage = 0x0017;
constexpr std::uint16_t LoginAccepted = 0x0018;
constexpr std::uint16_t ServerListResponse = 0x0019;
constexpr std::uint16_t PlayEverquestResponse = 0x0022;
constexpr std::uint16_t Poll = 0x0029;
constexpr std::uint16_t OrderExpansionReply = 0x002f; // DES-encrypted like LoginAccepted
constexpr std::uint16_t LoginExpansionPacketData = 0x0031;
constexpr std::uint16_t KickPlayerReply = 0x0032;
} // namespace login_op


} // namespace hailstone::net
