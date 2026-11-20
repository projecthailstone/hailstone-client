#include "net/protocol/rof2/packets/merchant_packets.h"

#include <algorithm>

#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;

AppPacket encode(const MerchantClick& v) {
    ByteWriter w;
    w.le(v.npc_id);
    w.le(v.player_id);
    w.le(v.command);
    w.f32(v.rate);
    w.le(v.tab_display);
    w.le(v.unknown02);
    return make_packet(rof2::Op::ShopRequest, std::move(w));
}

std::optional<MerchantClick> decode_merchant_click(const AppPacket& p) {
    if (p.data.size() != kMerchantClickSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        MerchantClick v;
        v.npc_id = r.le<std::uint32_t>();
        v.player_id = r.le<std::uint32_t>();
        v.command = r.le<std::uint32_t>();
        v.rate = r.f32();
        v.tab_display = r.le<std::int32_t>();
        v.unknown02 = r.le<std::int32_t>();
        return v;
    });
}

AppPacket encode(const MerchantBuy& v) {
    ByteWriter w;
    w.le(v.npc_id);
    w.le(v.player_id);
    w.le(v.item_slot);
    w.le(v.unknown12);
    w.le(v.quantity);
    w.le(v.unknown20);
    w.le(v.price);
    w.le(v.unknown28);
    return make_packet(rof2::Op::ShopPlayerBuy, std::move(w));
}

std::optional<MerchantBuy> decode_merchant_buy(const AppPacket& p) {
    if (p.data.size() != kMerchantBuySize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        MerchantBuy v;
        v.npc_id = r.le<std::uint32_t>();
        v.player_id = r.le<std::uint32_t>();
        v.item_slot = r.le<std::uint32_t>();
        v.unknown12 = r.le<std::uint32_t>();
        v.quantity = r.le<std::uint32_t>();
        v.unknown20 = r.le<std::uint32_t>();
        v.price = r.le<std::uint32_t>();
        v.unknown28 = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const MerchantSell& v) {
    ByteWriter w;
    w.le(v.npc_id);
    w.le(v.slot);
    w.le(v.sub);
    w.le(v.aug);
    w.le(v.unknown01);
    w.le(v.quantity);
    w.le(v.price);
    return make_packet(rof2::Op::ShopPlayerSell, std::move(w));
}

std::optional<MerchantSell> decode_merchant_sell(const AppPacket& p) {
    if (p.data.size() != kMerchantSellSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        MerchantSell v;
        v.npc_id = r.le<std::uint32_t>();
        v.slot = r.le<std::int16_t>();
        v.sub = r.le<std::int16_t>();
        v.aug = r.le<std::int16_t>();
        v.unknown01 = r.le<std::int16_t>();
        v.quantity = r.le<std::uint32_t>();
        v.price = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_shop_end(std::uint32_t npc_id, std::uint32_t player_id) {
    ByteWriter w;
    w.le(npc_id);
    w.le(player_id);
    return make_packet(rof2::Op::ShopEnd, std::move(w));
}

AppPacket encode(const ParcelRetrieve& v) {
    ByteWriter w;
    w.le(v.merchant_id);
    w.le(v.player_id);
    w.le(v.parcel_slot);
    w.le(v.item_id);
    return make_packet(rof2::Op::ShopRetrieveParcel, std::move(w));
}

AppPacket encode(const ParcelSend& v) {
    ByteWriter w;
    w.le(v.npc_id);
    w.le(v.slot);
    w.le(v.sub);
    w.le(std::int16_t{-1}); // the augment
    w.le(std::int16_t{-1});
    w.le(v.quantity);
    w.le(v.money_flag);
    w.fixed_string(std::string_view(v.to).substr(0, 63), 64);
    w.fixed_string(std::string_view(v.note).substr(0, 127), 128);
    w.zeros(12);
    return make_packet(rof2::Op::ShopSendParcel, std::move(w));
}

std::optional<ParcelIconStatus> decode_parcel_icon(const AppPacket& p) {
    if (p.data.size() != kParcelIconSize) return std::nullopt;
    const auto value = decode_with(p, [](ByteReader& r) { return r.le<std::uint32_t>(); });
    if (!value || *value > static_cast<std::uint32_t>(ParcelIconStatus::OverLimit)) return std::nullopt;
    return static_cast<ParcelIconStatus>(*value);
}

std::optional<ParcelRetrieve> decode_parcel_retrieve(const AppPacket& p) {
    if (p.data.size() < kParcelRetrieveSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        ParcelRetrieve v;
        v.merchant_id = r.le<std::uint32_t>();
        v.player_id = r.le<std::uint32_t>();
        v.parcel_slot = r.le<std::uint32_t>();
        v.item_id = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const MerchantDelItem& v) {
    ByteWriter w;
    w.le(v.npc_id);
    w.le(v.player_id);
    w.le(v.item_slot);
    return make_packet(rof2::Op::ShopDelItem, std::move(w));
}

std::optional<MerchantDelItem> decode_merchant_del_item(const AppPacket& p) {
    // The original reads +8 and +12; EQEmu sends 12 bytes.
    if (p.data.size() < kMerchantDelItemSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        MerchantDelItem v;
        v.npc_id = r.le<std::uint32_t>();
        v.player_id = r.le<std::uint32_t>();
        v.item_slot = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<AltCurrencyPacket> decode_alt_currency(const AppPacket& p) {
    if (p.data.size() < 8) return std::nullopt;
    return detail::decode_with(p, [&](ByteReader& r) {
        AltCurrencyPacket v;
        std::uint32_t mode = r.le<std::uint32_t>();
        if (mode == kAltCurrencyPopulate) {
            std::uint32_t n = r.le<std::uint32_t>();
            if (static_cast<std::uint64_t>(n) * 25 + 8 > p.data.size()) throw std::out_of_range("alt currency count");
            for (std::uint32_t k = 0; k < n; ++k) {
                AltCurrencyEntry e;
                e.id = r.le<std::uint32_t>();
                r.le<std::uint32_t>();
                r.le<std::uint32_t>();
                e.item_id = r.le<std::uint32_t>();
                e.icon = r.le<std::uint32_t>();
                e.stack_size = r.le<std::uint32_t>();
                e.shown = r.le<std::uint8_t>() != 0;
                v.populate.push_back(e);
            }
        } else if (mode == kAltCurrencyUpdate) {
            r.fixed_string(64);
            AltCurrencyUpdate u;
            u.id = r.le<std::uint32_t>();
            r.le<std::uint32_t>();
            u.amount = r.le<std::uint32_t>();
            v.update = u;
        } else {
            throw std::out_of_range("alt currency mode");
        }
        return v;
    });
}

AppPacket encode_alt_currency_populate(const std::vector<AltCurrencyEntry>& v) {
    ByteWriter w;
    w.le(kAltCurrencyPopulate);
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& e : v) {
        w.le(e.id);
        w.le<std::uint32_t>(1);
        w.le(e.id);
        w.le(e.item_id);
        w.le(e.icon);
        w.le(e.stack_size);
        w.le<std::uint8_t>(e.shown ? 1 : 0);
    }
    return make_packet(rof2::Op::AltCurrency, std::move(w));
}

AppPacket encode_alt_currency_update(const AltCurrencyUpdate& v) {
    ByteWriter w;
    w.le(kAltCurrencyUpdate);
    w.zeros(64);
    w.le(v.id);
    w.le<std::uint32_t>(1);
    w.le(v.amount);
    w.zeros(8);
    return make_packet(rof2::Op::AltCurrency, std::move(w));
}

AppPacket encode_alt_currency_reclaim(std::uint32_t currency, std::uint32_t count, bool put_in) {
    ByteWriter w;
    w.le(currency);
    w.le<std::uint32_t>(0);
    w.le(count);
    w.le<std::uint32_t>(put_in ? 1 : 0);
    return make_packet(rof2::Op::AltCurrencyReclaim, std::move(w));
}

AppPacket encode_alt_currency_merchant_request(std::uint32_t npc) {
    ByteWriter w;
    w.le(npc);
    return make_packet(rof2::Op::AltCurrencyMerchantRequest, std::move(w));
}

std::optional<PointMerchantList> decode_point_merchant_list(const AppPacket& p) {
    std::string text;
    for (auto b : p.data) {
        if (b == std::byte{0}) break;
        text += static_cast<char>(b);
    }
    auto fields = [](std::string_view s) {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (true) {
            std::size_t bar = s.find('|', start);
            out.emplace_back(s.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start));
            if (bar == std::string_view::npos) break;
            start = bar + 1;
        }
        return out;
    };
    auto num = [](const std::string& s) { return static_cast<std::uint32_t>(std::strtoul(s.c_str(), nullptr, 10)); };
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t caret = text.find('^', start);
        parts.push_back(text.substr(start, caret == std::string::npos ? std::string::npos : caret - start));
        if (caret == std::string::npos) break;
        start = caret + 1;
    }
    auto head = fields(parts[0]);
    if (head.size() < 4) return std::nullopt;
    PointMerchantList v;
    v.currency = num(head[0]);
    for (std::size_t k = 1; k < parts.size(); ++k) {
        auto f = fields(parts[k]);
        if (f.size() < 7) continue;
        v.items.push_back({f[0], num(f[1]), num(f[2]), num(f[5]), num(f[6])});
    }
    return v;
}

AppPacket encode_alt_currency_purchase(std::uint32_t npc, std::uint32_t item_id) {
    ByteWriter w;
    w.le(npc);
    w.le(item_id);
    w.le<std::uint32_t>(1);
    return make_packet(rof2::Op::AltCurrencyPurchase, std::move(w));
}

namespace {

void typeless_slot(ByteWriter& w, std::int16_t slot, std::int16_t sub) {
    w.le(slot);
    w.le(sub);
    w.le<std::int16_t>(-1);
    w.le<std::int16_t>(0);
}

} // namespace

AppPacket encode_alt_currency_select(std::uint32_t npc, std::int16_t slot, std::int16_t sub) {
    ByteWriter w;
    w.le(npc);
    typeless_slot(w, slot, sub);
    w.zeros(68); // 17 unknown dwords: the RoF2 struct is 80 bytes and EQEmu checks the size
    return make_packet(rof2::Op::AltCurrencySellSelection, std::move(w));
}

std::optional<AltCurrencyQuote> decode_alt_currency_quote(const AppPacket& p) {
    if (p.data.size() < 80) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        AltCurrencyQuote v;
        r.skip(10);
        v.item_name = r.fixed_string(64);
        r.skip(2);
        v.cost = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_alt_currency_sell(std::uint32_t npc, std::int16_t slot, std::int16_t sub, std::uint32_t charges,
                                   std::uint32_t cost) {
    ByteWriter w;
    w.le(npc);
    typeless_slot(w, slot, sub);
    w.le(charges);
    w.le(cost);
    return make_packet(rof2::Op::AltCurrencySell, std::move(w));
}

std::optional<PointSale> decode_alt_currency_sale(const AppPacket& p) {
    if (p.data.size() < 20) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        PointSale v;
        v.npc = r.le<std::uint32_t>();
        v.slot = r.le<std::int16_t>();
        v.sub = r.le<std::int16_t>();
        r.skip(4);
        v.charges = r.le<std::uint32_t>();
        v.price = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<PointSale> decode_adventure_merchant_sale(const AppPacket& p) {
    if (p.data.size() < 24) return std::nullopt;
    AppPacket rest(p.opcode, std::vector<std::byte>(p.data.begin() + 4, p.data.end()));
    return decode_alt_currency_sale(rest);
}

AppPacket encode_adventure_merchant_request(std::uint32_t type, std::uint32_t npc) {
    ByteWriter w;
    w.le(type);
    w.le(npc);
    return make_packet(rof2::Op::AdventureMerchantRequest, std::move(w));
}

std::optional<std::vector<AdventureMerchantItem>> decode_adventure_merchant_list(const AppPacket& p) {
    if (p.data.empty()) return std::nullopt;
    std::string text;
    for (std::size_t i = 1; i < p.data.size() && p.data[i] != std::byte{0}; ++i) text += static_cast<char>(p.data[i]);
    auto num = [](const std::string& s) { return static_cast<std::uint32_t>(std::strtoul(s.c_str(), nullptr, 10)); };
    std::vector<AdventureMerchantItem> v;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t caret = text.find('^', start);
        std::string part = text.substr(start, caret == std::string::npos ? std::string::npos : caret - start);
        std::vector<std::string> f;
        std::size_t b = 0;
        while (true) {
            std::size_t bar = part.find('|', b);
            f.push_back(part.substr(b, bar == std::string::npos ? std::string::npos : bar - b));
            if (bar == std::string::npos) break;
            b = bar + 1;
        }
        if (f.size() >= 8)
            v.push_back({f[0], num(f[1]), num(f[2]), num(f[3]), num(f[6]), num(f[7]), f[4] == "1", f[5] == "1"});
        if (caret == std::string::npos) break;
        start = caret + 1;
    }
    return v;
}

AppPacket encode_adventure_merchant_purchase(std::uint32_t type, std::uint32_t npc, std::uint32_t item_id) {
    ByteWriter w;
    w.le(type);
    w.le(npc);
    w.le(item_id);
    w.le<std::uint32_t>(0);
    return make_packet(rof2::Op::AdventureMerchantPurchase, std::move(w));
}

AppPacket encode_adventure_merchant_sell(std::uint32_t npc, std::int16_t slot, std::int16_t sub, std::uint32_t charges) {
    ByteWriter w;
    w.le<std::uint32_t>(1);
    w.le(npc);
    typeless_slot(w, slot, sub);
    w.le(charges);
    w.le<std::uint32_t>(0);
    return make_packet(rof2::Op::AdventureMerchantSell, std::move(w));
}

} // namespace hailstone::net
