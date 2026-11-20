#include "net/protocol/rof2/opcode_map.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace hailstone::net::rof2 {

namespace {

struct Entry {
    const char* name;
    std::uint16_t wire;
};

constexpr Entry kTable[] = {
#define HAILSTONE_OP(name, value) {#name, value},
#include "net/protocol/rof2/opcodes.inc"
#include "net/protocol/rof2/nonstandard_opcodes.inc"
#undef HAILSTONE_OP
};
static_assert(std::size(kTable) == static_cast<std::size_t>(Op::Count_));

// Wire value → first Op with that value, sorted by wire value for lookup.
struct ReverseMap {
    std::array<std::pair<std::uint16_t, Op>, std::size(kTable)> items{};
    std::size_t count = 0;

    ReverseMap() {
        for (std::size_t i = 0; i < std::size(kTable); ++i) {
            if (kTable[i].wire == 0) continue;
            items[count++] = {kTable[i].wire, static_cast<Op>(i)};
        }
        // Stable sort keeps file order among equal wire values.
        std::stable_sort(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(count),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
    }
};

const ReverseMap& reverse_map() {
    static const ReverseMap map;
    return map;
}

} // namespace

std::uint16_t to_wire(Op op) {
    auto i = static_cast<std::size_t>(op);
    return i < std::size(kTable) ? kTable[i].wire : 0;
}

Op from_wire(std::uint16_t wire) {
    if (wire == 0) return Op::Unknown;
    const auto& map = reverse_map();
    auto end = map.items.begin() + static_cast<std::ptrdiff_t>(map.count);
    auto it = std::lower_bound(map.items.begin(), end, wire,
                               [](const auto& item, std::uint16_t w) { return item.first < w; });
    return (it != end && it->first == wire) ? it->second : Op::Unknown;
}

const char* op_name(Op op) {
    auto i = static_cast<std::size_t>(op);
    return i < std::size(kTable) ? kTable[i].name : "Unknown";
}

} // namespace hailstone::net::rof2
