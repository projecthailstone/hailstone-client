#pragma once

// Common deterministic inputs for protocol-decoder and login-parser fuzz tests.
#include <cstddef>
#include <utility>
#include <vector>

#include "fuzz_support.h"

namespace hailstone::fuzz {

// Patterns the truncation sweep cuts at every length: all zeros, all 0xff, a counting pattern and random data with
// plenty of boundary values (so counts and string lengths come out large, negative or zero).
inline std::vector<Bytes> patterns() {
    std::vector<Bytes> out;
    // 24 KB: long enough for the largest packets (the player profile, inventories).
    constexpr std::size_t kLen = 24000;
    out.emplace_back(kLen, std::byte{0});
    out.emplace_back(kLen, std::byte{0xff});
    Bytes counting(kLen);
    for (std::size_t i = 0; i < counting.size(); ++i) counting[i] = static_cast<std::byte>(i);
    out.push_back(std::move(counting));
    Rng rng(0xd1ce);
    out.push_back(fuzz::random_bytes(rng, kLen));
    // Short fields of printable text, as in names and messages.
    Bytes text(kLen);
    for (std::size_t i = 0; i < text.size(); ++i) text[i] = static_cast<std::byte>(i % 17 == 16 ? 0 : 'a' + i % 23);
    out.push_back(std::move(text));
    return out;
}

// 0..plenty at every length, then sparser.
inline std::vector<std::size_t> truncation_lengths(std::size_t max) {
    std::vector<std::size_t> v;
    for (std::size_t n = 0; n <= max; n += (n < 160 ? 1 : n < 600 ? 5 : n < 2048 ? 53 : 997)) v.push_back(n);
    return v;
}

} // namespace hailstone::fuzz
