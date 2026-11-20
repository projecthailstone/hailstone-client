#pragma once
#include <cstdint>

namespace hailstone::net {
// Coin location, denomination and amount are inventory concepts, not bytes.
inline constexpr std::int32_t kCoinCursor = 0, kCoinInventory = 1, kCoinBank = 2, kCoinTrade = 3, kCoinSharedBank = 4;
inline constexpr std::int32_t kCoinCopper = 0, kCoinSilver = 1, kCoinGold = 2, kCoinPlatinum = 3;
struct MoveCoin {
    std::int32_t from = 0;
    std::int32_t to = 0;
    std::int32_t type_from = 0;
    std::int32_t type_to = 0;
    std::int32_t amount = 0;
};
// Copper-group split amount, ordered platinum, gold, silver, copper.
struct CoinSplit { std::int32_t platinum=0, gold=0, silver=0, copper=0; };
} // namespace hailstone::net
