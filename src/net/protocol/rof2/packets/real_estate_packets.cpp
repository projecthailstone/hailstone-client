#include "net/protocol/rof2/packets/real_estate_packets.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "core/byte_io.h"
#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;
using detail::write_cstring;

namespace {

// SerializeBuffer_ReadCXStr @0x80c9b0: u32 length, the bytes (no NUL; 0 allowed).
std::string lstr(ByteReader& r) {
    auto n = r.le<std::uint32_t>();
    if (n > r.remaining()) throw std::out_of_range("real estate string past the end");
    auto s = r.bytes(n);
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}
void lstr(ByteWriter& w, std::string_view s) {
    w.le(static_cast<std::uint32_t>(s.size()));
    w.bytes({reinterpret_cast<const std::byte*>(s.data()), s.size()});
}
// A NUL-terminated string the original copies with a cap (cap - 1 characters kept).
std::string capped(ByteReader& r, std::size_t cap) {
    std::string s = r.cstring();
    if (s.size() > cap - 1) s.resize(cap - 1);
    return s;
}
// A count before n items of at least `each` bytes: one that cannot fit is refused before any allocation.
std::uint32_t count(ByteReader& r, std::size_t each) {
    auto n = r.le<std::uint32_t>();
    if (each != 0 && n > r.remaining() / each) throw std::out_of_range("real estate count past the end");
    return n;
}
std::string fixed_text(std::span<const std::byte> body, std::size_t at, std::size_t n) {
    std::string s(reinterpret_cast<const char*>(body.data() + at), n);
    return s.substr(0, s.find('\0'));
}
void put_text(std::vector<std::byte>& body, std::size_t at, std::size_t n, std::string_view s) {
    std::size_t k = std::min(s.size(), n - 1);
    if (k != 0) std::memcpy(body.data() + at, s.data(), k); // an empty view may have a null data()
}
std::uint32_t get_u32(std::span<const std::byte> body, std::size_t at) {
    ByteReader r(body.subspan(at, 4));
    return r.le<std::uint32_t>();
}
void put_u32(std::vector<std::byte>& body, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) body[at + static_cast<std::size_t>(i)] = static_cast<std::byte>((v >> (8 * i)) & 0xff);
}
void put_f32(std::vector<std::byte>& body, std::size_t at, float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    put_u32(body, at, v);
}

void write_lstr_list(ByteWriter& w, const std::vector<std::uint32_t>& ids) {
    w.le(static_cast<std::uint32_t>(ids.size()));
    for (auto id : ids) w.le(id);
}

ReDefinition read_definition(ByteReader& r) {
    ReDefinition d;
    d.id = r.le<std::uint32_t>();
    d.f01 = r.le<std::int32_t>();
    d.price_group = r.le<std::uint32_t>();
    d.upkeep_group = r.le<std::uint32_t>();
    d.capacity = r.le<std::int32_t>();
    d.vault_capacity = r.le<std::int32_t>();
    d.f06 = r.le<std::uint32_t>();
    d.f07 = r.le<std::uint32_t>();
    d.f08 = r.le<std::uint32_t>();
    d.type = static_cast<ReType>(r.le<std::uint32_t>());
    d.category = r.le<std::uint32_t>();
    d.name = lstr(r);
    d.f11 = r.le<std::uint32_t>();
    d.pet_capacity = r.le<std::int32_t>();
    for (auto& f : d.f13_16) f = r.le<std::uint32_t>();
    auto n = count(r, 4);
    d.ids.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) d.ids.push_back(r.le<std::uint32_t>());
    return d;
}
void write_definition(ByteWriter& w, const ReDefinition& d) {
    w.le(d.id);
    w.le(d.f01);
    w.le(d.price_group);
    w.le(d.upkeep_group);
    w.le(d.capacity);
    w.le(d.vault_capacity);
    w.le(d.f06);
    w.le(d.f07);
    w.le(d.f08);
    w.le(static_cast<std::uint32_t>(d.type));
    w.le(d.category);
    lstr(w, d.name);
    w.le(d.f11);
    w.le(d.pet_capacity);
    for (auto f : d.f13_16) w.le(f);
    write_lstr_list(w, d.ids);
}

ReAccess read_access(ByteReader& r) {
    ReAccess a;
    for (auto& g : a.groups) {
        g.group = r.le<std::uint32_t>();
        g.bits = r.le<std::uint32_t>();
    }
    auto n = count(r, 8);
    for (std::uint32_t i = 0; i < n; ++i) {
        ReAccess::Member m;
        m.name = lstr(r);
        m.group = r.le<std::uint32_t>();
        a.members.push_back(std::move(m));
    }
    return a;
}
void write_access(ByteWriter& w, const ReAccess& a) {
    for (const auto& g : a.groups) {
        w.le(g.group);
        w.le(g.bits);
    }
    w.le(static_cast<std::uint32_t>(a.members.size()));
    for (const auto& m : a.members) {
        lstr(w, m.name);
        w.le(m.group);
    }
}

// The fixed bodies: zero-filled to `size`, refused when shorter than `min`.
std::vector<std::byte> body(std::size_t size) { return std::vector<std::byte>(size, std::byte{0}); }

} // namespace

ReRecord read_re_record(ByteReader& r) {
    ReRecord v;
    v.id = r.le<std::uint32_t>();
    v.f18 = r.le<std::uint32_t>();
    v.parent = r.le<std::uint32_t>();
    v.definition = r.le<std::uint32_t>();
    v.guild_id = r.le<std::int32_t>();
    v.f28 = r.le<std::uint32_t>();
    v.address = lstr(r);
    v.owner = lstr(r);
    v.f34 = r.le<std::uint32_t>();
    auto n = count(r, 4);
    v.name = lstr(r);
    if (n > r.remaining() / 4) throw std::out_of_range("real estate children past the end");
    v.children.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) v.children.push_back(r.le<std::uint32_t>());
    auto m = r.le<std::uint8_t>();
    for (std::uint32_t i = 0; i < m; ++i) {
        ReRecord::Balance b;
        b.currency = r.le<std::uint32_t>();
        b.amount = r.f64();
        v.escrow.push_back(b);
    }
    v.upkeep_set = r.le<std::uint32_t>();
    v.f40 = r.le<std::uint32_t>();
    v.opt_out = r.le<std::uint8_t>() != 0;
    return v;
}

void write_re_record(ByteWriter& w, const ReRecord& v) {
    w.le(v.id);
    w.le(v.f18);
    w.le(v.parent);
    w.le(v.definition);
    w.le(v.guild_id);
    w.le(v.f28);
    lstr(w, v.address);
    lstr(w, v.owner);
    w.le(v.f34);
    w.le(static_cast<std::uint32_t>(v.children.size()));
    lstr(w, v.name);
    for (auto c : v.children) w.le(c);
    w.le(static_cast<std::uint8_t>(std::min<std::size_t>(v.escrow.size(), 255)));
    for (std::size_t i = 0; i < v.escrow.size() && i < 255; ++i) {
        w.le(v.escrow[i].currency);
        w.f64(v.escrow[i].amount);
    }
    w.le(v.upkeep_set);
    w.le(v.f40);
    w.le(static_cast<std::uint8_t>(v.opt_out ? 1 : 0));
}

std::optional<ReRecord> decode_re_record(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) { return read_re_record(r); });
}

AppPacket encode_re_record(std::uint16_t op, const ReRecord& v) {
    ByteWriter w;
    write_re_record(w, v);
    return AppPacket(op, w.take());
}

std::optional<ReCatalog> decode_re_catalog(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        ReCatalog v;
        auto n = count(r, 4 * 18);
        for (std::uint32_t i = 0; i < n; ++i) v.definitions.push_back(read_definition(r));
        auto m = count(r, 8);
        for (std::uint32_t i = 0; i < m; ++i) {
            ReCostSet set;
            set.id = r.le<std::uint32_t>();
            auto e = count(r, 8);
            for (std::uint32_t j = 0; j < e; ++j) {
                ReCostEntry entry;
                entry.key = r.le<std::uint32_t>();
                auto l = count(r, 16);
                for (std::uint32_t k = 0; k < l; ++k) {
                    ReCostLeaf leaf;
                    leaf.currency = r.le<std::uint32_t>();
                    leaf.sub = r.le<std::uint32_t>();
                    leaf.amount = r.le<std::uint64_t>();
                    entry.leaves.push_back(leaf);
                }
                set.entries.push_back(std::move(entry));
            }
            v.costs.push_back(std::move(set));
        }
        return v;
    });
}

AppPacket encode(const ReCatalog& v) {
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(v.definitions.size()));
    for (const auto& d : v.definitions) write_definition(w, d);
    w.le(static_cast<std::uint32_t>(v.costs.size()));
    for (const auto& set : v.costs) {
        w.le(set.id);
        w.le(static_cast<std::uint32_t>(set.entries.size()));
        for (const auto& e : set.entries) {
            w.le(e.key);
            w.le(static_cast<std::uint32_t>(e.leaves.size()));
            for (const auto& l : e.leaves) {
                w.le(l.currency);
                w.le(l.sub);
                w.le(l.amount);
            }
        }
    }
    return AppPacket(kOpReCatalog, w.take());
}

std::optional<ReAccessMessage> decode_re_access(const AppPacket& p) {
    bool with_tail = p.opcode == kOpReAccess;
    return decode_with(p, [with_tail](ByteReader& r) {
        ReAccessMessage v;
        v.re = r.le<std::uint32_t>();
        v.access = read_access(r);
        // 0x3968 reads its last dword only when it is there.
        if (with_tail && r.remaining() >= 4) v.tail = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(std::uint16_t op, const ReAccessMessage& v) {
    ByteWriter w;
    w.le(v.re);
    write_access(w, v.access);
    if (v.tail) w.le(*v.tail);
    return AppPacket(op, w.take());
}

ReItemRow read_re_item_row(ByteReader& r) {
    ReItemRow v;
    v.re = r.le<std::uint32_t>();
    v.item_id = r.le<std::uint32_t>();
    v.placed = r.le<std::uint8_t>() != 0;
    v.f04 = r.le<std::uint32_t>();
    for (auto& c : v.counters) c = r.le<std::uint32_t>();
    v.text = lstr(r);
    v.name = lstr(r);
    v.item = read_item(r);
    return v;
}

void write_re_item_row(ByteWriter& w, const ReItemRow& v) {
    w.le(v.re);
    w.le(v.item_id);
    w.le(static_cast<std::uint8_t>(v.placed ? 1 : 0));
    w.le(v.f04);
    for (auto c : v.counters) w.le(c);
    lstr(w, v.text);
    lstr(w, v.name);
    write_item(w, v.item);
}

std::optional<ReItemList> decode_re_item_list(const AppPacket& p) {
    std::uint16_t op = p.opcode;
    return decode_with(p, [op](ByteReader& r) {
        ReItemList v;
        v.re = r.le<std::uint32_t>();
        if (op == kOpReItemRowAdd) {
            v.rows.push_back(read_re_item_row(r));
            return v;
        }
        if (op == kOpReItemSummary) v.name = lstr(r);
        auto n = count(r, 49);
        for (std::uint32_t i = 0; i < n; ++i) v.rows.push_back(read_re_item_row(r));
        return v;
    });
}

AppPacket encode(std::uint16_t op, const ReItemList& v) {
    ByteWriter w;
    w.le(v.re);
    if (op == kOpReItemRowAdd) {
        if (!v.rows.empty()) write_re_item_row(w, v.rows.front());
        return AppPacket(op, w.take());
    }
    if (op == kOpReItemSummary) lstr(w, v.name.value_or(std::string()));
    w.le(static_cast<std::uint32_t>(v.rows.size()));
    for (const auto& row : v.rows) write_re_item_row(w, row);
    return AppPacket(op, w.take());
}

std::optional<ReRename> decode_re_rename(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        ReRename v;
        v.re = r.le<std::uint32_t>();
        v.name = r.empty() ? std::string() : r.cstring();
        return v;
    });
}

std::optional<ReItemRef> decode_re_item_removed(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        ReItemRef v;
        v.re = r.le<std::uint32_t>();
        v.item_id = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<ReItemRef> decode_re_placed_item_remove(const AppPacket& p) {
    if (p.data.size() < 0x28) return std::nullopt;
    return ReItemRef{get_u32(p.data, 0x20), get_u32(p.data, 0x24)};
}

std::optional<std::int32_t> decode_re_dword(const AppPacket& p) {
    if (p.opcode == kOpRePlotSearchOpen) {
        if (p.data.empty()) return std::nullopt;
        return static_cast<std::int32_t>(std::to_integer<std::uint8_t>(p.data[0]));
    }
    if (p.data.size() < 4) return std::nullopt;
    return static_cast<std::int32_t>(get_u32(p.data, 0));
}

std::optional<std::vector<std::uint32_t>> decode_re_id_list(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        std::vector<std::uint32_t> v;
        auto n = count(r, 4);
        for (std::uint32_t i = 0; i < n; ++i) v.push_back(r.le<std::uint32_t>());
        return v;
    });
}

AppPacket encode_re_id_list(std::uint16_t op, const std::vector<std::uint32_t>& ids) {
    ByteWriter w;
    write_lstr_list(w, ids);
    return AppPacket(op, w.take());
}

std::optional<ReListToggle> decode_re_list_toggle(const AppPacket& p) {
    if (p.data.size() < 0x69) return std::nullopt;
    return ReListToggle{get_u32(p.data, 0), p.data[0x68] != std::byte{0}};
}

std::optional<ReSearchReply> decode_re_search_reply(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        ReSearchReply v;
        r.le<std::uint32_t>();
        auto owner_type = static_cast<ReType>(r.le<std::uint8_t>());
        if (owner_type == ReType::Plot || owner_type == ReType::GuildPlot) {
            // 0x5b0f80's form B.
            v.kind = ReSearchReply::Record;
            r.le<std::uint32_t>();
            auto auto_open = r.le<std::uint16_t>();
            v.record.id = r.le<std::uint32_t>();
            v.record.parent = r.le<std::uint32_t>();
            v.record.definition = r.le<std::uint32_t>();
            v.record.guild_id = r.le<std::int32_t>();
            v.record.f28 = r.le<std::uint32_t>();
            lstr(r);
            lstr(r);
            v.record.address = lstr(r);
            v.record.owner = lstr(r);
            for (int i = 0; i < 4; ++i) r.le<std::uint32_t>();
            auto flag = r.le<std::uint8_t>();
            v.auto_open = auto_open != 0 && flag != 0;
            return v;
        }
        v.target = r.le<std::int32_t>();
        if (v.target == 4 || v.target == 5) {
            // CRealEstatePlotSearchWnd_ReadSearchResults @0x74c4c0.
            v.kind = ReSearchReply::PlotList;
            auto n = r.le<std::uint16_t>();
            v.title = lstr(r);
            for (std::uint32_t i = 0; i < n; ++i) {
                RePlotRow row;
                row.icon = r.le<std::uint32_t>();
                row.type = static_cast<ReType>(r.le<std::uint32_t>());
                row.re = r.le<std::uint32_t>();
                row.owner = lstr(r);
                row.address = lstr(r);
                if (row.type == ReType::Plot || row.type == ReType::GuildPlot) v.plots.push_back(std::move(row));
            }
            return v;
        }
        if (v.target == 3 || v.target < 0) return v; // dropped (a negative one after its u16)
        // CRealEstateNeighborhoodWnd_ReadNeighborhoodList @0x74ab50.
        auto n = r.le<std::uint16_t>();
        if (v.target < 2) {
            v.kind = ReSearchReply::Neighborhoods;
            v.show = r.le<std::uint8_t>() != 0;
            if (n != 0) v.header_id = r.le<std::uint32_t>();
            for (std::uint32_t i = 0; i < n; ++i) {
                ReNeighborhoodRow row;
                row.id = r.le<std::uint32_t>();
                row.name = lstr(r);
                row.guild_id = r.le<std::int32_t>();
                row.player_plots = r.le<std::uint32_t>();
                row.player_used = r.le<std::uint32_t>();
                row.guild_plots = r.le<std::uint32_t>();
                row.guild_used = r.le<std::uint32_t>();
                v.neighborhoods.push_back(std::move(row));
            }
        } else if (v.target == 2) {
            v.kind = ReSearchReply::Members;
            for (std::uint32_t i = 0; i < n; ++i) v.players.push_back(lstr(r));
            auto m = r.le<std::uint16_t>();
            for (std::uint32_t i = 0; i < m; ++i) v.guilds.push_back(r.le<std::int32_t>());
        }
        return v;
    });
}

AppPacket encode(const ReSearchReply& v) {
    ByteWriter w;
    w.le<std::uint32_t>(0);
    if (v.kind == ReSearchReply::Record) {
        w.le(static_cast<std::uint8_t>(ReType::Plot));
        w.le<std::uint32_t>(0);
        w.le<std::uint16_t>(v.auto_open ? 1 : 0);
        w.le(v.record.id);
        w.le(v.record.parent);
        w.le(v.record.definition);
        w.le(v.record.guild_id);
        w.le(v.record.f28);
        lstr(w, "");
        lstr(w, "");
        lstr(w, v.record.address);
        lstr(w, v.record.owner);
        for (int i = 0; i < 4; ++i) w.le<std::uint32_t>(0);
        w.le<std::uint8_t>(v.auto_open ? 1 : 0);
        return AppPacket(kOpReSearch, w.take());
    }
    w.le<std::uint8_t>(0);
    w.le(v.target);
    if (v.kind == ReSearchReply::PlotList) {
        w.le(static_cast<std::uint16_t>(v.plots.size()));
        lstr(w, v.title);
        for (const auto& row : v.plots) {
            w.le(row.icon);
            w.le(static_cast<std::uint32_t>(row.type));
            w.le(row.re);
            lstr(w, row.owner);
            lstr(w, row.address);
        }
    } else if (v.kind == ReSearchReply::Neighborhoods) {
        w.le(static_cast<std::uint16_t>(v.neighborhoods.size()));
        w.le<std::uint8_t>(v.show ? 1 : 0);
        if (!v.neighborhoods.empty()) w.le(v.header_id);
        for (const auto& row : v.neighborhoods) {
            w.le(row.id);
            lstr(w, row.name);
            w.le(row.guild_id);
            w.le(row.player_plots);
            w.le(row.player_used);
            w.le(row.guild_plots);
            w.le(row.guild_used);
        }
    } else if (v.kind == ReSearchReply::Members) {
        w.le(static_cast<std::uint16_t>(v.players.size()));
        for (const auto& s : v.players) lstr(w, s);
        w.le(static_cast<std::uint16_t>(v.guilds.size()));
        for (auto g : v.guilds) w.le(g);
    }
    return AppPacket(kOpReSearch, w.take());
}

std::optional<ReEscrow> decode_re_escrow(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        ReEscrow v;
        v.action = r.le<std::uint32_t>();
        v.re = r.le<std::uint32_t>();
        v.f8 = r.le<std::uint32_t>();
        v.address = capped(r, 64);
        v.owner = capped(r, 32);
        v.b = r.le<std::uint8_t>();
        v.c = r.le<std::uint32_t>();
        auto n = count(r, 16);
        for (std::uint32_t i = 0; i < n; ++i) {
            ReEscrow::Entry e;
            e.currency = r.le<std::uint32_t>();
            e.coin_type = r.le<std::uint32_t>();
            e.amount = r.le<std::uint64_t>();
            v.entries.push_back(e);
        }
        return v;
    });
}

AppPacket encode(const ReEscrow& v) {
    ByteWriter w;
    w.le(v.action);
    w.le(v.re);
    w.le(v.f8);
    write_cstring(w, std::string_view(v.address).substr(0, 63));
    write_cstring(w, std::string_view(v.owner).substr(0, 31));
    w.le(v.b);
    w.le(v.c);
    w.le(static_cast<std::uint32_t>(v.entries.size()));
    for (const auto& e : v.entries) {
        w.le(e.currency);
        w.le(e.coin_type);
        w.le(e.amount);
    }
    return AppPacket(kOpReEscrow, w.take());
}

std::optional<ReTransaction> decode_re_transaction(const AppPacket& p) {
    if (p.data.size() < kReTransactionSize) return std::nullopt;
    std::span<const std::byte> b(p.data);
    ReTransaction v;
    v.re = get_u32(b, 0);
    v.a = get_u32(b, 4);
    v.b = get_u32(b, 8);
    v.text0c = fixed_text(b, 0x0c, 64);
    v.text4c = fixed_text(b, 0x4c, 32);
    v.text6c = fixed_text(b, 0x6c, 64);
    v.ac = get_u32(b, 0xac);
    v.b0 = get_u32(b, 0xb0);
    v.b4 = get_u32(b, 0xb4);
    v.b8 = std::to_integer<std::uint8_t>(b[0xb8]);
    v.b9 = std::to_integer<std::uint8_t>(b[0xb9]);
    v.ba = std::to_integer<std::uint8_t>(b[0xba]);
    return v;
}

AppPacket encode(std::uint16_t op, const ReTransaction& v) {
    auto b = body(kReTransactionSize);
    put_u32(b, 0, v.re);
    put_u32(b, 4, v.a);
    put_u32(b, 8, v.b);
    put_text(b, 0x0c, 64, v.text0c);
    put_text(b, 0x4c, 32, v.text4c);
    put_text(b, 0x6c, 64, v.text6c);
    put_u32(b, 0xac, v.ac);
    put_u32(b, 0xb0, v.b0);
    put_u32(b, 0xb4, v.b4);
    b[0xb8] = static_cast<std::byte>(v.b8);
    b[0xb9] = static_cast<std::byte>(v.b9);
    b[0xba] = static_cast<std::byte>(v.ba);
    return AppPacket(op, std::move(b));
}

RePlacedItem read_re_placed_item(ByteReader& r) {
    RePlacedItem v;
    v.tag = capped(r, 64);
    v.npc_item = r.le<std::uint8_t>() != 0;
    v.fbc = r.le<std::uint32_t>();
    v.f0c = r.le<std::int32_t>();
    v.key = capped(r, 17);
    v.re = r.le<std::uint32_t>();
    v.item_id = r.le<std::uint32_t>();
    v.scale = r.f32();
    v.heading = r.f32();
    v.pitch = r.f32();
    v.roll = r.f32();
    v.server_pos.x = r.f32();
    v.server_pos.y = r.f32();
    v.server_pos.z = r.f32();
    v.no_collision = r.le<std::uint8_t>() != 0;
    v.wall_only = r.le<std::uint8_t>() != 0;
    v.no_free_mode = r.le<std::uint8_t>() != 0;
    v.wall_allowed = r.le<std::uint32_t>();
    for (auto& t : v.tail) t = r.le<std::uint32_t>();
    return v;
}

void write_re_placed_item(ByteWriter& w, const RePlacedItem& v) {
    write_cstring(w, std::string_view(v.tag).substr(0, 63));
    w.le(static_cast<std::uint8_t>(v.npc_item ? 1 : 0));
    w.le(v.fbc);
    w.le(v.f0c);
    write_cstring(w, std::string_view(v.key).substr(0, 16));
    w.le(v.re);
    w.le(v.item_id);
    w.f32(v.scale);
    w.f32(v.heading);
    w.f32(v.pitch);
    w.f32(v.roll);
    w.f32(v.server_pos.x);
    w.f32(v.server_pos.y);
    w.f32(v.server_pos.z);
    w.le(static_cast<std::uint8_t>(v.no_collision ? 1 : 0));
    w.le(static_cast<std::uint8_t>(v.wall_only ? 1 : 0));
    w.le(static_cast<std::uint8_t>(v.no_free_mode ? 1 : 0));
    w.le(v.wall_allowed);
    for (auto t : v.tail) w.le(t);
}

std::optional<RePlacedItem> decode_re_placed_item(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) { return read_re_placed_item(r); });
}

AppPacket encode(const RePlacedItem& v) {
    ByteWriter w;
    write_re_placed_item(w, v);
    return AppPacket(kOpRePlacedItem, w.take());
}

std::optional<std::vector<ReArea>> decode_re_areas(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        std::vector<ReArea> v;
        auto n = count(r, 88);
        for (std::uint32_t i = 0; i < n; ++i) {
            ReArea a;
            a.id = r.le<std::uint32_t>();
            for (auto& f : a.m) f = r.f32();
            for (auto& t : a.tail) t = r.le<std::int32_t>();
            v.push_back(a);
        }
        return v;
    });
}

AppPacket encode(const std::vector<ReArea>& v) {
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& a : v) {
        w.le(a.id);
        for (auto f : a.m) w.f32(f);
        for (auto t : a.tail) w.le(t);
    }
    return AppPacket(kOpReAreas, w.take());
}

std::optional<ReNpcItem> decode_re_npc_item(const AppPacket& p) {
    if (p.data.size() < 0x20) return std::nullopt;
    std::span<const std::byte> b(p.data);
    return ReNpcItem{get_u32(b, 0), fixed_text(b, 4, 0x14), get_u32(b, 0x18), get_u32(b, 0x1c)};
}

namespace {

// The fixed head of 0x21ec, 0x3424 and 0x2036: the key, the slot {-1, -1, 0xffff}, re, item.
void item_head(std::vector<std::byte>& b, std::string_view key, std::uint32_t re, std::uint32_t item) {
    put_text(b, 0, 18, key);
    put_u32(b, 0x14, 0xffffffffu);
    put_u32(b, 0x18, 0xffffffffu);
    put_u32(b, 0x1c, 0xffffu);
    put_u32(b, 0x20, re);
    put_u32(b, 0x24, item);
}

} // namespace

std::optional<ReMenu> decode_re_menu(const AppPacket& p) {
    if (p.data.size() < kReMenuSize) return std::nullopt;
    std::span<const std::byte> b(p.data);
    ReMenu v;
    v.key = fixed_text(b, 0, 18);
    v.re = get_u32(b, 0x20);
    v.item = get_u32(b, 0x24);
    v.name = fixed_text(b, 0x2c, 0x40);
    for (int i = 0; i < 40; ++i) {
        std::size_t at = 0x6c + 0x10 * static_cast<std::size_t>(i);
        ReMenu::Entry e;
        e.index = i;
        e.type = static_cast<std::int32_t>(get_u32(b, at));
        if (e.type == -1) continue;
        e.param = static_cast<std::int32_t>(get_u32(b, at + 4));
        e.text = get_u32(b, at + 8);
        e.sub = static_cast<std::int32_t>(get_u32(b, at + 12));
        v.entries.push_back(e);
    }
    v.restricted = b[0x2ec] != std::byte{0};
    v.x = static_cast<std::int32_t>(get_u32(b, 0x2f0));
    v.y = static_cast<std::int32_t>(get_u32(b, 0x2f4));
    v.f2f8 = get_u32(b, 0x2f8);
    v.f2fc = get_u32(b, 0x2fc);
    return v;
}

AppPacket encode(const ReMenu& v) {
    auto b = body(kReMenuSize);
    item_head(b, v.key, v.re, v.item);
    put_text(b, 0x2c, 0x40, v.name);
    std::fill(b.begin() + 0x6c, b.begin() + 0x6c + 0x10 * 40, std::byte{0xff});
    for (const auto& e : v.entries) {
        if (e.index < 0 || e.index >= 40) continue;
        std::size_t at = 0x6c + 0x10 * static_cast<std::size_t>(e.index);
        put_u32(b, at, static_cast<std::uint32_t>(e.type));
        put_u32(b, at + 4, static_cast<std::uint32_t>(e.param));
        put_u32(b, at + 8, e.text);
        put_u32(b, at + 12, static_cast<std::uint32_t>(e.sub));
    }
    b[0x2ec] = std::byte{static_cast<std::uint8_t>(v.restricted ? 1 : 0)};
    put_u32(b, 0x2f0, static_cast<std::uint32_t>(v.x));
    put_u32(b, 0x2f4, static_cast<std::uint32_t>(v.y));
    put_u32(b, 0x2f8, v.f2f8);
    put_u32(b, 0x2fc, v.f2fc);
    return AppPacket(kOpReContextMenu, std::move(b));
}

AppPacket encode_re_menu_request(std::string_view key, std::uint32_t re, std::uint32_t item, std::int32_t x,
                                 std::int32_t y, std::uint32_t f2f8) {
    ReMenu q;
    q.key = std::string(key.substr(0, 17));
    q.re = re;
    q.item = item;
    q.restricted = true;
    q.x = x;
    q.y = y;
    q.f2f8 = f2f8;
    return encode(q);
}

AppPacket encode_re_menu_request(std::uint32_t re, std::uint32_t item, std::int32_t x, std::int32_t y) {
    // EqItemGuid_Construct (sixteen '0', then a u16 0) and 0x542a90(guid, {-1, -1, 0xffff}, re, item, x, y, 0,
    // -1); the third index dword's upper half is the original's stack, 0 here.
    ReMenu q;
    q.key = "0000000000000000";
    q.re = re;
    q.item = item;
    q.x = x;
    q.y = y;
    q.f2f8 = 0xffffffffu;
    return encode(q);
}

AppPacket encode_re_item_op(std::uint16_t op, std::uint32_t re, std::uint32_t item, std::uint32_t param) {
    ByteWriter w;
    w.le(re);
    w.le(item);
    w.le(param);
    return AppPacket(op, w.take());
}

AppPacket encode_re_menu5(std::uint32_t re, std::uint32_t item) {
    auto b = body(0x94);
    put_u32(b, 0, re);
    put_u32(b, 4, item);
    return AppPacket(kOpReMenu5, std::move(b));
}

std::optional<Item> decode_re_inspect(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) { return read_item(r); });
}

AppPacket encode_re_search(std::uint32_t subtype, std::uint32_t id, std::string_view s0, std::string_view s1,
                           std::string_view s2, std::uint8_t flag) {
    auto b = body(0xe0);
    put_u32(b, 0, subtype);
    put_u32(b, 4, id);
    put_text(b, 0x0c, 80, s0);
    put_text(b, 0x5c, 64, s1);
    put_text(b, 0x9c, 64, s2);
    b[0xdc] = static_cast<std::byte>(flag);
    b[0xdd] = std::byte{1};
    return AppPacket(kOpReSearch, std::move(b));
}

AppPacket encode_re_travel(std::uint32_t id, std::uint32_t secondary) {
    ByteWriter w;
    w.le(id);
    w.le(secondary);
    w.le<std::uint32_t>(0);
    return AppPacket(kOpReTravel, w.take());
}

AppPacket encode_re_plot_select(std::uint32_t plot) {
    ByteWriter w;
    w.le(plot);
    return AppPacket(kOpRePlotSelect, w.take());
}

AppPacket encode_re_purchase(std::uint32_t re, std::uint32_t price_key, std::uint32_t upkeep_key) {
    ReTransaction t;
    t.re = re;
    t.ac = price_key;
    t.b0 = upkeep_key;
    return encode(kOpRePurchase, t);
}

AppPacket encode_re_sell(std::uint32_t re) {
    ReTransaction t;
    t.re = re;
    t.a = 0xffffffffu;
    t.b = 0xffffffffu;
    return encode(kOpReSell, t);
}

AppPacket encode_re_transfer(std::uint32_t re, std::string_view target) {
    ReTransaction t;
    t.re = re;
    t.text6c = std::string(target.substr(0, 63));
    return encode(kOpReTransfer, t);
}

AppPacket encode_re_opt_out(std::uint32_t re, bool opt_out) {
    ByteWriter w;
    w.le(re);
    w.le<std::uint32_t>(opt_out ? 1 : 0);
    return AppPacket(kOpReOptOut, w.take());
}

AppPacket encode_re_escrow(ReEscrowAction action, std::uint32_t re, std::vector<ReEscrow::Entry> entries) {
    ReEscrow v;
    v.action = static_cast<std::uint32_t>(action);
    v.re = re;
    v.entries = std::move(entries);
    return encode(v);
}

AppPacket encode_re_access_save(std::uint32_t re, const ReAccess& access) {
    ByteWriter w;
    w.le(re);
    write_access(w, access);
    return AppPacket(kOpReAccessSave, w.take());
}

AppPacket encode_re_to_storage(std::string_view key, std::uint32_t re, std::uint32_t item, std::uint32_t destination) {
    auto b = body(48);
    item_head(b, key, re, item);
    put_u32(b, 0x2c, destination);
    return AppPacket(kOpReToStorage, std::move(b));
}

AppPacket encode_re_to_inventory(std::string_view key, std::uint32_t re, std::uint32_t item) {
    auto b = body(44);
    item_head(b, key, re, item);
    return AppPacket(kOpReToInventory, std::move(b));
}


AppPacket encode_re_store_all() { return AppPacket(kOpReStoreAll); }

AppPacket encode_re_pack_crate(bool pack) {
    return AppPacket(kOpRePackCrate, {static_cast<std::byte>(pack ? 1 : 0)});
}

AppPacket encode_re_inspect(std::uint32_t re, std::uint32_t item) {
    ByteWriter w;
    w.le(re);
    w.le(item);
    return AppPacket(kOpReInspect, w.take());
}

namespace {

void put_index(std::vector<std::byte>& b, std::size_t at, const ReItemIndex& i) {
    put_u32(b, at, static_cast<std::uint32_t>(i.location));
    auto u16 = [&](std::size_t o, std::int16_t v) {
        auto u = static_cast<std::uint16_t>(v);
        b[at + o] = static_cast<std::byte>(u & 0xff);
        b[at + o + 1] = static_cast<std::byte>(u >> 8);
    };
    u16(4, i.slot);
    u16(6, i.sub);
    u16(8, i.aug);
}
ReItemIndex get_index(std::span<const std::byte> b, std::size_t at) {
    ByteReader r(b.subspan(at, 10));
    ReItemIndex i;
    i.location = r.le<std::int32_t>();
    i.slot = r.le<std::int16_t>();
    i.sub = r.le<std::int16_t>();
    i.aug = r.le<std::int16_t>();
    return i;
}
float get_f32(std::span<const std::byte> b, std::size_t at) {
    std::uint32_t u = get_u32(b, at);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

} // namespace

AppPacket encode(const RePlaceRequest& v) {
    auto b = body(60);
    put_text(b, 0, 17, v.key);
    put_index(b, 0x14, v.index);
    put_u32(b, 0x20, v.re);
    put_u32(b, 0x24, v.item);
    put_u32(b, 0x2c, 1);
    b[0x30] = std::byte{1};
    put_u32(b, 0x34, static_cast<std::uint32_t>(v.x));
    put_u32(b, 0x38, static_cast<std::uint32_t>(v.y));
    return AppPacket(kOpRePlace, std::move(b));
}

std::optional<RePlaceRequest> decode_re_place_request(const AppPacket& p) {
    if (p.data.size() < 60) return std::nullopt;
    std::span<const std::byte> b(p.data);
    RePlaceRequest v;
    v.key = fixed_text(b, 0, 0x11);
    v.index = get_index(b, 0x14);
    v.re = get_u32(b, 0x20);
    v.item = get_u32(b, 0x24);
    v.x = static_cast<std::int32_t>(get_u32(b, 0x34));
    v.y = static_cast<std::int32_t>(get_u32(b, 0x38));
    return v;
}

AppPacket encode_re_place_request(std::string_view key, std::uint16_t slot, std::int32_t x, std::int32_t y) {
    RePlaceRequest v;
    v.key = std::string(key.substr(0, 16));
    v.index = {0, static_cast<std::int16_t>(slot), -1, -1};
    v.x = x;
    v.y = y;
    return encode(v);
}

std::optional<RePlaceReply> decode_re_place_reply(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        RePlaceReply v;
        v.item = read_re_placed_item(r);
        v.index.location = r.le<std::uint8_t>();
        v.index.slot = r.le<std::int16_t>();
        v.index.sub = r.le<std::int16_t>();
        v.index.aug = r.le<std::int16_t>();
        v.b1 = r.le<std::uint8_t>();
        v.b2 = r.le<std::uint8_t>();
        v.b3 = r.le<std::uint8_t>();
        v.c1 = r.le<std::uint32_t>();
        v.c2 = r.le<std::uint32_t>();
        v.c3 = r.le<std::uint32_t>();
        auto n = count(r, 8);
        for (std::uint32_t i = 0; i <= n; ++i) {
            std::uint32_t key = r.le<std::uint32_t>();
            std::uint32_t code = r.le<std::uint32_t>();
            v.codes.emplace_back(key, code);
        }
        return v;
    });
}

AppPacket encode(const RePlaceReply& v) {
    ByteWriter w;
    write_re_placed_item(w, v.item);
    w.le(static_cast<std::uint8_t>(v.index.location));
    w.le(v.index.slot);
    w.le(v.index.sub);
    w.le(v.index.aug);
    w.le(v.b1);
    w.le(v.b2);
    w.le(v.b3);
    w.le(v.c1);
    w.le(v.c2);
    w.le(v.c3);
    std::size_t n = v.codes.empty() ? 0 : v.codes.size() - 1;
    w.le(static_cast<std::uint32_t>(n));
    for (std::size_t i = 0; i <= n; ++i) {
        auto kv = i < v.codes.size() ? v.codes[i] : std::pair<std::uint32_t, std::uint32_t>{0, 1};
        w.le(kv.first);
        w.le(kv.second);
    }
    return AppPacket(kOpRePlace, w.take());
}

AppPacket encode_re_place_confirm(const RePlacedItem& v, const ReItemIndex& index, const glm::vec3& box_a,
                                  const glm::vec3& box_b) {
    auto b = body(0x60);
    put_text(b, 0, 17, v.key);
    put_index(b, 0x14, index);
    put_u32(b, 0x20, v.re);
    put_u32(b, 0x24, v.item_id);
    const float t[13] = {v.heading,        v.pitch,          v.roll,    v.scale,  v.server_pos.x,
                         v.server_pos.y,   v.server_pos.z,   box_a.x,   box_a.y,  box_a.z,
                         box_b.x,          box_b.y,          box_b.z};
    for (std::size_t i = 0; i < 13; ++i) put_f32(b, 0x2c + 4 * i, t[i]);
    return AppPacket(kOpRePlaceConfirm, std::move(b));
}

std::optional<RePlaceConfirm> decode_re_place_confirm(const AppPacket& p) {
    if (p.data.size() < 0x60) return std::nullopt;
    std::span<const std::byte> b(p.data);
    RePlaceConfirm v;
    v.key = fixed_text(b, 0, 0x11);
    v.index = get_index(b, 0x14);
    v.re = get_u32(b, 0x20);
    v.item = get_u32(b, 0x24);
    v.heading = get_f32(b, 0x2c);
    v.pitch = get_f32(b, 0x30);
    v.roll = get_f32(b, 0x34);
    v.scale = get_f32(b, 0x38);
    v.pos = {get_f32(b, 0x3c), get_f32(b, 0x40), get_f32(b, 0x44)};
    v.box_a = {get_f32(b, 0x48), get_f32(b, 0x4c), get_f32(b, 0x50)};
    v.box_b = {get_f32(b, 0x54), get_f32(b, 0x58), get_f32(b, 0x5c)};
    return v;
}

} // namespace hailstone::net
