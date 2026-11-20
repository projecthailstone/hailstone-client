#include "net/protocol/extensions/hailstone/quest_npc_packets.h"

#include <algorithm>

#include "core/byte_io.h"
#include "net/wire/byte_codec.h"

namespace hailstone::net {

using detail::decode_with;

AppPacket encode_quest_npcs_request() {
    ByteWriter w;
    w.le(kHsQuestNpcsMagic);
    w.le(kHsQuestNpcsVersion);
    return AppPacket(kOpHsQuestNpcsRequest, w.take());
}

std::optional<std::vector<QuestNpc>> decode_quest_npcs(const AppPacket& p) {
    if (p.opcode != kOpHsQuestNpcs) return std::nullopt;
    auto got = decode_with(p, [](ByteReader& r) -> std::optional<std::vector<QuestNpc>> {
        if (r.le<std::uint32_t>() != kHsQuestNpcsMagic || r.le<std::uint16_t>() != kHsQuestNpcsVersion) return std::nullopt;
        const std::uint16_t count = r.le<std::uint16_t>();
        std::vector<QuestNpc> out;
        out.reserve(std::min<std::size_t>(count, r.remaining() / 2));
        for (std::uint16_t i = 0; i < count; ++i) {
            QuestNpc n;
            n.flags = r.le<std::uint8_t>();
            const std::uint8_t len = r.le<std::uint8_t>();
            n.name = r.fixed_string(len);
            out.push_back(std::move(n));
        }
        return out;
    });
    return got ? *got : std::nullopt;
}

AppPacket encode_quest_npcs(const std::vector<QuestNpc>& npcs) {
    ByteWriter w;
    w.le(kHsQuestNpcsMagic);
    w.le(kHsQuestNpcsVersion);
    w.le(static_cast<std::uint16_t>(npcs.size()));
    for (const auto& n : npcs) {
        const std::string name = n.name.substr(0, 255);
        w.le(n.flags);
        w.le(static_cast<std::uint8_t>(name.size()));
        w.fixed_string(name, name.size());
    }
    return AppPacket(kOpHsQuestNpcs, w.take());
}

} // namespace hailstone::net
