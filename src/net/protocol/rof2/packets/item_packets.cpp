// Written from EQEmu rof2.cpp / rof2_structs.h (read as reference, no code
// copied) and the original client's item handling. OpenEQ crates/openeq-net/src/inventory.rs
// was studied to cross-check the block sizes (THIRD_PARTY.md).

#include "net/protocol/rof2/packets/item_packets.h"
#include "net/wire/item_serialization.h"

#include <algorithm>
#include <stdexcept>

#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;

namespace {

constexpr std::uint32_t kMaxInventoryItems = 4096;
// Bound OP_CharInventory allocation by the number of complete items that could fit.
constexpr std::size_t kMinItemBytes = 64;
std::size_t reserve_for(std::uint32_t count, const ByteReader& r, std::size_t per_entry) {
    return std::min<std::size_t>(count, r.remaining() / per_entry);
}

} // namespace

// ---------------------------------------------------------------- slots

void write_slot(ByteWriter& w, const ItemSlot& s) {
    w.le(s.type);
    w.le(s.unknown02);
    w.le(s.slot);
    w.le(s.sub);
    w.le(s.aug);
    w.le(s.unknown01);
}

ItemSlot read_slot(ByteReader& r) {
    ItemSlot s;
    s.type = r.le<std::int16_t>();
    s.unknown02 = r.le<std::int16_t>();
    s.slot = r.le<std::int16_t>();
    s.sub = r.le<std::int16_t>();
    s.aug = r.le<std::int16_t>();
    s.unknown01 = r.le<std::int16_t>();
    return s;
}

// ---------------------------------------------------------------- items

std::optional<std::vector<Item>> decode_char_inventory(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) -> std::vector<Item> {
        std::uint32_t count = r.le<std::uint32_t>();
        if (count > kMaxInventoryItems) throw std::out_of_range("too many items");
        std::vector<Item> items;
        items.reserve(reserve_for(count, r, kMinItemBytes));
        for (std::uint32_t i = 0; i < count; ++i) items.push_back(read_item(r));
        if (!r.empty()) throw std::out_of_range("trailing bytes after the inventory");
        return items;
    });
}

AppPacket encode_char_inventory(const std::vector<Item>& items) {
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(items.size()));
    for (const auto& it : items) write_item(w, it);
    return make_packet(rof2::Op::CharInventory, std::move(w));
}

std::optional<ItemPacket> decode_item_packet(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        ItemPacket v;
        v.type = r.le<std::uint32_t>();
        v.item = read_item(r);
        if (v.type == kItemPacketParcel) {
            v.parcel_sent_time = r.le<std::uint32_t>();
            auto n = r.le<std::uint32_t>();
            auto s = r.bytes(n);
            v.parcel_from.assign(reinterpret_cast<const char*>(s.data()), s.size());
            n = r.le<std::uint32_t>();
            s = r.bytes(n);
            v.parcel_note.assign(reinterpret_cast<const char*>(s.data()), s.size());
        }
        if (!r.empty()) throw std::out_of_range("trailing bytes after the item");
        return v;
    });
}

AppPacket encode_item_packet(bool link_response, const ItemPacket& v) {
    ByteWriter w;
    w.le(v.type);
    write_item(w, v.item);
    if (v.type == kItemPacketParcel) {
        w.le(v.parcel_sent_time);
        w.le(static_cast<std::uint32_t>(v.parcel_from.size()));
        w.bytes({reinterpret_cast<const std::byte*>(v.parcel_from.data()), v.parcel_from.size()});
        w.le(static_cast<std::uint32_t>(v.parcel_note.size()));
        w.bytes({reinterpret_cast<const std::byte*>(v.parcel_note.data()), v.parcel_note.size()});
    }
    return make_packet(link_response ? rof2::Op::ItemLinkResponse : rof2::Op::ItemPacket, std::move(w));
}

// ---------------------------------------------------------------- moves

namespace {
ByteWriter write_move(const MoveItem& v) {
    ByteWriter w;
    write_slot(w, v.from);
    write_slot(w, v.to);
    w.le(v.count);
    return w;
}
} // namespace

AppPacket encode_item_preview_request(std::uint32_t item_id, const Item* item) {
    ByteWriter w;
    w.le(item_id);
    const bool ornamented = item && (!item->ornament_main.empty() || !item->ornament_off.empty() ||
                                     item->ornament_icon != 0 || item->unknowna1 >= 0 ||
                                     item->ornament_hero_model != 0 || item->unknown063 != 0);
    auto text = [&](const std::string& s) {
        std::array<std::byte, 30> b{};
        if (ornamented)
            for (std::size_t k = 0; k < s.size() && k < 29; ++k) b[k] = static_cast<std::byte>(s[k]); // steve_strncpy
        w.bytes(b);
    };
    text(ornamented ? item->ornament_main : std::string());
    text(ornamented ? item->ornament_off : std::string());
    w.le(ornamented ? item->unknowna1 : -1);
    w.le(ornamented ? item->ornament_hero_model : 0u);
    w.le(ornamented ? static_cast<std::uint32_t>(item->unknown063) : 0u);
    w.le(std::uint8_t{0});
    w.le(static_cast<std::uint8_t>(ornamented ? 1 : 0));
    w.le(std::uint16_t{0});
    return make_packet(rof2::Op::ItemPreviewRequest, std::move(w));
}

std::optional<ItemPreviewReply> decode_item_preview(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        ItemPreviewReply v;
        v.item = read_item(r);
        if (!r.empty()) v.trailing = r.le<std::uint8_t>();
        return v;
    });
}

AppPacket encode(const ItemPreviewReply& v) {
    ByteWriter w;
    write_item(w, v.item);
    w.le(v.trailing);
    return make_packet(rof2::Op::ItemPreviewRequest, std::move(w));
}

AppPacket encode(const MoveItem& v) { return make_packet(rof2::Op::MoveItem, write_move(v)); }
AppPacket encode_delete_item(const MoveItem& v) { return make_packet(rof2::Op::DeleteItem, write_move(v)); }

std::optional<MoveItem> decode_move_item(const AppPacket& p) {
    if (p.data.size() != kMoveItemSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        MoveItem v;
        v.from = read_slot(r);
        v.to = read_slot(r);
        v.count = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<AppPacket> encode_move_multiple(const std::vector<MultiMoveEntry>& moves) {
    if (moves.empty()) return std::nullopt;
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(moves.size()));
    for (const MultiMoveEntry& m : moves) {
        write_slot(w, m.from);
        write_slot(w, m.to);
        w.le(m.count);
        w.le(m.flags);
        w.le(std::uint32_t{0});
    }
    return make_packet(rof2::Op::MoveMultipleItems, std::move(w));
}

// ---------------------------------------------------------------- loot

AppPacket encode_loot_request(std::uint32_t corpse_id) {
    ByteWriter w;
    w.le(corpse_id);
    return make_packet(rof2::Op::LootRequest, std::move(w));
}

AppPacket encode_end_loot_request(std::uint32_t corpse_id) {
    ByteWriter w;
    w.le(corpse_id);
    return make_packet(rof2::Op::EndLootRequest, std::move(w));
}

std::optional<std::uint32_t> decode_loot_request(const AppPacket& p) {
    if (p.data.size() != 4) return std::nullopt;
    return decode_with(p, [](ByteReader& r) { return r.le<std::uint32_t>(); });
}

AppPacket encode(const LootItem& v) {
    ByteWriter w;
    w.le(v.lootee);
    w.le(v.looter);
    w.le(v.slot);
    w.le(v.unknown10);
    w.le(v.auto_loot);
    w.le(v.unknown16);
    return make_packet(rof2::Op::LootItem, std::move(w));
}

std::optional<LootItem> decode_loot_item(const AppPacket& p) {
    if (p.data.size() != kLootItemSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        LootItem v;
        v.lootee = r.le<std::uint32_t>();
        v.looter = r.le<std::uint32_t>();
        v.slot = r.le<std::uint16_t>();
        v.unknown10 = r.le<std::uint16_t>();
        v.auto_loot = r.le<std::int32_t>();
        v.unknown16 = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const MoneyOnCorpse& v) {
    ByteWriter w;
    w.le(v.response);
    w.le(v.unknown1);
    w.le(v.unknown2);
    w.le(v.unknown3);
    w.le(v.platinum);
    w.le(v.gold);
    w.le(v.silver);
    w.le(v.copper);
    return make_packet(rof2::Op::MoneyOnCorpse, std::move(w));
}

std::optional<MoneyOnCorpse> decode_money_on_corpse(const AppPacket& p) {
    if (p.data.size() != kMoneyOnCorpseSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        MoneyOnCorpse v;
        v.response = r.le<std::uint8_t>();
        v.unknown1 = r.le<std::uint8_t>();
        v.unknown2 = r.le<std::uint8_t>();
        v.unknown3 = r.le<std::uint8_t>();
        v.platinum = r.le<std::uint32_t>();
        v.gold = r.le<std::uint32_t>();
        v.silver = r.le<std::uint32_t>();
        v.copper = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_split_coin(const Coins& v) {
    ByteWriter w;
    for (std::uint32_t c : {v.platinum, v.gold, v.silver, v.copper}) w.le(c);
    return AppPacket(kOpSplitCoin, w.take());
}

std::optional<Coins> decode_split_coin(const AppPacket& p) {
    if (p.opcode != kOpSplitCoin || p.data.size() < 16) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        Coins v;
        v.platinum = r.le<std::uint32_t>();
        v.gold = r.le<std::uint32_t>();
        v.silver = r.le<std::uint32_t>();
        v.copper = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<Coins> decode_money_update(const AppPacket& p) {
    if (p.data.size() != 16) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        Coins v;
        v.platinum = r.le<std::uint32_t>();
        v.gold = r.le<std::uint32_t>();
        v.silver = r.le<std::uint32_t>();
        v.copper = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_money_update(const Coins& v) {
    ByteWriter w;
    w.le(v.platinum);
    w.le(v.gold);
    w.le(v.silver);
    w.le(v.copper);
    return make_packet(rof2::Op::MoneyUpdate, std::move(w));
}

// ---------------------------------------------------------------- item links

AppPacket encode(const ItemLinkClick& v) {
    ByteWriter w;
    w.le(v.item_id);
    for (auto a : v.augments) w.le(a);
    w.le(v.hash);
    w.le(v.flags);
    w.le(v.is_evolving);
    w.le(v.evolve_group);
    w.le(v.evolve_level);
    w.le(v.ornament_icon);
    return make_packet(rof2::Op::ItemLinkClick, std::move(w));
}

AppPacket encode(const ItemLinkRequest& v) {
    ByteWriter w;
    w.le(v.item_id);
    w.le(v.flags);
    w.fixed_string(v.name.substr(0, 63), 64);
    return make_packet(rof2::Op::ItemLinkResponse, std::move(w));
}

std::optional<ItemLinkClick> decode_item_link_click(const AppPacket& p) {
    if (p.data.size() != kItemLinkClickSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        ItemLinkClick v;
        v.item_id = r.le<std::uint32_t>();
        for (auto& a : v.augments) a = r.le<std::uint32_t>();
        v.hash = r.le<std::uint32_t>();
        v.flags = r.le<std::uint32_t>();
        v.is_evolving = r.le<std::uint32_t>();
        v.evolve_group = r.le<std::uint32_t>();
        v.evolve_level = r.le<std::uint32_t>();
        v.ornament_icon = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode_consume(const ItemSlot& slot, bool automatic, bool drink) {
    ByteWriter w;
    write_slot(w, slot);
    w.le<std::uint32_t>(automatic ? 0xffffffffu : 999u);
    w.le<std::uint32_t>(drink ? 2 : 1);
    w.le<std::uint32_t>(2);
    return make_packet(rof2::Op::Consume, std::move(w));
}

AppPacket encode_consume_reply(const ItemSlot& slot, bool drink) {
    ByteWriter w;
    write_slot(w, slot);
    w.le<std::uint32_t>(0xffffffffu);
    w.le<std::uint32_t>(drink ? 2 : 1);
    w.le<std::uint32_t>(1);
    return make_packet(rof2::Op::Consume, std::move(w));
}

AppPacket encode_item_verify_request(const ItemSlot& slot, std::uint32_t target_id) {
    ByteWriter w;
    write_slot(w, slot);
    w.le(target_id);
    return make_packet(rof2::Op::ItemVerifyRequest, std::move(w));
}

std::optional<ItemVerifyReply> decode_item_verify_reply(const AppPacket& p) {
    if (p.data.size() < kItemVerifyReplySize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        ItemVerifyReply v;
        v.slot = read_slot(r);
        v.spell = r.le<std::int32_t>();
        v.target_id = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const ItemVerifyReply& v) {
    ByteWriter w;
    write_slot(w, v.slot);
    w.le(v.spell);
    w.le(v.target_id);
    return make_packet(rof2::Op::ItemVerifyReply, std::move(w));
}

AppPacket encode(const ApplyPoison& v) {
    ByteWriter w;
    w.le(v.slot);
    w.le(v.sub);
    w.le(v.aug);
    w.le(v.unknown);
    w.le(v.success);
    return make_packet(rof2::Op::ApplyPoison, std::move(w));
}

AppPacket encode(const EvolvingFinalResult& v) {
    // CItemDisplayWnd's 0x69ef60: hyperlink action 1 uses this action-specific layout, not the toggle's fields.
    ByteWriter w;
    w.le<std::uint32_t>(4);
    w.le(v.item_id);
    w.le(v.final_item_id);
    w.le(v.level);
    w.le(v.hash);
    w.le(v.display_flags);
    return make_packet(rof2::Op::EvolveItem, std::move(w));
}

std::optional<EvolvingItemUpdate> decode_evolving_item_update(const AppPacket& p) {
    if (p.data.size() != 24) return std::nullopt;
    auto update = decode_with(p, [](ByteReader& r) {
        const auto action = r.le<std::uint32_t>();
        r.skip(4);
        EvolvingItemUpdate v;
        v.unique_id = r.le<std::uint64_t>();
        v.percentage = r.le<std::uint32_t>();
        v.activated = r.le<std::uint32_t>();
        // A large XP reward can overshoot 100 before the replacement arrives. Native reads a signed dword.
        if (action != 0 || v.unique_id == 0 || v.percentage > 0x7fffffffu || v.activated > 1)
            throw std::out_of_range("invalid evolving item update");
        return v;
    });
    return update;
}

std::optional<ApplyPoison> decode_apply_poison(const AppPacket& p) {
    if (p.data.size() < kApplyPoisonSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        ApplyPoison v;
        v.slot = r.le<std::int16_t>();
        v.sub = r.le<std::int16_t>();
        v.aug = r.le<std::int16_t>();
        v.unknown = r.le<std::int16_t>();
        v.success = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<CoinReward> decode_quest_reward(const AppPacket& p) {
    if (p.data.size() < 36) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        CoinReward v;
        r.skip(4);
        v.target_id = r.le<std::uint32_t>();
        r.skip(12);
        v.coins.copper = r.le<std::uint32_t>();
        v.coins.silver = r.le<std::uint32_t>();
        v.coins.gold = r.le<std::uint32_t>();
        v.coins.platinum = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<CoinReward> decode_cash_reward(const AppPacket& p) {
    if (p.data.size() < 16) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        CoinReward v;
        v.coins.copper = r.le<std::uint32_t>();
        v.coins.silver = r.le<std::uint32_t>();
        v.coins.gold = r.le<std::uint32_t>();
        v.coins.platinum = r.le<std::uint32_t>();
        return v;
    });
}

} // namespace hailstone::net
