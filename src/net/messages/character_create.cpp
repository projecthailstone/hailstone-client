#include "net/messages/world.h"

#include <algorithm>

namespace hailstone::net {

std::optional<CharCreateAllocation> validate_char_create(const CharCreateData& data, const CharCreate& cc) {
    auto combo = std::find_if(data.combos.begin(), data.combos.end(), [&](const CharCreateCombo& c) {
        return c.class_ == cc.class_ && c.race == cc.race && c.deity == cc.deity && c.zone == cc.start_zone;
    });
    if (combo == data.combos.end()) return std::nullopt;
    auto alloc = std::find_if(data.allocations.begin(), data.allocations.end(),
                              [&](const CharCreateAllocation& a) { return a.index == combo->allocation_index; });
    if (alloc == data.allocations.end()) return std::nullopt;
    std::uint32_t budget = 0;
    for (auto x : alloc->points) budget += x;
    const std::array<std::uint32_t, 7> stats{cc.str, cc.dex, cc.agi, cc.sta, cc.int_, cc.wis, cc.cha};
    std::uint32_t spent = 0;
    for (std::size_t i = 0; i < 7; ++i) {
        if (stats[i] < alloc->base[i] || stats[i] > alloc->base[i] + budget) return std::nullopt;
        spent += stats[i] - alloc->base[i];
    }
    if (spent > budget) return std::nullopt;
    return *alloc;
}

std::optional<CharCreate> default_char_create(const CharCreateData& data, const CharCreateCombo& combo,
                                              std::uint32_t gender) {
    auto alloc = std::find_if(data.allocations.begin(), data.allocations.end(),
                              [&](const CharCreateAllocation& a) { return a.index == combo.allocation_index; });
    if (alloc == data.allocations.end()) return std::nullopt;
    CharCreate cc;
    cc.gender = gender;
    cc.race = combo.race;
    cc.class_ = combo.class_;
    cc.deity = combo.deity;
    cc.start_zone = combo.zone;
    auto stat = [&](std::size_t i) { return alloc->base[i] + alloc->points[i]; };
    cc.str = stat(0);
    cc.dex = stat(1);
    cc.agi = stat(2);
    cc.sta = stat(3);
    cc.int_ = stat(4);
    cc.wis = stat(5);
    cc.cha = stat(6);
    return cc;
}

} // namespace hailstone::net
