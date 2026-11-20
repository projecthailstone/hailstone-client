#pragma once
#include <cstdint>
namespace hailstone::net {
// The operands describe participants and decisions, not an EQ packet layout.
struct TradeInvitation { std::uint32_t target_id = 0, actor_id = 0; };
struct TradeReadiness { std::uint32_t partner_id = 0; bool ready = false; };
struct TradeCoinDelta { std::uint8_t denomination = 0; std::uint32_t amount = 0; };
struct TradeRefusal { std::uint32_t target_id = 0, actor_id = 0; std::uint8_t reason = 1; };
struct TradeCancellation { std::uint32_t partner_id = 0, actor_id = 0; };
} // namespace hailstone::net
