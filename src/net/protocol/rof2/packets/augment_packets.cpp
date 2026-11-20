// Written from the original client's augment sends and EQEmu rof2_structs.h (read as reference, no code
// copied).

#include "net/protocol/rof2/packets/augment_packets.h"

#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;

AppPacket encode(const AugmentItem& v) {
    ByteWriter w;
    w.le(v.serial);
    w.le(v.serial_high);
    write_slot(w, v.host);
    w.le(v.socket);
    write_slot(w, v.distiller);
    w.le(static_cast<std::uint32_t>(v.action));
    return make_packet(rof2::Op::AugmentItem, std::move(w));
}

std::optional<AugmentItem> decode_augment_item(const AppPacket& p) {
    if (p.data.size() != kAugmentItemSize) return std::nullopt; // DECODE(OP_AugmentItem): exact length
    return decode_with(p, [](ByteReader& r) {
        AugmentItem v;
        v.serial = r.le<std::uint32_t>();
        v.serial_high = r.le<std::uint32_t>();
        v.host = read_slot(r);
        v.socket = r.le<std::int32_t>();
        v.distiller = read_slot(r);
        v.action = static_cast<AugmentAction>(r.le<std::uint32_t>());
        return v;
    });
}

AppPacket encode_augment_info_request(std::uint32_t solvent_id, std::uint32_t window) {
    AugmentInfo v;
    v.item_id = solvent_id;
    v.window = window;
    return encode(v);
}

AppPacket encode(const AugmentInfo& v) {
    ByteWriter w;
    w.le(v.item_id);
    w.le(v.window);
    w.fixed_string(v.name.substr(0, 63), 64);
    w.le(v.unknown072);
    return make_packet(rof2::Op::AugmentInfo, std::move(w));
}

std::optional<AugmentInfo> decode_augment_info(const AppPacket& p) {
    if (p.data.size() < kAugmentInfoSize) return std::nullopt;
    return decode_with(p, [](ByteReader& r) {
        AugmentInfo v;
        v.item_id = r.le<std::uint32_t>();
        v.window = r.le<std::uint32_t>();
        v.name = r.fixed_string(64);
        v.unknown072 = r.le<std::uint32_t>();
        return v;
    });
}

} // namespace hailstone::net
