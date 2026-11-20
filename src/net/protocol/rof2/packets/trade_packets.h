#pragma once

// RoF2 trade packets (player trades and NPC "give"/quest hand-ins). None has
// a RoF2 translator in the NMS server's common/patches/rof2.cpp except
// OP_CancelTrade (ENCODE copies fromid and action), so the layouts are
// rof2_structs.h / eq_packet_structs.h (#pragma pack(1)). What the original
// client sends and does follows eqgame.exe:
//
//   OP_TradeRequest    C→Z  CEverQuest::DropItemOrMoneyOnPlayer @0x5331f0: {target, me}
//   OP_TradeRequest    Z→C  Handle_OP_TradeRequest @0x4c23c0: CTradeWnd::Activate(partner, 0),
//                           which answers OP_TradeRequestAck {partner, me} at once
//   OP_TradeRequestAck Z→C  Handle_OP_TradeRequestAck @0x4c2070 → CTradeWnd::Activate(partner, 1):
//                           drops the cursor item (or cursor coin) into the trade
//   OP_TradeAcceptClick C→Z Send_OP_TradeAcceptClick @0x77f490: {partner, u8 1, 3 unset bytes}
//   OP_TradeAcceptClick Z→C Handle_OP_TradeAcceptClick @0x4b7890: byte +4 = partner ready
//   OP_TradeCoins      Z→C  Handle_OP_TradeCoins @0x4b76d0: the partner added coin (a delta)
//   OP_CancelTrade     C→Z  Send_OP_CancelTrade_00780550: {partner, me}
//   OP_CancelTrade     Z→C  Handle_OP_CancelTrade @0x4b7710: message, close, and send our own
//                           cancel back (CTradeWnd_CancelAndClose)
//   OP_FinishTrade     Z→C  Handle_OP_FinishTrade @0x4b78b0 → CTradeWnd::CompleteTrade (empty)
//   OP_MoveCoin        C→Z  CEverQuest::MoveMoney @0x51ad70 (inventory → cursor → trade)
//   OP_TradeBusy       C→Z  Handle_OP_TradeRequest when trades are refused
//
// Per-packet notes are in docs/protocol/rof2-packets.md section "Trade".

#include <cstdint>
#include "net/messages/coin.h"
#include <optional>

#include "net/wire/app_packet.h"

namespace hailstone::net {

// TradeRequest_Struct (8 bytes): OP_TradeRequest and OP_TradeRequestAck.
// The sender writes {the other side, itself}; the server forwards it as is
// (for an NPC target it answers the Ack itself with {us, the NPC}).
struct TradeRequest {
    std::uint32_t to_id = 0;
    std::uint32_t from_id = 0;
};
inline constexpr std::size_t kTradeRequestSize = 8;
AppPacket encode_trade_request(const TradeRequest& v);
AppPacket encode_trade_request_ack(const TradeRequest& v);
std::optional<TradeRequest> decode_trade_request(const AppPacket& p); // either opcode

// OP_TradeAcceptClick (8 bytes, TradeAccept_Struct): the partner's id and
// the ready byte (the original sends its "I am ready" flag, always 1 when
// sent; the rest of the dword is unset, we send 0). The server forwards it
// to a player partner, who reads byte +4.
struct TradeAccept {
    std::uint32_t partner_id = 0;
    std::uint8_t ready = 1;
    std::uint8_t unknown5 = 0, unknown6 = 0, unknown7 = 0;
};
inline constexpr std::size_t kTradeAcceptSize = 8;
AppPacket encode(const TradeAccept& v);
std::optional<TradeAccept> decode_trade_accept(const AppPacket& p);

// OP_CancelTrade (8 bytes, CancelTrade_Struct): {from id, action}. The
// original sends {partner, me}; the server forwards {the receiver's own id
// for a player partner / the NPC's id, action} back.
struct CancelTrade {
    std::uint32_t from_id = 0;
    std::uint32_t action = 0;
};
inline constexpr std::size_t kCancelTradeSize = 8;
AppPacket encode(const CancelTrade& v);
std::optional<CancelTrade> decode_cancel_trade(const AppPacket& p);

// OP_TradeCoins (zone → client, 12 bytes, TradeCoin_Struct): the partner
// put coin into the trade: u32 trader (the receiver's own id), u8 coin type
// (0 copper, 1 silver, 2 gold, 3 platinum), 3 bytes, u32 amount added.
struct TradeCoins {
    std::uint32_t trader = 0;
    std::uint8_t type = 0;
    std::uint16_t unknown5 = 0;
    std::uint8_t unknown7 = 0;
    std::uint32_t amount = 0;
};
inline constexpr std::size_t kTradeCoinsSize = 12;
AppPacket encode(const TradeCoins& v); // server direction, for tests
std::optional<TradeCoins> decode_trade_coins(const AppPacket& p);

// OP_MoveCoin (client → zone, 20 bytes, MoveCoin_Struct): from location,
// to location (0 cursor, 1 inventory, 2 bank, 3 trade, 4 shared bank, -1
// destroy), coin type from and to (0 copper .. 3 platinum), amount.
inline constexpr std::size_t kMoveCoinSize = 20;
AppPacket encode(const MoveCoin& v);
std::optional<MoveCoin> decode_move_coin(const AppPacket& p);

// OP_TradeBusy (12 bytes, TradeBusy_Struct): {the requester, me, u8 reason,
// 3 bytes}. The original sends reason 0x62 when trades are off, 0x63 for
// group-only, 1 when busy, and prints eqstr 1447.
struct TradeBusy {
    std::uint32_t to_id = 0;
    std::uint32_t from_id = 0;
    std::uint8_t type = 1;
    std::uint8_t unknown9 = 0, unknown10 = 0, unknown11 = 0;
};
inline constexpr std::size_t kTradeBusySize = 12;
AppPacket encode(const TradeBusy& v);
std::optional<TradeBusy> decode_trade_busy(const AppPacket& p);

} // namespace hailstone::net
