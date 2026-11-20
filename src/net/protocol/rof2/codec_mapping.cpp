#include "net/protocol/rof2/codec_mapping.h"

namespace hailstone::net::rof2::detail {
namespace {
using messages::GameOpcode;
using messages::ServiceScope;
constexpr OpcodeMapping kMappings[] = {
#define HAILSTONE_ROF2_MAPPING(scope, semantic, wire) {ServiceScope::scope, GameOpcode::semantic, Op::wire},
#include "net/protocol/rof2/semantic_map.inc"
#undef HAILSTONE_ROF2_MAPPING
};
}

std::span<const OpcodeMapping> mappings() noexcept { return kMappings; }
} // namespace hailstone::net::rof2::detail
