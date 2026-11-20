#pragma once

// SHA-256 (FIPS 180-4): the server's client files are listed and checked by it (game/server_file_sync.h), and its own
// plugins and scripts known by it (plugin/policy.h).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hailstone {

using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256 {
public:
    Sha256();
    void update(std::span<const std::byte> data);
    void update(std::string_view data) { update(std::as_bytes(std::span(data.data(), data.size()))); }
    Sha256Digest finish();

private:
    void block(const std::uint8_t* p);

    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buf_{};
    std::size_t used_ = 0;
    std::uint64_t length_ = 0;
};

Sha256Digest sha256(std::span<const std::byte> data);
Sha256Digest sha256(std::string_view data);

// Lower-case hex, 64 characters; parse_sha256 takes either case.
std::string to_hex(const Sha256Digest& d);
std::optional<Sha256Digest> parse_sha256(std::string_view hex);

} // namespace hailstone
