#include "net/protocol/rof2/packets/missile_packets.h"
#include <cmath>
#include "core/byte_io.h"

namespace hailstone::net::rof2 {
using hailstone::ByteReader;
std::optional<game::MissilePacket> decode_missile_packet(std::span<const std::byte> data) {
    if (data.size() < 116) return std::nullopt;
    ByteReader r(data);
    game::MissilePacket p;
    auto vec = [&r] {
        glm::vec3 v;
        v.x = r.f32();
        v.y = r.f32();
        v.z = r.f32();
        return v;
    };
    p.position = vec();
    p.velocity = vec();
    p.speed = r.f32();
    p.heading = r.f32();
    p.tilt = r.f32();
    p.tilt_rate = r.f32();
    p.heading_rate = r.f32();
    p.pitch = r.f32();
    p.source_id = r.le<std::uint32_t>();
    p.target_id = r.le<std::uint32_t>();
    p.item_id = r.le<std::uint32_t>();
    p.item_extra = r.le<std::uint32_t>();
    p.spell = r.le<std::uint32_t>();
    p.light = r.le<std::uint8_t>();
    r.le<std::uint32_t>(); // +0x45 (EQEmu's 175 at +0x46: its "ranged, not a swing")
    p.range = r.le<std::uint8_t>();
    p.item_type = r.le<std::uint8_t>();
    p.skill = r.le<std::uint8_t>();
    p.to_location = r.le<std::uint8_t>() != 0;
    p.location = vec();
    p.model = r.fixed_string(27);
    // Not the original's check: a NaN or infinite position, velocity or angle would carry into the flight and the
    // renderer's matrices, so such a packet is refused, as spells.cpp and collision.cpp refuse non-finite input.
    auto finite = [](const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if (!finite(p.position) || !finite(p.velocity) || !finite(p.location) || !std::isfinite(p.speed) ||
        !std::isfinite(p.heading) || !std::isfinite(p.tilt) || !std::isfinite(p.tilt_rate) ||
        !std::isfinite(p.heading_rate) || !std::isfinite(p.pitch))
        return std::nullopt;
    return p;
}

} // namespace hailstone::net::rof2
