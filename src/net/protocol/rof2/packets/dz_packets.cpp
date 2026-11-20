#include "net/protocol/rof2/packets/dz_packets.h"

#include <bit>
#include <stdexcept>

#include "core/byte_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;

namespace {

constexpr std::size_t kDzName = 128, kName = 64;

ByteWriter header() {
    ByteWriter w;
    w.le<std::uint32_t>(0); // client id
    w.le<std::uint32_t>(0);
    return w;
}

// A fixed name field, NUL-terminated inside it.
void name_field(ByteWriter& w, std::string_view s, std::size_t n) { w.fixed_string(s.substr(0, n - 1), n); }

AppPacket command(rof2::Op op, std::string_view name) {
    ByteWriter w = header();
    name_field(w, name, kName);
    return make_packet(op, std::move(w));
}

} // namespace

std::optional<DzInfo> decode_dz_info(const AppPacket& p) {
    if (p.data.size() < 16 + kDzName + kName) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        r.skip(8);
        DzInfo v;
        v.assigned = r.le<std::uint32_t>() != 0;
        v.max_players = r.le<std::uint32_t>();
        v.name = r.fixed_string(kDzName);
        v.leader = r.fixed_string(kName);
        return v;
    });
}

AppPacket encode(const DzInfo& v) {
    ByteWriter w = header();
    w.le<std::uint32_t>(v.assigned ? 1 : 0);
    w.le(v.max_players);
    name_field(w, v.name, kDzName);
    name_field(w, v.leader, kName);
    return make_packet(rof2::Op::DzExpeditionInfo, std::move(w));
}

std::optional<DzInvite> decode_dz_invite(const AppPacket& p) {
    if (p.data.size() < 272) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        r.skip(8);
        DzInvite v;
        v.inviter = r.fixed_string(kName);
        v.expedition = r.fixed_string(kDzName);
        v.swapping = r.le<std::uint8_t>() != 0;
        v.swap_name = r.fixed_string(kName);
        r.skip(3);
        v.zone = r.le<std::uint16_t>();
        v.instance = r.le<std::uint16_t>();
        return v;
    });
}

AppPacket encode(const DzInvite& v) {
    ByteWriter w = header();
    name_field(w, v.inviter, kName);
    name_field(w, v.expedition, kDzName);
    w.le<std::uint8_t>(v.swapping ? 1 : 0);
    name_field(w, v.swap_name, kName);
    w.zeros(3);
    w.le(v.zone);
    w.le(v.instance);
    return make_packet(rof2::Op::DzExpeditionInvite, std::move(w));
}

AppPacket encode_dz_invite_response(const DzInvite& invite, bool accepted) {
    ByteWriter w = header();
    w.le(invite.zone);
    w.le(invite.instance);
    w.le<std::uint8_t>(accepted ? 1 : 0);
    w.le<std::uint8_t>(invite.swapping ? 1 : 0);
    name_field(w, invite.swap_name, kName);
    w.zeros(2);
    return make_packet(rof2::Op::DzExpeditionInviteResponse, std::move(w));
}

std::optional<std::vector<DzLockout>> decode_dz_lockouts(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        r.skip(4);
        std::uint32_t n = r.le<std::uint32_t>();
        std::vector<DzLockout> v;
        for (std::uint32_t i = 0; i < n; ++i) {
            DzLockout l;
            l.expedition = r.cstring();
            l.seconds = r.le<std::uint32_t>();
            l.event_type = r.le<std::int32_t>();
            l.event = r.cstring();
            v.push_back(std::move(l));
        }
        return v;
    });
}

AppPacket encode(const std::vector<DzLockout>& v) {
    ByteWriter w;
    w.le<std::uint32_t>(0);
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& l : v) {
        detail::write_cstring(w, l.expedition);
        w.le(l.seconds);
        w.le(l.event_type);
        detail::write_cstring(w, l.event);
    }
    return make_packet(rof2::Op::DzExpeditionLockoutTimers, std::move(w));
}

std::optional<std::vector<DzMember>> decode_dz_members(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        r.skip(4);
        std::uint32_t n = r.le<std::uint32_t>();
        if (n > kDzMembersMax) throw std::length_error("dz member count");
        std::vector<DzMember> v;
        for (std::uint32_t i = 0; i < n; ++i) {
            DzMember m;
            m.name = r.cstring();
            m.status = static_cast<DzMemberStatus>(r.le<std::uint8_t>());
            v.push_back(std::move(m));
        }
        return v;
    });
}

AppPacket encode(const std::vector<DzMember>& v, bool status_update) {
    ByteWriter w;
    w.le<std::uint32_t>(0);
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& m : v) {
        detail::write_cstring(w, m.name);
        w.le(static_cast<std::uint8_t>(m.status));
    }
    return make_packet(status_update ? rof2::Op::DzMemberListStatus : rof2::Op::DzMemberList, std::move(w));
}

std::optional<DzMemberName> decode_dz_member_name(const AppPacket& p) {
    if (p.data.size() < 12 + kName) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        r.skip(8);
        DzMemberName v;
        v.add = r.le<std::uint32_t>() != 0;
        v.name = r.fixed_string(kName);
        return v;
    });
}

AppPacket encode(const DzMemberName& v) {
    ByteWriter w = header();
    w.le<std::uint32_t>(v.add ? 1 : 0);
    name_field(w, v.name, kName);
    return make_packet(rof2::Op::DzMemberListName, std::move(w));
}

std::optional<std::string> decode_dz_leader(const AppPacket& p) {
    if (p.data.size() < 8 + kName) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        r.skip(8);
        return r.fixed_string(kName);
    });
}

AppPacket encode_dz_leader(std::string_view leader) { return command(rof2::Op::DzSetLeaderName, leader); }

std::optional<std::uint32_t> decode_dz_ends_warning(const AppPacket& p) {
    if (p.data.size() < 12) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        r.skip(8);
        return r.le<std::uint32_t>();
    });
}

AppPacket encode_dz_ends_warning(std::uint32_t minutes) {
    ByteWriter w = header();
    w.le(minutes);
    return make_packet(rof2::Op::DzExpeditionEndsWarning, std::move(w));
}

std::optional<std::vector<DzChoice>> decode_dz_choose_zone(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        r.skip(4);
        std::uint32_t n = r.le<std::uint32_t>();
        std::vector<DzChoice> v;
        for (std::uint32_t i = 0; i < n; ++i) {
            DzChoice c;
            c.zone = r.le<std::uint16_t>();
            c.instance = r.le<std::uint16_t>();
            c.id1 = r.le<std::uint32_t>();
            c.type = r.le<std::uint32_t>();
            c.id2 = r.le<std::uint32_t>();
            c.description = r.cstring();
            c.leader = r.cstring();
            v.push_back(std::move(c));
        }
        return v;
    });
}

AppPacket encode(const std::vector<DzChoice>& v) {
    ByteWriter w;
    w.le<std::uint32_t>(0);
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& c : v) {
        w.le(c.zone);
        w.le(c.instance);
        w.le(c.id1);
        w.le(c.type);
        w.le(c.id2);
        detail::write_cstring(w, c.description);
        detail::write_cstring(w, c.leader);
    }
    return make_packet(rof2::Op::DzChooseZone, std::move(w));
}

AppPacket encode_dz_choose_zone_reply(const DzChoice& c) {
    // EQEmu's DynamicZoneChooseZoneReply_Struct; it reads the zone and instance. The words it notes as seen (ff ff
    // ff ff, 0x69, ff ff ff ff) lead it.
    ByteWriter w;
    w.le<std::uint32_t>(0xffffffff);
    w.le<std::uint32_t>(0x69);
    w.le<std::uint32_t>(0xffffffff);
    w.le(c.id1);
    w.le(c.zone);
    w.le(c.instance);
    w.le(c.type);
    w.le(c.id2);
    w.zeros(24);
    return make_packet(rof2::Op::DzChooseZoneReply, std::move(w));
}

std::optional<std::vector<DzCompassPoint>> decode_dz_compass(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        r.skip(4);
        std::uint32_t n = r.le<std::uint32_t>();
        if (n > r.remaining() / 24) throw std::out_of_range("dz compass count");
        std::vector<DzCompassPoint> v;
        for (std::uint32_t i = 0; i < n; ++i) {
            DzCompassPoint c;
            r.skip(4);
            c.type = r.le<std::uint32_t>();
            r.skip(4);
            // y, x, z on the server: zone-file x, y, z (game/coords.h).
            c.at.x = std::bit_cast<float>(r.le<std::uint32_t>());
            c.at.y = std::bit_cast<float>(r.le<std::uint32_t>());
            c.at.z = std::bit_cast<float>(r.le<std::uint32_t>());
            v.push_back(c);
        }
        return v;
    });
}

AppPacket encode(const std::vector<DzCompassPoint>& v) {
    ByteWriter w;
    w.le<std::uint32_t>(0);
    w.le(static_cast<std::uint32_t>(v.size()));
    for (const auto& c : v) {
        w.le<std::uint32_t>(0);
        w.le(c.type);
        w.le<std::uint32_t>(0);
        w.le(std::bit_cast<std::uint32_t>(c.at.x));
        w.le(std::bit_cast<std::uint32_t>(c.at.y));
        w.le(std::bit_cast<std::uint32_t>(c.at.z));
    }
    return make_packet(rof2::Op::DzCompass, std::move(w));
}

AppPacket encode_dz_add_player(std::string_view name) { return command(rof2::Op::DzAddPlayer, name); }
AppPacket encode_dz_remove_player(std::string_view name) { return command(rof2::Op::DzRemovePlayer, name); }
AppPacket encode_dz_make_leader(std::string_view name) { return command(rof2::Op::DzMakeLeader, name); }

AppPacket encode_dz_swap_player(std::string_view add, std::string_view remove) {
    ByteWriter w = header();
    name_field(w, add, kName);
    name_field(w, remove, kName);
    return make_packet(rof2::Op::DzSwapPlayer, std::move(w));
}

AppPacket encode_dz_quit() { return make_packet(rof2::Op::DzQuit, header()); }
AppPacket encode_dz_list_timers() { return make_packet(rof2::Op::DzListTimers, header()); }
AppPacket encode_dz_player_list() { return make_packet(rof2::Op::DzPlayerList, header()); }

} // namespace hailstone::net
