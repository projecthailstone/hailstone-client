// Written from EQEmu rof2.cpp/rof2_structs.h (read as reference, no code copied)
// and the original client; OpenEQ crates/openeq-net/src/world.rs was studied to
// cross-check the login record and character-select tail (THIRD_PARTY.md).

#include "net/protocol/rof2/packets/world_packets.h"

#include <algorithm>
#include <charconv>

#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;
using detail::write_cstring;

namespace {

constexpr std::size_t kEnterWorldSize = 72;
constexpr std::size_t kChecksumSize = 8 + 256 * 4 * 2; // 2056
constexpr std::size_t kNameApprovalSize = 80;
constexpr std::size_t kCharCreateSize = 96;
constexpr std::size_t kRandomNameSize = 72;
constexpr std::size_t kLogServerSize = 560;
constexpr std::size_t kZoneServerInfoSize = 130;
constexpr std::size_t kAllocationSize = 4 + 7 * 4 + 7 * 4; // 60
constexpr std::size_t kComboSize = 6 * 4;                  // 24

bool exact(const AppPacket& p, std::size_t n) { return p.data.size() == n; }

} // namespace

std::uint32_t eq_crc32(std::span<const std::byte> data) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xedb88320u : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xffffffffu;
    for (std::byte b : data) crc = (crc >> 8) ^ table[(crc ^ std::to_integer<std::uint32_t>(b)) & 0xff];
    return crc;
}

// ---------------------------------------------------------------- LoginInfo

bool login_info_fits(const LoginInfo& v) { return v.account_id.size() + 1 + v.session_key.size() + 1 <= 64; }

AppPacket encode(const LoginInfo& v) {
    // login_info[64] holds "<account>\0<key>\0"; the original client builds
    // "login\0password\0\0" (WorldAuthenticate @00561e50) and sets +0xc0 = 0xcc.
    ByteWriter w;
    std::string login = v.account_id;
    login.push_back('\0');
    login += v.session_key;
    // The last byte stays a NUL, so world never reads the key on into the next field.
    w.fixed_string(std::string_view(login).substr(0, 63), 64);
    // unknown064[124], NUL-terminated (world reads it as a C string): at most 123 characters.
    w.fixed_string(std::string_view(v.custom_files_key).substr(0, 123), 124);
    w.le<std::uint8_t>(v.zoning ? 1 : 0);   // +188
    w.zeros(3);
    w.le<std::uint8_t>(0xcc); // +192, as the original client
    w.zeros(kLoginInfoSize - 193);
    return make_packet(rof2::Op::SendLoginInfo, std::move(w));
}

std::optional<LoginInfo> decode_login_info(const AppPacket& p) {
    if (!exact(p, kLoginInfoSize)) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        LoginInfo v;
        auto field = r.bytes(64);
        const char* s = reinterpret_cast<const char*>(field.data());
        std::size_t a = std::find(s, s + 64, '\0') - s;
        v.account_id.assign(s, a);
        if (a < 64) {
            std::size_t k = std::find(s + a + 1, s + 64, '\0') - (s + a + 1);
            v.session_key.assign(s + a + 1, k);
        }
        v.custom_files_key = r.fixed_string(124);
        v.zoning = r.le<std::uint8_t>() == 1;
        return v;
    });
}

// ---------------------------------------------------------------- EnterWorld

AppPacket encode(const EnterWorld& v) {
    ByteWriter w;
    w.fixed_string(std::string_view(v.name).substr(0, 63), 64);
    w.le(v.tutorial);
    w.le<std::uint32_t>(v.return_home ? 0x101u : 0u); // RoF2 client sets both low flag bytes
    return make_packet(rof2::Op::EnterWorld, std::move(w));
}

std::optional<EnterWorld> decode_enter_world(const AppPacket& p) {
    if (!exact(p, kEnterWorldSize)) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        EnterWorld v;
        v.name = r.fixed_string(64);
        v.tutorial = r.le<std::uint32_t>();
        v.return_home = (r.le<std::uint32_t>() & 0x101u) != 0;
        return v;
    });
}

std::optional<std::string> decode_enter_world_name(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) { return r.cstring(); });
}

// ---------------------------------------------------------------- file checksums

FileChecksum make_file_checksum(std::span<const std::byte> file, std::uint32_t seed) {
    FileChecksum v;
    v.checksum = eq_crc32(file);
    v.size = static_cast<std::uint32_t>(file.size());
    const std::uint32_t dwords = static_cast<std::uint32_t>(file.size() / 4);
    std::uint32_t x = seed ? seed : 0x9e3779b9u; // xorshift32
    for (std::size_t i = 0; i < 256; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        std::uint32_t index = dwords ? x % dwords : 0;
        v.index[i] = index;
        std::uint32_t value = 0;
        if (dwords) {
            ByteReader r(file.subspan(static_cast<std::size_t>(index) * 4, 4));
            value = r.le<std::uint32_t>();
        }
        v.value[i] = value;
    }
    return v;
}

AppPacket encode(ChecksumKind kind, const FileChecksum& v) {
    ByteWriter w;
    w.le(v.checksum);
    w.le(v.size);
    for (auto i : v.index) w.le(i);
    for (auto x : v.value) w.le(x);
    rof2::Op op = kind == ChecksumKind::Exe         ? rof2::Op::World_Client_CRC1
            : kind == ChecksumKind::SkillCaps ? rof2::Op::World_Client_CRC2
                                              : rof2::Op::World_Client_CRC3;
    return make_packet(op, std::move(w));
}

std::optional<FileChecksum> decode_file_checksum(const AppPacket& p) {
    if (!exact(p, kChecksumSize)) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        FileChecksum v;
        v.checksum = r.le<std::uint32_t>();
        v.size = r.le<std::uint32_t>();
        for (auto& i : v.index) i = r.le<std::uint32_t>();
        for (auto& x : v.value) x = r.le<std::uint32_t>();
        return v;
    });
}

// ---------------------------------------------------------------- character creation

AppPacket encode(const NameApproval& v) {
    ByteWriter w;
    // World's HandleNameApprovalPacket strlen's the name: keep its NUL inside the field.
    w.fixed_string(std::string_view(v.name).substr(0, 63), 64);
    w.le(v.race);
    w.le(v.class_);
    w.le(v.deity);
    w.le(v.flag);
    return make_packet(rof2::Op::ApproveName, std::move(w));
}

std::optional<NameApproval> decode_name_approval(const AppPacket& p) {
    if (!exact(p, kNameApprovalSize)) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        NameApproval v;
        v.name = r.fixed_string(64);
        v.race = r.le<std::uint32_t>();
        v.class_ = r.le<std::uint32_t>();
        v.deity = r.le<std::uint32_t>();
        v.flag = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<std::uint8_t> decode_approve_name_reply(const AppPacket& p) {
    if (p.data.empty()) return std::nullopt;
    return std::to_integer<std::uint8_t>(p.data[0]);
}

AppPacket encode(const CharCreate& v) {
    ByteWriter w;
    for (std::uint32_t x : {v.gender, v.race, v.class_, v.deity, v.start_zone, v.hair_color, v.beard, v.beard_color,
                            v.hair_style, v.face, v.eye_color1, v.eye_color2, v.drakkin_heritage, v.drakkin_tattoo,
                            v.drakkin_details, v.str, v.sta, v.agi, v.dex, v.wis, v.int_, v.cha, v.tutorial})
        w.le(x);
    w.le<std::uint32_t>(0); // unknown0092
    return make_packet(rof2::Op::CharacterCreate, std::move(w));
}

std::optional<CharCreate> decode_char_create(const AppPacket& p) {
    if (!exact(p, kCharCreateSize)) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        CharCreate v;
        for (std::uint32_t* x : {&v.gender, &v.race, &v.class_, &v.deity, &v.start_zone, &v.hair_color, &v.beard,
                                 &v.beard_color, &v.hair_style, &v.face, &v.eye_color1, &v.eye_color2,
                                 &v.drakkin_heritage, &v.drakkin_tattoo, &v.drakkin_details, &v.str, &v.sta, &v.agi,
                                 &v.dex, &v.wis, &v.int_, &v.cha, &v.tutorial})
            *x = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const CharCreateData& v) {
    ByteWriter w;
    w.le<std::uint8_t>(0);
    w.le(static_cast<std::uint32_t>(v.allocations.size()));
    for (const auto& a : v.allocations) {
        w.le(a.index);
        for (auto x : a.base) w.le(x);
        for (auto x : a.points) w.le(x);
    }
    w.le(static_cast<std::uint32_t>(v.combos.size()));
    for (const auto& c : v.combos) {
        for (auto x : {c.expansion_required, c.race, c.class_, c.deity, c.allocation_index, c.zone}) w.le(x);
    }
    return make_packet(rof2::Op::CharacterCreateRequest, std::move(w));
}

std::optional<CharCreateData> decode_char_create_data(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) -> CharCreateData {
        CharCreateData v;
        r.skip(1);
        std::uint32_t allocs = r.le<std::uint32_t>();
        if (allocs > r.remaining() / kAllocationSize) throw std::out_of_range("allocation count");
        v.allocations.resize(allocs);
        for (auto& a : v.allocations) {
            a.index = r.le<std::uint32_t>();
            for (auto& x : a.base) x = r.le<std::uint32_t>();
            for (auto& x : a.points) x = r.le<std::uint32_t>();
        }
        std::uint32_t combos = r.le<std::uint32_t>();
        if (combos > r.remaining() / kComboSize) throw std::out_of_range("combo count");
        v.combos.resize(combos);
        for (auto& c : v.combos) {
            for (std::uint32_t* x : {&c.expansion_required, &c.race, &c.class_, &c.deity, &c.allocation_index, &c.zone})
                *x = r.le<std::uint32_t>();
        }
        if (!r.empty()) throw std::out_of_range("trailing bytes");
        return v;
    });
}


AppPacket encode(const DeleteCharacter& v) {
    ByteWriter w;
    write_cstring(w, v.name);
    return make_packet(rof2::Op::DeleteCharacter, std::move(w));
}

AppPacket encode(const RandomName& v) {
    ByteWriter w;
    w.le(v.race);
    w.le(v.gender);
    w.fixed_string(std::string_view(v.name).substr(0, 63), 64);
    return make_packet(rof2::Op::RandomNameGenerator, std::move(w));
}

std::optional<RandomName> decode_random_name(const AppPacket& p) {
    if (!exact(p, kRandomNameSize)) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        RandomName v;
        v.race = r.le<std::uint32_t>();
        v.gender = r.le<std::uint32_t>();
        v.name = r.fixed_string(64);
        return v;
    });
}

// ---------------------------------------------------------------- world → client

std::optional<LogServer> decode_log_server(const AppPacket& p) {
    if (!exact(p, kLogServerSize)) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        LogServer v;
        r.seek(4);
        v.pvp = r.le<std::int32_t>();
        r.seek(8);
        v.enable_fv = r.le<std::uint8_t>();
        r.seek(0x18);
        v.spell_scribe = r.le<std::int32_t>();
        r.seek(36);
        v.world_short_name = r.fixed_string(32);
        return v;
    });
}

std::optional<std::uint32_t> decode_expansion_info(const AppPacket& p) {
    if (!exact(p, 68)) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        r.seek(64);
        return r.le<std::uint32_t>();
    });
}

std::optional<std::uint32_t> decode_max_characters(const AppPacket& p) {
    if (p.data.size() < 4) return std::nullopt;
    return decode_with(p, [](ByteReader& r) { return r.le<std::uint32_t>(); });
}

AppPacket encode(const Membership& v) {
    ByteWriter w;
    w.le(v.level);
    w.le(v.races);
    w.le(v.classes);
    w.le(static_cast<std::uint32_t>(v.entries.size()));
    for (auto e : v.entries) w.le(e);
    return make_packet(rof2::Op::SendMembership, std::move(w));
}

std::optional<Membership> decode_membership(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        Membership v;
        v.level = r.le<std::uint32_t>();
        v.races = r.le<std::uint32_t>();
        v.classes = r.le<std::uint32_t>();
        auto n = r.le<std::uint32_t>();
        for (std::uint32_t i = 0; i < n && i < kMembershipEntryCap && r.remaining() >= 4; ++i)
            v.entries.push_back(r.le<std::int32_t>());
        return v;
    });
}

AppPacket encode(const MembershipDetails& v) {
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(v.settings.size()));
    for (const auto& s : v.settings) {
        w.le(s.level);
        w.le(s.feature);
        w.le(s.value);
    }
    for (const auto* list : {&v.races, &v.classes}) {
        w.le(static_cast<std::uint32_t>(list->size()));
        for (const auto& [id, bits] : *list) {
            w.le(id);
            w.le(bits);
        }
    }
    for (const auto* url : {&v.url, &v.url2}) {
        w.le(static_cast<std::uint32_t>(url->size()));
        w.bytes({reinterpret_cast<const std::byte*>(url->data()), url->size()});
    }
    return make_packet(rof2::Op::SendMembershipDetails, std::move(w));
}

std::optional<MembershipDetails> decode_membership_details(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        MembershipDetails v;
        auto n = r.le<std::uint32_t>();
        if (n > r.remaining() / 12) throw std::out_of_range("membership settings");
        for (std::uint32_t i = 0; i < n; ++i) {
            MembershipSetting s;
            s.level = r.le<std::uint32_t>();
            s.feature = r.le<std::uint32_t>();
            s.value = r.le<std::int32_t>();
            v.settings.push_back(s);
        }
        for (auto* list : {&v.races, &v.classes}) {
            auto m = r.le<std::uint32_t>();
            if (m > r.remaining() / 8) throw std::out_of_range("membership entries");
            for (std::uint32_t i = 0; i < m; ++i) {
                auto id = r.le<std::uint32_t>();
                list->emplace_back(id, r.le<std::uint32_t>());
            }
        }
        // The URLs (SerializeBuffer_ReadCXStr); EQEmu sends two zero lengths.
        for (auto* url : {&v.url, &v.url2}) {
            if (r.remaining() < 4) break;
            auto len = r.le<std::uint32_t>();
            if (len > r.remaining()) throw std::out_of_range("membership url");
            for (std::uint32_t i = 0; i < len; ++i) url->push_back(static_cast<char>(r.le<std::uint8_t>()));
        }
        return v;
    });
}

std::optional<std::vector<GuildEntry>> decode_guilds_list(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        std::vector<GuildEntry> out;
        r.skip(64);
        std::uint32_t count = r.le<std::uint32_t>();
        // Ids >= MAX_GUILD_ID are skipped by the encoder without fixing the
        // count, so read entries until the payload ends.
        for (std::uint32_t i = 0; i < count && !r.empty(); ++i) {
            GuildEntry g;
            g.id = r.le<std::uint32_t>();
            g.name = r.cstring();
            out.push_back(std::move(g));
        }
        return out;
    });
}

AppPacket encode(const std::vector<CharacterPreview>& list) {
    ByteWriter w;
    w.le(static_cast<std::uint32_t>(list.size()));
    for (const auto& c : list) {
        write_cstring(w, c.name);
        w.le(c.class_);
        w.le(c.race);
        w.le(c.level);
        w.le(c.shroud_class);
        w.le(c.shroud_race);
        w.le(c.zone);
        w.le(c.instance);
        w.le(c.gender);
        w.le(c.face);
        for (const auto& e : c.equip) {
            for (auto x : {e.material, e.unknown1, e.elite_material, e.hero_forge_model, e.material2, e.color}) w.le(x);
        }
        w.le(c.texture);
        w.le(c.helm);
        w.le(c.drakkin_tattoo);
        w.le(c.drakkin_details);
        w.le(c.deity);
        w.le(c.primary_id_file);
        w.le(c.secondary_id_file);
        for (auto b : {c.hair_color, c.beard_color, c.eye_color1, c.eye_color2, c.hair_style, c.beard, c.go_home,
                       c.tutorial})
            w.le(b);
        w.le(c.drakkin_heritage);
        w.le(c.unknown1);
        w.le(c.enabled);
        w.le(c.last_login);
        w.le(c.unknown2);
    }
    return make_packet(rof2::Op::SendCharInfo, std::move(w));
}

// RoF2's per-character fixed tail, excluding the NUL-terminated name.
constexpr std::size_t kCharacterSelectTail = 274;

std::optional<std::vector<CharacterPreview>> decode_character_select(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        std::uint32_t count = r.le<std::uint32_t>();
        if (count > r.remaining() / (kCharacterSelectTail + 1)) throw std::out_of_range("character count");
        std::vector<CharacterPreview> list(count);
        for (auto& c : list) {
            c.name = r.cstring();
            c.class_ = r.le<std::uint8_t>();
            c.race = r.le<std::uint32_t>();
            c.level = r.le<std::uint8_t>();
            c.shroud_class = r.le<std::uint8_t>();
            c.shroud_race = r.le<std::uint32_t>();
            c.zone = r.le<std::uint16_t>();
            c.instance = r.le<std::uint16_t>();
            c.gender = r.le<std::uint8_t>();
            c.face = r.le<std::uint8_t>();
            for (auto& e : c.equip) {
                for (std::uint32_t* x :
                     {&e.material, &e.unknown1, &e.elite_material, &e.hero_forge_model, &e.material2, &e.color})
                    *x = r.le<std::uint32_t>();
            }
            c.texture = r.le<std::uint8_t>();
            c.helm = r.le<std::uint8_t>();
            c.drakkin_tattoo = r.le<std::uint32_t>();
            c.drakkin_details = r.le<std::uint32_t>();
            c.deity = r.le<std::uint32_t>();
            c.primary_id_file = r.le<std::uint32_t>();
            c.secondary_id_file = r.le<std::uint32_t>();
            for (std::uint8_t* b : {&c.hair_color, &c.beard_color, &c.eye_color1, &c.eye_color2, &c.hair_style,
                                    &c.beard, &c.go_home, &c.tutorial})
                *b = r.le<std::uint8_t>();
            c.drakkin_heritage = r.le<std::uint32_t>();
            c.unknown1 = r.le<std::uint8_t>();
            c.enabled = r.le<std::uint8_t>();
            c.last_login = r.le<std::uint32_t>();
            c.unknown2 = r.le<std::uint8_t>();
        }
        if (!r.empty()) throw std::out_of_range("trailing bytes");
        return list;
    });
}

std::optional<std::string> decode_motd(const AppPacket& p) {
    if (p.data.empty()) return std::string{};
    return decode_with(p, [](ByteReader& r) { return r.fixed_string(r.size()); });
}

std::optional<ChatServerInfo> decode_chat_server(const AppPacket& p) {
    auto text = decode_motd(p);
    if (!text) return std::nullopt;
    const std::string& s = *text;
    std::size_t c1 = s.find(',');
    std::size_t c2 = c1 == std::string::npos ? c1 : s.find(',', c1 + 1);
    std::size_t c3 = c2 == std::string::npos ? c2 : s.find(',', c2 + 1);
    if (c3 == std::string::npos) return std::nullopt;
    ChatServerInfo v;
    v.host = s.substr(0, c1);
    unsigned port = 0;
    auto [ptr, ec] = std::from_chars(s.data() + c1 + 1, s.data() + c2, port);
    if (ec != std::errc{} || ptr != s.data() + c2 || port > 0xffff) return std::nullopt;
    v.port = static_cast<std::uint16_t>(port);
    std::string who = s.substr(c2 + 1, c3 - c2 - 1);
    std::size_t dot = who.find('.');
    if (dot == std::string::npos) return std::nullopt;
    v.server_short_name = who.substr(0, dot);
    v.character = who.substr(dot + 1);
    std::string tail = s.substr(c3 + 1);
    if (tail.empty()) return std::nullopt;
    v.connection_type = tail[0];
    v.mail_key = tail.substr(1);
    return v;
}

AppPacket encode(const ZoneServerInfo& v) {
    ByteWriter w;
    w.fixed_string(v.host, 128);
    w.le(v.port);
    return make_packet(rof2::Op::ZoneServerInfo, std::move(w));
}

std::optional<ZoneServerInfo> decode_zone_server_info(const AppPacket& p) {
    if (p.data.size() < kZoneServerInfoSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        ZoneServerInfo v;
        v.host = r.fixed_string(128);
        v.port = r.le<std::uint16_t>();
        return v;
    });
}

std::optional<std::string> decode_zone_unavail(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) { return r.fixed_string(std::min<std::size_t>(16, r.size())); });
}


} // namespace hailstone::net
