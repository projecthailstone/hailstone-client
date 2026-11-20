#include "net/protocol/rof2/packets/aa_packets.h"

#include <algorithm>

#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

namespace {

// IntArray_UnserializeCounted @0x4a3710: a count, then that many dwords.
std::vector<std::int32_t> read_counted(ByteReader& r) {
    auto n = r.le<std::uint32_t>();
    if (n > r.remaining() / 4) throw std::out_of_range("counted array past the end");
    std::vector<std::int32_t> v(n);
    for (auto& x : v) x = r.le<std::int32_t>();
    return v;
}

void write_counted(ByteWriter& w, const std::vector<std::int32_t>& v) {
    w.le(static_cast<std::uint32_t>(v.size()));
    for (std::int32_t x : v) w.le(x);
}

} // namespace

std::optional<game::AltAbility> decode_send_aa_table(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        game::AltAbility a;
        a.id = r.le<std::uint32_t>();
        a.unknown04 = r.le<std::uint8_t>();
        a.hotkey_sid = r.le<std::int32_t>();
        a.hotkey_sid2 = r.le<std::int32_t>();
        a.title_sid = r.le<std::int32_t>();
        a.desc_sid = r.le<std::int32_t>();
        a.level_req = r.le<std::int32_t>();
        a.cost = r.le<std::int32_t>();
        a.group = r.le<std::int32_t>();
        a.rank = r.le<std::int32_t>();
        a.prereq_groups = read_counted(r);
        a.prereq_ranks = read_counted(r);
        a.type = r.le<std::int32_t>();
        a.spell = r.le<std::int32_t>();
        a.timers = read_counted(r);
        a.refresh = r.le<std::int32_t>();
        a.classes = r.le<std::uint32_t>();
        a.max_rank = r.le<std::int32_t>();
        a.prev_id = r.le<std::int32_t>();
        a.next_id = r.le<std::int32_t>();
        a.total_cost = r.le<std::int32_t>();
        a.unknown50 = r.le<std::uint8_t>();
        a.grant_only = r.le<std::uint8_t>();
        a.ignores_level = r.le<std::uint8_t>();
        a.charges = r.le<std::uint32_t>();
        a.expansion = r.le<std::int32_t>();
        a.category = r.le<std::int32_t>();
        for (auto& f : a.flags) f = r.le<std::uint8_t>();
        auto n = r.le<std::uint32_t>();
        if (n > r.remaining() / 16) throw std::out_of_range("effects past the end");
        a.effects.resize(n);
        for (auto& e : a.effects) {
            e.spa = r.le<std::int32_t>();
            e.base = r.le<std::int32_t>();
            e.limit = r.le<std::int32_t>();
            e.slot = r.le<std::int32_t>();
        }
        return a;
    });
}

AppPacket encode_send_aa_table(const game::AltAbility& a) {
    ByteWriter w;
    w.le(a.id);
    w.le(a.unknown04);
    w.le(a.hotkey_sid);
    w.le(a.hotkey_sid2);
    w.le(a.title_sid);
    w.le(a.desc_sid);
    w.le(a.level_req);
    w.le(a.cost);
    w.le(a.group);
    w.le(a.rank);
    write_counted(w, a.prereq_groups);
    write_counted(w, a.prereq_ranks);
    w.le(a.type);
    w.le(a.spell);
    write_counted(w, a.timers);
    w.le(a.refresh);
    w.le(a.classes);
    w.le(a.max_rank);
    w.le(a.prev_id);
    w.le(a.next_id);
    w.le(a.total_cost);
    w.le(a.unknown50);
    w.le(a.grant_only);
    w.le(a.ignores_level);
    w.le(a.charges);
    w.le(a.expansion);
    w.le(a.category);
    for (auto f : a.flags) w.le(f);
    w.le(static_cast<std::uint32_t>(a.effects.size()));
    for (const auto& e : a.effects) {
        w.le(e.spa);
        w.le(e.base);
        w.le(e.limit);
        w.le(e.slot);
    }
    return make_packet(rof2::Op::SendAATable, std::move(w));
}

std::optional<RespondAA> decode_respond_aa(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        RespondAA v;
        v.spent = r.le<std::uint32_t>();
        for (auto& a : v.assigned) a = r.le<std::uint32_t>();
        // Handle_OP_RespondAA reads up to 300 slots and stops at the end of the packet. Deliberate difference:
        // every slot is read, for servers that send more than 300 (game::kOwnedAbilities).
        while (r.remaining() >= 12) {
            game::OwnedAbility o;
            o.id = r.le<std::uint32_t>();
            o.value = r.le<std::uint32_t>();
            o.charges = r.le<std::uint32_t>();
            if (o.id != 0) v.owned.push_back(o);
        }
        return v;
    });
}

AppPacket encode(const RespondAA& v) {
    ByteWriter w;
    w.le(v.spent);
    for (auto a : v.assigned) w.le(a);
    const std::size_t slots = std::max(v.owned.size(), static_cast<std::size_t>(game::kOwnedAbilities));
    for (std::size_t i = 0; i < slots; ++i) {
        game::OwnedAbility o = i < v.owned.size() ? v.owned[i] : game::OwnedAbility{};
        w.le(o.id);
        w.le(o.value);
        w.le(o.charges);
    }
    return make_packet(rof2::Op::RespondAA, std::move(w));
}

std::optional<AAExpUpdate> decode_aa_exp_update(const AppPacket& p) {
    if (p.data.size() < 9) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        AAExpUpdate v;
        v.exp = r.le<std::uint32_t>();
        v.points = r.le<std::uint32_t>();
        v.percent = r.le<std::uint8_t>();
        return v;
    });
}

AppPacket encode(const AAExpUpdate& v) {
    ByteWriter w;
    w.le(v.exp);
    w.le(v.points);
    w.le(v.percent);
    w.zeros(3);
    return make_packet(rof2::Op::AAExpUpdate, std::move(w));
}

AppPacket encode(const AAAction& v) {
    ByteWriter w;
    w.le(v.action);
    w.le(v.ability);
    w.le(v.target);
    w.le(v.value);
    return make_packet(rof2::Op::AAAction, std::move(w));
}

std::optional<AAAction> decode_aa_action(const AppPacket& p) {
    if (p.data.size() != 16) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        AAAction v;
        v.action = r.le<std::uint32_t>();
        v.ability = r.le<std::uint32_t>();
        v.target = r.le<std::uint32_t>();
        v.value = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<AATimer> decode_aa_timer(const AppPacket& p) {
    if (p.data.size() < 12) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        AATimer v;
        v.begin = r.le<std::uint32_t>();
        v.timer = r.le<std::uint32_t>();
        v.server_now = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const AATimer& v) {
    ByteWriter w;
    w.le(v.begin);
    w.le(v.timer);
    w.le(v.server_now);
    return make_packet(rof2::Op::AAAction, std::move(w));
}

} // namespace hailstone::net
