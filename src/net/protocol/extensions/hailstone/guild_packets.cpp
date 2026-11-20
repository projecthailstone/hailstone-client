#include "net/protocol/extensions/hailstone/guild_packets.h"
#include "core/byte_io.h"
#include "net/wire/byte_codec.h"
namespace hailstone::net {
using detail::decode_with;
static void name64(ByteWriter& w, const std::string& s) { w.fixed_string(s.substr(0, 63), 64); }
AppPacket encode_hailstone_guild(const HailstoneGuild& v, bool reply) {
    ByteWriter w;
    w.le(kHailstoneGuildMagic);
    w.le(kHailstoneGuildVersion);
    w.le(static_cast<std::uint16_t>(v.action));
    name64(w, v.name);
    w.le(v.guild_id);
    w.le(v.value);
    return AppPacket(reply ? kOpHailstoneGuildReply : kOpHailstoneGuild, w.take());
}

std::optional<HailstoneGuild> decode_hailstone_guild(const AppPacket& p, bool reply) {
    if (p.opcode != (reply ? kOpHailstoneGuildReply : kOpHailstoneGuild) || p.data.size() < kHailstoneGuildSize) return std::nullopt;
    auto got = decode_with(p, [](ByteReader& r) -> std::optional<HailstoneGuild> {
        if (r.le<std::uint32_t>() != kHailstoneGuildMagic || r.le<std::uint16_t>() != kHailstoneGuildVersion) return std::nullopt;
        HailstoneGuild v;
        v.action = static_cast<HailstoneGuildAction>(r.le<std::uint16_t>());
        v.name = r.fixed_string(64);
        v.guild_id = r.le<std::uint32_t>();
        v.value = r.le<std::uint32_t>();
        return v;
    });
    return got ? *got : std::nullopt;
}

}
