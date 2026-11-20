#include "net/protocol/rof2/packets/trade_packets.h"

#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;

namespace {

AppPacket write_request(rof2::Op op, const TradeRequest& v) {
    ByteWriter w;
    w.le(v.to_id);
    w.le(v.from_id);
    return make_packet(op, std::move(w));
}

} // namespace

AppPacket encode_trade_request(const TradeRequest& v) { return write_request(rof2::Op::TradeRequest, v); }
AppPacket encode_trade_request_ack(const TradeRequest& v) { return write_request(rof2::Op::TradeRequestAck, v); }

std::optional<TradeRequest> decode_trade_request(const AppPacket& p) {
    if (p.data.size() != kTradeRequestSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        TradeRequest v;
        v.to_id = r.le<std::uint32_t>();
        v.from_id = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const TradeAccept& v) {
    ByteWriter w;
    w.le(v.partner_id);
    w.le(v.ready);
    w.le(v.unknown5);
    w.le(v.unknown6);
    w.le(v.unknown7);
    return make_packet(rof2::Op::TradeAcceptClick, std::move(w));
}

std::optional<TradeAccept> decode_trade_accept(const AppPacket& p) {
    if (p.data.size() != kTradeAcceptSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        TradeAccept v;
        v.partner_id = r.le<std::uint32_t>();
        v.ready = r.le<std::uint8_t>();
        v.unknown5 = r.le<std::uint8_t>();
        v.unknown6 = r.le<std::uint8_t>();
        v.unknown7 = r.le<std::uint8_t>();
        return v;
    });
}

AppPacket encode(const CancelTrade& v) {
    ByteWriter w;
    w.le(v.from_id);
    w.le(v.action);
    return make_packet(rof2::Op::CancelTrade, std::move(w));
}

std::optional<CancelTrade> decode_cancel_trade(const AppPacket& p) {
    if (p.data.size() != kCancelTradeSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        CancelTrade v;
        v.from_id = r.le<std::uint32_t>();
        v.action = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const TradeCoins& v) {
    ByteWriter w;
    w.le(v.trader);
    w.le(v.type);
    w.le(v.unknown5);
    w.le(v.unknown7);
    w.le(v.amount);
    return make_packet(rof2::Op::TradeCoins, std::move(w));
}

std::optional<TradeCoins> decode_trade_coins(const AppPacket& p) {
    if (p.data.size() != kTradeCoinsSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        TradeCoins v;
        v.trader = r.le<std::uint32_t>();
        v.type = r.le<std::uint8_t>();
        v.unknown5 = r.le<std::uint16_t>();
        v.unknown7 = r.le<std::uint8_t>();
        v.amount = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const MoveCoin& v) {
    ByteWriter w;
    w.le(v.from);
    w.le(v.to);
    w.le(v.type_from);
    w.le(v.type_to);
    w.le(v.amount);
    return make_packet(rof2::Op::MoveCoin, std::move(w));
}

std::optional<MoveCoin> decode_move_coin(const AppPacket& p) {
    if (p.data.size() != kMoveCoinSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        MoveCoin v;
        v.from = r.le<std::int32_t>();
        v.to = r.le<std::int32_t>();
        v.type_from = r.le<std::int32_t>();
        v.type_to = r.le<std::int32_t>();
        v.amount = r.le<std::int32_t>();
        return v;
    });
}

AppPacket encode(const TradeBusy& v) {
    ByteWriter w;
    w.le(v.to_id);
    w.le(v.from_id);
    w.le(v.type);
    w.le(v.unknown9);
    w.le(v.unknown10);
    w.le(v.unknown11);
    return make_packet(rof2::Op::TradeBusy, std::move(w));
}

std::optional<TradeBusy> decode_trade_busy(const AppPacket& p) {
    if (p.data.size() != kTradeBusySize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        TradeBusy v;
        v.to_id = r.le<std::uint32_t>();
        v.from_id = r.le<std::uint32_t>();
        v.type = r.le<std::uint8_t>();
        v.unknown9 = r.le<std::uint8_t>();
        v.unknown10 = r.le<std::uint8_t>();
        v.unknown11 = r.le<std::uint8_t>();
        return v;
    });
}

} // namespace hailstone::net
