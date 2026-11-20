/* SPDX-License-Identifier: MIT (sdk/LICENSE) */
#pragma once

// Endian-explicit readers and writers for game files and packets. All EQ
// data is little-endian except the SOE session header, which is big-endian.
// Values are assembled byte by byte, so this works on any host byte order
// and alignment.
//
// Part of the plugin SDK (C++20, header only): the client reads its packets with it (core/byte_io.h),
// and a plugin may read and write its own (OnPacket, send_packet) the same way.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace hailstone {

class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> data) : data_(data) {}
    ByteReader(const void* data, std::size_t size)
        : data_(static_cast<const std::byte*>(data), size) {}

    std::size_t position() const { return pos_; }
    std::size_t size() const { return data_.size(); }
    std::size_t remaining() const { return data_.size() - pos_; }
    bool empty() const { return remaining() == 0; }

    void seek(std::size_t pos) {
        if (pos > data_.size()) throw std::out_of_range("ByteReader::seek past end");
        pos_ = pos;
    }
    void skip(std::size_t n) { need(n); pos_ += n; }

    template <typename T> T le() { return read<T>(false); }
    template <typename T> T be() { return read<T>(true); }

    float f32() {
        std::uint32_t bits = le<std::uint32_t>();
        float f;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }
    double f64() {
        std::uint64_t bits = le<std::uint64_t>();
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    }

    std::span<const std::byte> bytes(std::size_t n) {
        need(n);
        auto s = data_.subspan(pos_, n);
        pos_ += n;
        return s;
    }

    // Fixed-size field holding a NUL-padded string.
    std::string fixed_string(std::size_t n) {
        auto s = bytes(n);
        const char* p = reinterpret_cast<const char*>(s.data());
        return std::string(p, strnlen_bounded(p, n));
    }

    // NUL-terminated string; consumes the terminator.
    std::string cstring() {
        const char* p = reinterpret_cast<const char*>(data_.data() + pos_);
        std::size_t len = strnlen_bounded(p, remaining());
        if (len == remaining()) throw std::out_of_range("ByteReader::cstring unterminated");
        pos_ += len + 1;
        return std::string(p, len);
    }

private:
    template <typename T> T read(bool big_endian) {
        static_assert(std::is_integral_v<T>);
        using U = std::make_unsigned_t<T>;
        need(sizeof(T));
        U v = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            std::size_t shift = big_endian ? (sizeof(T) - 1 - i) * 8 : i * 8;
            v = static_cast<U>(v | (static_cast<U>(std::to_integer<std::uint8_t>(data_[pos_ + i])) << shift));
        }
        pos_ += sizeof(T);
        return static_cast<T>(v);
    }

    void need(std::size_t n) const {
        if (n > remaining()) throw std::out_of_range("ByteReader: read past end");
    }

    static std::size_t strnlen_bounded(const char* p, std::size_t max) {
        if (max == 0) return 0; // p may be null for an empty buffer
        const void* nul = std::memchr(p, 0, max);
        return nul ? static_cast<std::size_t>(static_cast<const char*>(nul) - p) : max;
    }

    std::span<const std::byte> data_;
    std::size_t pos_ = 0;
};

class ByteWriter {
public:
    void reserve(std::size_t size) { buf_.reserve(size); }
    const std::vector<std::byte>& data() const { return buf_; }
    std::vector<std::byte> take() { return std::move(buf_); }
    std::size_t size() const { return buf_.size(); }

    template <typename T> void le(T v) { write(v, false); }
    template <typename T> void be(T v) { write(v, true); }

    void f32(float f) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, sizeof bits);
        le(bits);
    }
    void f64(double d) {
        std::uint64_t bits;
        std::memcpy(&bits, &d, sizeof bits);
        le(bits);
    }

    void bytes(std::span<const std::byte> s) {
        if (s.empty()) return;
        // Append through vector's single-element insertion path. GCC 16 can
        // misdiagnose the bulk resize/fill path for small inline buffers.
        // Reserve before writing so no partially extended result remains.
        buf_.reserve(buf_.size() + s.size());
        for (std::byte value : s) buf_.push_back(value);
    }
    void zeros(std::size_t n) {
        buf_.reserve(buf_.size() + n);
        for (std::size_t i = 0; i < n; ++i) buf_.push_back(std::byte{0});
    }

    // Writes s into a fixed-size NUL-padded field, truncating if needed.
    void fixed_string(std::string_view s, std::size_t n) {
        std::size_t len = s.size() < n ? s.size() : n;
        bytes({reinterpret_cast<const std::byte*>(s.data()), len});
        zeros(n - len);
    }

private:
    template <typename T> void write(T v, bool big_endian) {
        static_assert(std::is_integral_v<T>);
        using U = std::make_unsigned_t<T>;
        U u = static_cast<U>(v);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            std::size_t shift = big_endian ? (sizeof(T) - 1 - i) * 8 : i * 8;
            buf_.push_back(static_cast<std::byte>((u >> shift) & 0xff));
        }
    }

    std::vector<std::byte> buf_;
};

} // namespace hailstone
