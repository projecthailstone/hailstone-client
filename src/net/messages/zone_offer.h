#pragma once
// Server-proposed travel and resurrection offers. The response token is issued
// by the active codec; all opaque wire fields remain inside that codec.
#include <cstdint>
#include <string>
namespace hailstone::net {
enum class ZoneOfferKind : std::uint8_t { Translocate, Resurrection };
struct ZoneOffer {
    ZoneOfferKind kind = ZoneOfferKind::Translocate;
    std::uint64_t token = 0;
    std::uint32_t zone = 0;
    std::uint16_t instance = 0;
    std::string source;
};
struct ZoneOfferResponse { std::uint64_t token = 0; bool accepted = false; };
} // namespace hailstone::net
