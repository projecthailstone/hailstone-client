#pragma once

// Generic, bounded wire field helpers. No application opcodes, EQ framing,
// protocol selection, or game-specific structures belong in this header.
#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "core/byte_io.h"
#include "net/wire/app_packet.h"

namespace hailstone::net {
namespace detail {

inline void write_cstring(ByteWriter& w, std::string_view s) {
    w.bytes({reinterpret_cast<const std::byte*>(s.data()), s.size()});
    w.le<std::uint8_t>(0);
}

// Runs a decoder body; a read past the end (or any range error) gives nullopt, and so does an allocation the
// packet's counts made too large: a decoder never throws into the handler loop.
template <typename F> auto decode_with(const AppPacket& p, F&& body) -> std::optional<decltype(body(std::declval<ByteReader&>()))> {
    try {
        ByteReader r(p.data);
        return body(r);
    } catch (const std::out_of_range&) {
        return std::nullopt;
    } catch (const std::length_error&) {
        return std::nullopt;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
}

// Unsigned field of `bits` bits starting at bit `shift` of a little-endian word.
constexpr std::uint32_t bits_u(std::uint32_t word, unsigned shift, unsigned bits) {
    return (word >> shift) & ((bits == 32) ? 0xffffffffu : ((1u << bits) - 1u));
}

// Signed (two's complement) field.
constexpr std::int32_t bits_s(std::uint32_t word, unsigned shift, unsigned bits) {
    std::uint32_t v = bits_u(word, shift, bits);
    std::uint32_t sign = 1u << (bits - 1);
    return static_cast<std::int32_t>(v ^ sign) - static_cast<std::int32_t>(sign);
}

// Places the low `bits` bits of `value` at `shift`.
constexpr std::uint32_t put_bits(std::int64_t value, unsigned shift, unsigned bits) {
    std::uint32_t mask = (bits == 32) ? 0xffffffffu : ((1u << bits) - 1u);
    return (static_cast<std::uint32_t>(value) & mask) << shift;
}

} // namespace detail

} // namespace hailstone::net
