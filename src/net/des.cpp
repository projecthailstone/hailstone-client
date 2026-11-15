#include "net/des.h"

#include <algorithm>

// Straightforward table-driven DES from the FIPS 46-3 tables. Bit numbers in
// the tables count from 1 = most significant bit, as in the standard.

namespace hailstone::net::des {

namespace {

constexpr std::uint8_t kIP[64] = {58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
                                  62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
                                  57, 49, 41, 33, 25, 17, 9,  1, 59, 51, 43, 35, 27, 19, 11, 3,
                                  61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7};

constexpr std::uint8_t kFP[64] = {40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
                                  38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
                                  36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
                                  34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9,  49, 17, 57, 25};

constexpr std::uint8_t kE[48] = {32, 1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,  8,  9,  10, 11,
                                 12, 13, 12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21,
                                 22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};

constexpr std::uint8_t kP[32] = {16, 7, 20, 21, 29, 12, 28, 17, 1,  15, 23, 26, 5,  18, 31, 10,
                                 2,  8, 24, 14, 32, 27, 3,  9,  19, 13, 30, 6,  22, 11, 4,  25};

constexpr std::uint8_t kPC1[56] = {57, 49, 41, 33, 25, 17, 9,  1,  58, 50, 42, 34, 26, 18,
                                   10, 2,  59, 51, 43, 35, 27, 19, 11, 3,  60, 52, 44, 36,
                                   63, 55, 47, 39, 31, 23, 15, 7,  62, 54, 46, 38, 30, 22,
                                   14, 6,  61, 53, 45, 37, 29, 21, 13, 5,  28, 20, 12, 4};

constexpr std::uint8_t kPC2[48] = {14, 17, 11, 24, 1,  5,  3,  28, 15, 6,  21, 10, 23, 19, 12, 4,
                                   26, 8,  16, 7,  27, 20, 13, 2,  41, 52, 31, 37, 47, 55, 30, 40,
                                   51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};

constexpr std::uint8_t kShifts[16] = {1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};

constexpr std::uint8_t kS[8][64] = {
    {14, 4,  13, 1, 2,  15, 11, 8,  3,  10, 6,  12, 5,  9,  0, 7,  0, 15, 7,  4,  14, 2,
     13, 1,  10, 6, 12, 11, 9,  5,  3,  8,  4,  1,  14, 8,  13, 6, 2,  11, 15, 12, 9,  7,
     3,  10, 5,  0, 15, 12, 8,  2,  4,  9,  1,  7,  5,  11, 3,  14, 10, 0, 6,  13},
    {15, 1,  8,  14, 6,  11, 3,  4,  9,  7, 2,  13, 12, 0, 5,  10, 3,  13, 4,  7,  15, 2,
     8,  14, 12, 0,  1,  10, 6,  9,  11, 5, 0,  14, 7,  11, 10, 4, 13, 1,  5,  8,  12, 6,
     9,  3,  2,  15, 13, 8,  10, 1,  3,  15, 4,  2,  11, 6,  7,  12, 0,  5,  14, 9},
    {10, 0,  9,  14, 6,  3,  15, 5,  1,  13, 12, 7,  11, 4,  2,  8,  13, 7,  0,  9,  3,  4,
     6,  10, 2,  8,  5,  14, 12, 11, 15, 1,  13, 6,  4,  9,  8,  15, 3,  0,  11, 1,  2,  12,
     5,  10, 14, 7,  1,  10, 13, 0,  6,  9,  8,  7,  4,  15, 14, 3,  11, 5,  2,  12},
    {7,  13, 14, 3,  0,  6,  9,  10, 1,  2, 8,  5,  11, 12, 4,  15, 13, 8,  11, 5,  6,  15,
     0,  3,  4,  7,  2,  12, 1,  10, 14, 9, 10, 6,  9,  0,  12, 11, 7,  13, 15, 1,  3,  14,
     5,  2,  8,  4,  3,  15, 0,  6,  10, 1, 13, 8,  9,  4,  5,  11, 12, 7,  2,  14},
    {2,  12, 4,  1,  7,  10, 11, 6,  8,  5,  3,  15, 13, 0, 14, 9,  14, 11, 2,  12, 4,  7,
     13, 1,  5,  0,  15, 10, 3,  9,  8,  6,  4,  2,  1,  11, 10, 13, 7, 8,  15, 9,  12, 5,
     6,  3,  0,  14, 11, 8,  12, 7,  1,  14, 2,  13, 6,  15, 0,  9,  10, 4, 5,  3},
    {12, 1,  10, 15, 9,  2,  6,  8,  0,  13, 3,  4,  14, 7,  5,  11, 10, 15, 4,  2,  7,  12,
     9,  5,  6,  1,  13, 14, 0,  11, 3,  8,  9,  14, 15, 5,  2,  8,  12, 3,  7,  0,  4,  10,
     1,  13, 11, 6,  4,  3,  2,  12, 9,  5,  15, 10, 11, 14, 1,  7,  6,  0,  8,  13},
    {4,  11, 2,  14, 15, 0,  8,  13, 3,  12, 9,  7,  5,  10, 6,  1,  13, 0,  11, 7,  4,  9,
     1,  10, 14, 3,  5,  12, 2,  15, 8,  6,  1,  4,  11, 13, 12, 3,  7,  14, 10, 15, 6,  8,
     0,  5,  9,  2,  6,  11, 13, 8,  1,  4,  10, 7,  9,  5,  0,  15, 14, 2,  3,  12},
    {13, 2,  8,  4,  6,  15, 11, 1,  10, 9,  3,  14, 5,  0,  12, 7,  1,  15, 13, 8,  10, 3,
     7,  4,  12, 5,  6,  11, 0,  14, 9,  2,  7,  11, 4,  1,  9,  12, 14, 2,  0,  6,  10, 13,
     15, 3,  5,  8,  2,  1,  14, 7,  4,  10, 8,  13, 15, 12, 9,  0,  3,  5,  6,  11}};

// Picks bits from `in` (in_bits wide) per table, building an out-of-n-bit value.
template <std::size_t N> std::uint64_t permute(std::uint64_t in, int in_bits, const std::uint8_t (&table)[N]) {
    std::uint64_t out = 0;
    for (std::size_t i = 0; i < N; ++i) out = (out << 1) | ((in >> (in_bits - table[i])) & 1);
    return out;
}

std::uint32_t feistel(std::uint32_t r, std::uint64_t subkey) {
    std::uint64_t x = permute(r, 32, kE) ^ subkey;
    std::uint32_t s = 0;
    for (int i = 0; i < 8; ++i) {
        auto six = static_cast<unsigned>((x >> (42 - 6 * i)) & 0x3f);
        unsigned row = ((six & 0x20) >> 4) | (six & 1);
        unsigned col = (six >> 1) & 0xf;
        s = (s << 4) | kS[i][row * 16 + col];
    }
    return static_cast<std::uint32_t>(permute(s, 32, kP));
}

std::uint64_t load_be64(const std::byte* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | std::to_integer<std::uint8_t>(p[i]);
    return v;
}

void store_be64(std::byte* p, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        p[i] = static_cast<std::byte>(v & 0xff);
        v >>= 8;
    }
}

} // namespace

Cipher::Cipher(std::uint64_t key) {
    std::uint64_t cd = permute(key, 64, kPC1);
    auto c = static_cast<std::uint32_t>(cd >> 28) & 0x0fffffff;
    auto d = static_cast<std::uint32_t>(cd) & 0x0fffffff;
    for (int i = 0; i < 16; ++i) {
        for (int s = 0; s < kShifts[i]; ++s) {
            c = ((c << 1) | (c >> 27)) & 0x0fffffff;
            d = ((d << 1) | (d >> 27)) & 0x0fffffff;
        }
        subkeys_[static_cast<std::size_t>(i)] = permute((static_cast<std::uint64_t>(c) << 28) | d, 56, kPC2);
    }
}

std::uint64_t Cipher::crypt(std::uint64_t block, bool decrypt) const {
    std::uint64_t x = permute(block, 64, kIP);
    auto l = static_cast<std::uint32_t>(x >> 32);
    auto r = static_cast<std::uint32_t>(x);
    for (std::size_t i = 0; i < 16; ++i) {
        std::uint32_t next = l ^ feistel(r, subkeys_[decrypt ? 15 - i : i]);
        l = r;
        r = next;
    }
    return permute((static_cast<std::uint64_t>(r) << 32) | l, 64, kFP);
}

std::uint64_t Cipher::encrypt(std::uint64_t block) const { return crypt(block, false); }
std::uint64_t Cipher::decrypt(std::uint64_t block) const { return crypt(block, true); }

std::vector<std::byte> zero_cbc_encrypt(std::span<const std::byte> plain) {
    // Allocate the padded size before copying secrets, so growing the vector cannot abandon plaintext storage.
    std::vector<std::byte> out((plain.size() + 7) / 8 * 8, std::byte{0});
    std::copy(plain.begin(), plain.end(), out.begin());
    Cipher cipher(0);
    std::uint64_t chain = 0;
    for (std::size_t i = 0; i < out.size(); i += 8) {
        chain = cipher.encrypt(load_be64(&out[i]) ^ chain);
        store_be64(&out[i], chain);
    }
    return out;
}

std::optional<std::vector<std::byte>> zero_cbc_decrypt(std::span<const std::byte> in) {
    if (in.size() % 8 != 0) return std::nullopt;
    std::vector<std::byte> out(in.size());
    Cipher cipher(0);
    std::uint64_t chain = 0;
    for (std::size_t i = 0; i < in.size(); i += 8) {
        std::uint64_t block = load_be64(&in[i]);
        store_be64(&out[i], cipher.decrypt(block) ^ chain);
        chain = block;
    }
    return out;
}

} // namespace hailstone::net::des
