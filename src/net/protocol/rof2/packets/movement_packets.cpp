#include "net/protocol/rof2/packets/movement_packets.h"

#include <bit>
#include <cmath>

#include "core/byte_io.h"
#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

// The record ApplyFallDamage builds on its stack (@0x45ae2f) and ApplyDamage
// sends: id (u16 spawn id, then zeroes), damage at 6, 1.0f at 10, twelve
// zero bytes, the type at 26, six bytes the original leaves unset (zero
// here), 0xffffffff at 33, 0xff at 37 and 0 at 38. EQEmu's
// EnvDamage2_Struct reads id, damage and dmgtype.
AppPacket encode(const EnvDamage& d) {
    ByteWriter w;
    w.le<std::uint32_t>(d.spawn_id & 0xffffu);
    w.le<std::uint16_t>(0);
    w.le<std::uint32_t>(d.damage);
    w.f32(1.0f);
    w.zeros(12);
    w.le<std::uint8_t>(d.type);
    w.zeros(6);
    w.le<std::uint32_t>(0xffffffffu);
    w.le<std::uint8_t>(0xff);
    w.le<std::uint8_t>(0);
    return make_packet(rof2::Op::EnvDamage, std::move(w));
}

std::optional<EnvDamage> decode_env_damage(const AppPacket& p) {
    if (p.data.size() != 39) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        EnvDamage d;
        d.spawn_id = r.le<std::uint32_t>();
        r.skip(2);
        d.damage = r.le<std::uint32_t>();
        r.skip(16);
        d.type = r.le<std::uint8_t>();
        return d;
    });
}

AppPacket encode_jump() { return make_empty(rof2::Op::Jump); }

AppPacket encode_safe_fall_success() { return make_empty(rof2::Op::SafeFallSuccess); }

AppPacket encode_set_run_mode(bool running) {
    ByteWriter w;
    w.le<std::uint32_t>(running ? 1u : 0u);
    return make_packet(rof2::Op::SetRunMode, std::move(w));
}

AppPacket encode(const FindPersonRequest& r) {
    ByteWriter w;
    w.le(r.kind);
    w.le<std::uint32_t>(r.id);
    w.le(static_cast<std::uint32_t>(r.item_a));
    w.le(static_cast<std::uint32_t>(r.item_b));
    for (float f : {r.at.x, r.at.y, r.at.z, r.point.x, r.point.y, r.point.z}) w.le(std::bit_cast<std::uint32_t>(f));
    return make_packet(rof2::Op::FindPersonRequest, std::move(w));
}

std::optional<std::vector<glm::vec3>> decode_find_person_reply(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        std::vector<glm::vec3> points;
        while (r.remaining() >= 12) {
            glm::vec3 v;
            v.x = std::bit_cast<float>(r.le<std::uint32_t>());
            v.y = std::bit_cast<float>(r.le<std::uint32_t>());
            v.z = std::bit_cast<float>(r.le<std::uint32_t>());
            // Not the original's: a point that is not a number (or infinite) is dropped, as the missile decoder drops
            // such packets, so the path, the navigator and the face never take it.
            if (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)) points.push_back(v);
        }
        return points;
    });
}

AppPacket encode_find_person_reply(const std::vector<glm::vec3>& path) {
    ByteWriter w;
    auto put = [&](const glm::vec3& v) {
        for (float f : {v.x, v.y, v.z}) w.le(std::bit_cast<std::uint32_t>(f));
    };
    if (!path.empty()) {
        put(path.back());
        for (const auto& v : path) put(v);
    }
    return make_packet(rof2::Op::FindPersonReply, std::move(w));
}

std::optional<FindableNpc> decode_findable_npc(const AppPacket& p) {
    return detail::decode_with(p, [](ByteReader& r) {
        FindableNpc n;
        n.remove = r.le<std::uint32_t>() != 0;
        n.spawn_id = r.le<std::uint32_t>();
        n.name = r.fixed_string(64);
        n.last_name = r.fixed_string(32);
        n.race = r.le<std::uint32_t>();
        n.class_id = r.le<std::uint8_t>();
        return n;
    });
}

AppPacket encode_findable_npc(const FindableNpc& n) {
    ByteWriter w;
    w.le<std::uint32_t>(n.remove ? 1 : 0);
    w.le<std::uint32_t>(n.spawn_id);
    w.fixed_string(std::string_view(n.name).substr(0, 63), 64);
    w.fixed_string(std::string_view(n.last_name).substr(0, 31), 32);
    w.le<std::uint32_t>(n.race);
    w.le<std::uint8_t>(n.class_id);
    for (std::uint8_t b : {std::uint8_t{0x16}, std::uint8_t{0x06}, std::uint8_t{0x24}}) w.le<std::uint8_t>(b);
    return make_packet(rof2::Op::SendFindableNPCs, std::move(w));
}

} // namespace hailstone::net
