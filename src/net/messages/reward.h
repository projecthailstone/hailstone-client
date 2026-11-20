#pragma once

// Protocol-independent gameplay values extracted from the RoF2 packet declarations.
// Application codecs translate these values to their own wire layouts.
// Serial lengths, packet opcodes and binary read/write remain in protocol/rof2/.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "net/messages/item.h"

namespace hailstone::net {

enum class RewardLane : std::uint8_t { Pending, Preview };

enum class RewardEntryType : std::uint32_t {
    Text = 0, Money = 1, Item = 2, Experience = 3, AAAbility = 4, AAPoints = 5, GenericPoints = 11
};

struct RewardItem {
    std::uint32_t id = 0, quantity = 0;
    bool lore = false;
    std::uint32_t lore_group = 0;
    std::string name;
};

struct RewardEntry {
    RewardEntryType type = RewardEntryType::Text;
    std::uint32_t entry_id = 0, string_id = 0;
    std::string description;
    // Money: PP/GP/SP/CP; AAAbility: rank id/cumulative rank; AAPoints: amount;
    // GenericPoints: amount at [2], currency id at [3]. Experience has no numeric suffix.
    std::array<std::uint32_t, 4> values{};
    std::uint8_t flag = 0;
    std::vector<RewardItem> items;
};

struct RewardOption {
    std::uint32_t wire_id = 0;
    bool common = false;
    std::uint8_t flags = 0;
    std::string label;
    std::vector<RewardEntry> entries;
};

struct RewardSet {
    std::uint32_t pending_id = 0, set_id = 0;
    std::string title;
    float multiplier = 1.0f;
    std::vector<RewardOption> options;
};

struct RewardClaimReply {
    std::uint32_t pending_id = 0, set_id = 0, wire_option_id = 0;
    bool success = false;
};

struct RewardInspection {
    std::uint32_t item_id = 0;
    Item item;
};

struct RewardPacket {
    // Pending/List upserts by the first wire ID (pending_id); Preview/List and Bulk replace.
    // A false Pending/List is a no-op. Claim and Inspection carry only their own field.
    enum class Kind { Collection, Claim, Inspection } kind = Kind::Collection;
    RewardLane lane = RewardLane::Pending;
    bool replace_collection = true;
    std::vector<RewardSet> sets;
    RewardClaimReply claim;
    std::optional<RewardInspection> inspection;
};

inline constexpr std::size_t kMaxRewardTabs = 256, kMaxRewardOptions = 4096;

inline constexpr std::size_t kMaxRewardEntries = 16384, kMaxRewardItems = 16384;


// The game chooses a reward operation and its identities; only the codec
// knows the RoF2 action number, field order or application opcode.
struct RewardIntent {
    enum class Kind : std::uint8_t { RequestPending, PreviewTask, Claim, Inspect } kind = Kind::RequestPending;
    RewardLane lane = RewardLane::Pending;
    std::uint32_t task = 0, pending = 0, set = 0, option = 0, entry = 0, item = 0;
};

} // namespace hailstone::net
