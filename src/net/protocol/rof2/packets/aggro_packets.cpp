#include "net/protocol/rof2/packets/aggro_packets.h"

#include "core/byte_io.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;

std::optional<AggroTargetInfo> decode_aggro_target_info(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        AggroTargetInfo v;
        v.lock_id = r.le<std::uint32_t>();
        v.target_id = r.le<std::uint32_t>();
        return v;
    });
}

AppPacket encode(const AggroTargetInfo& v) {
    ByteWriter w;
    w.le(v.lock_id);
    w.le(v.target_id);
    return make_packet(rof2::Op::AggroMeterTargetInfo, std::move(w));
}

std::optional<AggroUpdate> decode_aggro_update(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        AggroUpdate v;
        if (r.le<std::uint8_t>()) v.secondary_id = r.le<std::uint32_t>();
        std::uint8_t n = r.le<std::uint8_t>();
        for (std::uint8_t i = 0; i < n; ++i) {
            std::uint8_t type = r.le<std::uint8_t>();
            std::uint16_t pct = r.le<std::uint16_t>();
            v.entries.emplace_back(type, pct);
        }
        return v;
    });
}

AppPacket encode(const AggroUpdate& v) {
    ByteWriter w;
    w.le<std::uint8_t>(v.secondary_id ? 1 : 0);
    if (v.secondary_id) w.le(*v.secondary_id);
    w.le(static_cast<std::uint8_t>(v.entries.size()));
    for (const auto& [type, pct] : v.entries) {
        w.le(type);
        w.le(pct);
    }
    return make_packet(rof2::Op::AggroMeterUpdate, std::move(w));
}

AppPacket encode_aggro_lock(std::uint32_t spawn_id) {
    ByteWriter w;
    w.le(spawn_id);
    return make_packet(rof2::Op::AggroMeterLockTarget, std::move(w));
}

} // namespace hailstone::net
