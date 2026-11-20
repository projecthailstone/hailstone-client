#include "net/protocol/rof2/packets/tracking_packets.h"
#include <algorithm>
#include <bit>
#include "net/protocol/rof2/packets/ability_packets.h"

namespace hailstone::net {
std::optional<TrackReply> decode_track_reply(const AppPacket& packet) {
    TrackReply result;
    if (auto header=decode_track_reply_header(packet); header && *header==game::kTrackReplyTimer) {
        result.timer=true;
        return result;
    }
    // As in the original handler, short records are zero-padded. A reply under two
    // bytes is an empty list; the two-byte count is bounded to the client capacity.
    const auto* data=reinterpret_cast<const std::uint8_t*>(packet.data.data());
    const auto length=packet.data.size();
    if (length < 2) return result;
    const auto byte=[&](std::size_t at)->std::uint8_t { return at<length ? data[at] : 0; };
    const auto u32=[&](std::size_t at) { return static_cast<std::uint32_t>(byte(at)) |
        (static_cast<std::uint32_t>(byte(at+1))<<8) | (static_cast<std::uint32_t>(byte(at+2))<<16) |
        (static_cast<std::uint32_t>(byte(at+3))<<24); };
    auto count=std::min<std::uint32_t>(static_cast<std::uint32_t>(byte(0)) | (static_cast<std::uint32_t>(byte(1))<<8),game::kTrackEntries);
    std::size_t at=2;
    std::uint8_t is_npc=1,is_pet=0,is_merc=0;
    result.entries.reserve(count);
    for (std::uint32_t n=0;n<count;++n) {
        game::TrackEntry entry;
        if (at+4<=length) { entry.spawn_id=static_cast<std::int32_t>(u32(at)); at+=4; }
        if (at+4<=length) { entry.distance=std::bit_cast<float>(u32(at)); at+=4; }
        if (at<length) entry.level=byte(at++);
        if (at<length) is_npc=byte(at++);
        std::size_t size=0;
        while (at+size<length && byte(at+size)!=0) ++size;
        const auto copy=std::min<std::size_t>(size+1,0x3f);
        if (at+copy<=length) {
            entry.name.assign(reinterpret_cast<const char*>(data+at),std::min(copy,size));
            at+=size+1;
        }
        if (at<length) is_pet=byte(at++);
        if (at<length) is_merc=byte(at++);
        entry.position=static_cast<std::int32_t>(n);
        entry.flags=(is_npc==0?game::kTrackPlayer:0) | (is_pet?game::kTrackPet:0) | (is_merc?game::kTrackMerc:0);
        result.entries.push_back(std::move(entry));
    }
    return result;
}
} // namespace hailstone::net
