#include "net/messages/achievement.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hailstone::net {
namespace {
template <class T> T decimal(std::string_view value) {
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::out_of_range("invalid achievement link number");
    return result;
}
} // namespace

bool valid_achievement_player_name(std::string_view value) {
    if (value.empty() || value.size() > 63) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c >= 0x20 && c != 0x7f && c != '^' && c != '\'';
    });
}
const AchievementDefinition* AchievementCatalog::find(std::uint32_t id) const {
    const auto found = definition_indices.find(id);
    return found == definition_indices.end() ? nullptr : &definitions[found->second];
}
std::optional<AchievementLink> parse_achievement_link(std::string_view metadata, const AchievementCatalog& catalog) {
    if (metadata.empty() || metadata.size() > kMaxAchievementLinkBytes) return std::nullopt;
    try {
        std::size_t at = 0;
        const auto take = [&]() {
            const auto end = metadata.find('^', at);
            if (end == std::string_view::npos) throw std::out_of_range("truncated achievement link");
            const auto value = metadata.substr(at, end - at); at = end + 1; return value;
        };
        AchievementLink result;
        result.player_name = take();
        if (!valid_achievement_player_name(result.player_name))
            throw std::out_of_range("invalid achievement link player");
        result.achievement_id = decimal<std::uint32_t>(take());
        const auto* definition = catalog.find(result.achievement_id);
        if (!definition) throw std::out_of_range("unknown linked achievement");
        const auto raw_status = decimal<std::int16_t>(take());
        if (raw_status < 0 || raw_status > 3) throw std::out_of_range("invalid achievement status");
        result.state.status = static_cast<AchievementStatus>(raw_status);
        for (const auto type : {1u, 2u, 0u}) {
            const auto n = definition->components[type].size();
            result.state.satisfied[type].resize(n); result.state.counts[type].resize(n);
            for (std::size_t first = 0; first < n; first += 16) {
                const auto word = static_cast<std::uint16_t>(decimal<std::int16_t>(take()));
                for (std::size_t bit = 0; bit < 16 && first + bit < n; ++bit)
                    result.state.satisfied[type][first + bit] = static_cast<std::uint8_t>((word >> bit) & 1u);
            }
        }
        if (result.state.status == AchievementStatus::Completed)
            result.state.completed_at = decimal<std::uint32_t>(take());
        for (const auto type : {1u, 2u, 0u}) for (auto& value : result.state.counts[type])
            value = decimal<std::uint32_t>(take());
        if (at != metadata.size()) throw std::out_of_range("trailing achievement link values");
        result.metadata = metadata;
        return result;
    } catch (const std::out_of_range&) { return std::nullopt; }
}
std::optional<AchievementLink> make_achievement_link(std::string_view player_name,
    const AchievementDefinition& definition, const AchievementState& state) {
    if (!valid_achievement_player_name(player_name))
        return std::nullopt;
    const auto value = static_cast<std::int16_t>(state.status);
    if (value < 0 || value > 3 || (state.status == AchievementStatus::Completed && !state.completed_at)) return std::nullopt;
    std::string metadata(player_name); metadata.push_back('^');
    const auto append = [&](auto number) { metadata += std::to_string(number); metadata.push_back('^'); };
    append(definition.id); append(value);
    for (const auto type : {1u, 2u, 0u}) {
        const auto n = definition.components[type].size();
        if (n != state.satisfied[type].size() || n != state.counts[type].size()) return std::nullopt;
        for (std::size_t first = 0; first < n; first += 16) {
            std::uint16_t word = 0;
            for (std::size_t bit = 0; bit < 16 && first + bit < n; ++bit)
                if (state.satisfied[type][first + bit]) word = static_cast<std::uint16_t>(word | (1u << bit));
            // Explicit signed conversion avoids implementation-defined unsigned->signed narrowing.
            append(word < 0x8000 ? static_cast<std::int32_t>(word) : static_cast<std::int32_t>(word) - 65536);
        }
    }
    if (state.status == AchievementStatus::Completed) append(*state.completed_at);
    for (const auto type : {1u, 2u, 0u}) for (const auto count_value : state.counts[type]) {
        if (!count_value || *count_value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
        append(static_cast<std::uint32_t>(*count_value));
        if (metadata.size() > kMaxAchievementLinkBytes) return std::nullopt;
    }
    if (metadata.size() > kMaxAchievementLinkBytes) return std::nullopt;
    return AchievementLink{std::string(player_name), definition.id, state, std::move(metadata)};
}
} // namespace hailstone::net
