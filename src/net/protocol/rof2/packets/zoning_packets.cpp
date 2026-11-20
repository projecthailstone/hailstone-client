#include "net/protocol/rof2/packets/zoning_packets.h"
#include <algorithm>
#include "core/byte_io.h"
namespace hailstone::net::rof2 {
using hailstone::ByteReader;
std::optional<game::ZoneToBindPacket> decode_zone_to_bind(std::span<const std::uint8_t> d) {
    if (d.size() <= 20) return std::nullopt;
    const std::span<const std::byte> bytes = std::as_bytes(d);
    game::ZoneToBindPacket p;
    ByteReader r(bytes);
    p.zone = r.le<std::uint32_t>();
    p.x = r.f32();
    p.y = r.f32();
    p.z = r.f32();
    p.heading = r.f32();
    // The zone name: up to its NUL (or the end), the first 0x1ff characters kept.
    std::span<const std::byte> rest = bytes.subspan(20);
    std::size_t len = 0;
    while (len < rest.size() && rest[len] != std::byte{0}) ++len;
    p.zone_name.assign(reinterpret_cast<const char*>(rest.data()), std::min<std::size_t>(len, 0x1ff));
    std::size_t at = 20 + len + 1; // past the NUL
    // The fields after it are lenient: each one the packet does not hold reads 0.
    auto i32 = [&](std::size_t pos) -> std::int32_t {
        if (pos + 4 > bytes.size()) return 0;
        ByteReader f(bytes.subspan(pos, 4));
        return f.le<std::int32_t>();
    };
    p.keep_items = at < d.size() ? d[at] : 0;
    p.hp = i32(at + 1);
    p.mana = i32(at + 5);
    p.endurance = i32(at + 9);
    return p;
}

} // namespace hailstone::net::rof2
