#pragma once
#include <cstdint>
#include <optional>
#include "net/messages/item.h"

namespace hailstone::net {
// Actions requested by gameplay code. The RoF2 codec chooses wire opcodes,
// slot representation, padding, item recursion and optional instance fields.
struct ItemVerificationIntent { ItemSlot slot; std::uint32_t target = 0; };
struct ItemPreviewIntent { std::uint32_t item_id = 0; std::optional<Item> instance; };
struct ItemConsumptionIntent { ItemSlot slot; bool automatic = false, drink = false, confirmation = false; };
struct LootSessionIntent { std::uint32_t corpse_id = 0; };
struct ItemActionIntent { MoveItem move; bool destroy = false; };
struct AugmentInformationIntent { std::uint32_t solvent_id = 0, window = 0; };
} // namespace hailstone::net
