#include "net/protocol/rof2/packets/reward_packets.h"

#include <cmath>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <utility>

#include "net/protocol/rof2/wire_io.h"

namespace hailstone::net {
namespace {

struct Budget {
    std::size_t options = 0, entries = 0, items = 0, text = 0;
};
void bounded_count(std::size_t count, std::size_t limit, std::size_t& used,
                   const ByteReader& r, std::size_t minimum) {
    if (count > limit - used || count > r.remaining() / minimum)
        throw std::out_of_range("reward collection exceeds its bounds");
    used += count;
}
std::string text(ByteReader& r, Budget& budget, std::size_t maximum) {
    std::string value;
    while (true) {
        const auto c = r.le<std::uint8_t>();
        if (!c) return value;
        if (value.size() == maximum || budget.text == (2u << 20))
            throw std::out_of_range("reward text exceeds its bounds");
        value.push_back(static_cast<char>(c));
        ++budget.text;
    }
}
bool boolean(ByteReader& r) {
    const auto value = r.le<std::uint8_t>();
    if (value > 1) throw std::out_of_range("invalid reward boolean");
    return value != 0;
}
RewardEntry entry(ByteReader& r, Budget& budget) {
    RewardEntry result;
    const auto type = r.le<std::uint32_t>();
    if (type > 5 && type != 11) throw std::out_of_range("unknown reward entry type");
    result.type = static_cast<RewardEntryType>(type);
    result.entry_id = r.le<std::uint32_t>();
    result.string_id = r.le<std::uint32_t>();
    result.description = text(r, budget, 16384);
    switch (result.type) {
    case RewardEntryType::Money:
    case RewardEntryType::GenericPoints:
        for (auto& value : result.values) value = r.le<std::uint32_t>();
        break;
    case RewardEntryType::Item: {
        const auto count = r.le<std::uint32_t>();
        bounded_count(count, kMaxRewardItems, budget.items, r, 14);
        result.items.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            RewardItem item;
            item.id = r.le<std::uint32_t>();
            item.quantity = r.le<std::uint32_t>();
            item.lore = boolean(r);
            item.lore_group = r.le<std::uint32_t>();
            item.name = text(r, budget, 4096);
            result.items.push_back(std::move(item));
        }
        break;
    }
    case RewardEntryType::AAAbility:
        result.values[0] = r.le<std::uint32_t>();
        result.values[1] = r.le<std::uint32_t>();
        break;
    case RewardEntryType::AAPoints:
        result.values[0] = r.le<std::uint32_t>();
        result.flag = r.le<std::uint8_t>();
        break;
    case RewardEntryType::Text:
    case RewardEntryType::Experience: break;
    }
    return result;
}
RewardSet set(ByteReader& r, Budget& budget) {
    RewardSet result;
    result.pending_id = r.le<std::uint32_t>();
    result.set_id = r.le<std::uint32_t>();
    if (!result.pending_id || !result.set_id) throw std::out_of_range("invalid reward set identity");
    result.title = text(r, budget, 4096);
    result.multiplier = r.f32();
    if (!std::isfinite(result.multiplier) || result.multiplier < 0)
        throw std::out_of_range("invalid reward display multiplier");
    if (r.le<std::uint32_t>() != result.set_id) throw std::out_of_range("reward set identity mismatch");
    const auto count = r.le<std::int32_t>();
    if (count < 0) throw std::out_of_range("negative reward option count");
    bounded_count(static_cast<std::size_t>(count), kMaxRewardOptions, budget.options, r, 11);
    result.options.reserve(static_cast<std::size_t>(count));
    std::set<std::uint32_t> ids;
    for (std::int32_t i = 0; i < count; ++i) {
        RewardOption option;
        option.wire_id = r.le<std::uint32_t>();
        if (!option.wire_id || !ids.insert(option.wire_id).second)
            throw std::out_of_range("duplicate reward option identity");
        const auto entries = r.le<std::uint32_t>();
        option.common = boolean(r);
        option.flags = r.le<std::uint8_t>();
        option.label = text(r, budget, 4096);
        bounded_count(entries, kMaxRewardEntries, budget.entries, r, 13);
        option.entries.reserve(entries);
        for (std::uint32_t j = 0; j < entries; ++j) option.entries.push_back(entry(r, budget));
        result.options.push_back(std::move(option));
    }
    return result;
}
AppPacket request(RewardLane lane, std::initializer_list<std::uint32_t> values) {
    ByteWriter writer;
    for (const auto value : values) writer.le(value);
    return AppPacket(reward_opcode(lane), writer.take());
}

} // namespace

std::optional<RewardPacket> decode_reward_packet(const AppPacket& p) {
    if ((p.opcode != kOpPendingRewards && p.opcode != kOpRewardPreview) || p.data.size() > kMaxRewardPacketBytes)
        return std::nullopt;
    return detail::decode_with(p, [&](ByteReader& r) {
        RewardPacket result;
        result.lane = p.opcode == kOpPendingRewards ? RewardLane::Pending : RewardLane::Preview;
        const auto action = r.le<std::uint32_t>();
        Budget budget;
        switch (action) {
        case 0:
            result.replace_collection = result.lane == RewardLane::Preview;
            if (boolean(r)) result.sets.push_back(set(r, budget));
            break;
        case 7: {
            const auto count = r.le<std::int32_t>();
            if (count < 0 || static_cast<std::size_t>(count) > kMaxRewardTabs ||
                static_cast<std::size_t>(count) > r.remaining() / 21)
                throw std::out_of_range("invalid reward tab count");
            r.skip(1); // native bulk display flags; no server policy is inferred from these
            result.sets.reserve(static_cast<std::size_t>(count));
            for (std::int32_t i = 0; i < count; ++i) result.sets.push_back(set(r, budget));
            break;
        }
        case 1: {
            result.kind = RewardPacket::Kind::Inspection;
            RewardInspection inspected;
            inspected.item_id = r.le<std::uint32_t>();
            inspected.item = read_item(r);
            if (!inspected.item_id || inspected.item.id != inspected.item_id)
                throw std::out_of_range("reward inspection item mismatch");
            result.inspection = std::move(inspected);
            break;
        }
        case 3:
            if (result.lane != RewardLane::Pending) throw std::out_of_range("preview cannot claim a reward");
            result.kind = RewardPacket::Kind::Claim;
            result.claim.pending_id = r.le<std::uint32_t>();
            result.claim.set_id = r.le<std::uint32_t>();
            result.claim.wire_option_id = r.le<std::uint32_t>();
            result.claim.success = boolean(r);
            r.skip(3); // unspecified native trailer padding
            break;
        default: throw std::out_of_range("unknown reward response action");
        }
        if (!r.empty()) throw std::out_of_range("trailing reward bytes");
        return result;
    });
}

AppPacket encode_pending_rewards_request() { return request(RewardLane::Pending, {6}); }
AppPacket encode_task_reward_preview(std::uint32_t id) { return request(RewardLane::Preview, {4, id}); }
AppPacket encode_achievement_reward_view(std::uint32_t index) { return request(RewardLane::Pending, {5, index}); }
AppPacket encode_reward_inspect(RewardLane lane, std::uint32_t set_id, std::uint32_t option,
                               std::uint32_t entry_id, std::uint32_t item_id) {
    return request(lane, {1, set_id, option, entry_id, item_id});
}
AppPacket encode_reward_claim(std::uint32_t pending_id, std::uint32_t set_id, std::uint32_t option) {
    // The native sender clears only the low trailer byte; deterministic zero padding is sufficient.
    return request(RewardLane::Pending, {3, pending_id, set_id, option, 0});
}

} // namespace hailstone::net
