#pragma once
#include <cstdint>
#include <optional>
#include "net/messages/item.h"

namespace hailstone::net {
enum class EvolvingAction : std::uint32_t { Update = 0, Open = 1, Details = 2, Transfer = 3, FinalResult = 4 };

// RoF2 puts an evolving item's persistent identifier in these two header words.
inline std::uint64_t evolving_item_id(const Item& item) {
    return item.is_evolving ? (static_cast<std::uint64_t>(item.parcel_item_id) << 32) | item.instance_id : 0;
}

struct EvolvingRequest {
    EvolvingAction action = EvolvingAction::Details;
    std::uint64_t source_id = 0, destination_id = 0;
};
// Action 1 (the server opens the transfer window) in action 0's 24-byte envelope (EQEmu's EvolveItemToggle).
struct EvolvingUpdate {
    EvolvingAction action = EvolvingAction::Open;
    std::uint64_t id = 0;
    std::uint32_t percentage = 0, activated = 0;
};
struct EvolvingTransferDetails {
    std::uint64_t source_id = 0, destination_id = 0;
    std::uint32_t compatibility = 0, max_transfer_level = 0;
    std::optional<Item> source, destination;
};

} // namespace hailstone::net
