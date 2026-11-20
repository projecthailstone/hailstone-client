#include "net/protocol/rof2/packets/pet_packets.h"

#include <stdexcept>

#include "core/byte_io.h"
#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {

using detail::decode_with;

AppPacket encode(const PetCommandPacket& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.command);
    w.le<std::uint32_t>(v.target);
    w.le<std::uint8_t>(v.flag);
    w.zeros(3);
    return make_packet(rof2::Op::PetCommands, std::move(w));
}

std::optional<PetCommandPacket> decode_pet_command(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        PetCommandPacket v;
        v.command = r.le<std::uint32_t>();
        v.target = r.le<std::uint32_t>();
        v.flag = r.le<std::uint8_t>();
        return v;
    });
}

AppPacket encode(const PetCommandState& v) {
    ByteWriter w;
    w.le<std::uint32_t>(v.button);
    w.le<std::uint32_t>(v.state);
    return make_packet(rof2::Op::PetCommandState, std::move(w));
}

std::optional<PetCommandState> decode_pet_command_state(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) {
        PetCommandState v;
        v.button = r.le<std::uint32_t>();
        v.state = r.le<std::uint32_t>();
        return v;
    });
}

std::optional<std::uint32_t> decode_pet_hott(const AppPacket& p) {
    return decode_with(p, [](ByteReader& r) { return r.le<std::uint32_t>(); });
}

} // namespace hailstone::net
