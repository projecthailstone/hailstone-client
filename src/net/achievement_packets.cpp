#include "net/achievement_packets.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

#include <zlib.h>

#include "net/packet_io.h"

namespace hailstone::net {
namespace {
struct CatalogBudget { std::size_t components = 0, associations = 0, children = 0, text = 0; };
void count(std::size_t value, std::size_t maximum, std::size_t& used, const ByteReader& r, std::size_t minimum) {
    if (value > maximum - used || value > r.remaining() / minimum)
        throw std::out_of_range("achievement count exceeds its bounds");
    used += value;
}
std::string string(ByteReader& r, std::size_t maximum, std::size_t& used, std::size_t total = 32u << 20) {
    std::string value;
    while (true) {
        const auto c = r.le<std::uint8_t>();
        if (!c) return value;
        if (value.size() == maximum || used == total) throw std::out_of_range("achievement string exceeds its bounds");
        value.push_back(static_cast<char>(c)); ++used;
    }
}
bool boolean(ByteReader& r) {
    const auto value = r.le<std::uint8_t>();
    if (value > 1) throw std::out_of_range("invalid achievement boolean");
    return value != 0;
}
AchievementStatus status(std::int16_t value) {
    if (value < 0 || value > 3) throw std::out_of_range("invalid achievement status");
    return static_cast<AchievementStatus>(value);
}
bool valid_player_name(std::string_view value) {
    if (value.empty() || value.size() > 63) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c >= 0x20 && c != 0x7f && c != '^' && c != '\'';
    });
}
void validate_categories(const AchievementCatalog& catalog) {
    std::vector<std::uint8_t> color(catalog.categories.size());
    std::vector<std::size_t> path;
    for (std::size_t start = 0; start < catalog.categories.size(); ++start) {
        auto current = start;
        path.clear();
        while (color[current] == 0) {
            color[current] = 1; path.push_back(current);
            const auto parent = catalog.categories[current].parent_id;
            if (parent == kAchievementRootCategory) break;
            const auto found = catalog.category_indices.find(parent);
            if (found == catalog.category_indices.end()) throw std::out_of_range("missing achievement category parent");
            current = found->second;
            if (color[current] == 1) throw std::out_of_range("achievement category cycle");
        }
        for (const auto index : path) color[index] = 2;
    }
    std::vector<bool> listed(catalog.categories.size());
    for (const auto& category : catalog.categories) {
        std::set<std::uint32_t> associated;
        for (const auto& association : category.associations) {
            if (!catalog.definition_indices.contains(association.achievement_id) ||
                !associated.insert(association.achievement_id).second)
                throw std::out_of_range("invalid achievement association");
        }
        for (const auto id : category.child_ids) {
            const auto child = catalog.category_indices.find(id);
            if (child == catalog.category_indices.end() || listed[child->second] ||
                catalog.categories[child->second].parent_id != category.id)
                throw std::out_of_range("invalid achievement category child");
            listed[child->second] = true;
        }
    }
    for (std::size_t i = 0; i < catalog.categories.size(); ++i)
        if (catalog.categories[i].parent_id != kAchievementRootCategory && !listed[i])
            throw std::out_of_range("unlisted achievement category child");
}
AchievementCatalog catalog_body(std::span<const std::byte> bytes) {
    ByteReader r(bytes);
    AchievementCatalog result;
    CatalogBudget budget;
    const auto categories = r.le<std::uint32_t>();
    std::size_t used = 0; count(categories, kMaxAchievementCategories, used, r, 23);
    result.categories.reserve(categories);
    for (std::uint32_t i = 0; i < categories; ++i) {
        AchievementCategory category;
        category.id = r.le<std::uint32_t>(); category.parent_id = r.le<std::uint32_t>();
        if (!category.id || category.id == kAchievementRootCategory ||
            !result.category_indices.emplace(category.id, result.categories.size()).second)
            throw std::out_of_range("invalid achievement category identity");
        category.name = string(r, 4096, budget.text);
        category.description = string(r, 16384, budget.text);
        category.icon = string(r, 4096, budget.text);
        category.display_order = r.le<std::uint32_t>();
        const auto associations = r.le<std::uint32_t>();
        count(associations, kMaxAchievementAssociations, budget.associations, r, 9);
        category.associations.reserve(associations);
        for (std::uint32_t j = 0; j < associations; ++j) {
            AchievementAssociation association;
            association.achievement_id = r.le<std::uint32_t>();
            association.text = string(r, 16384, budget.text);
            association.display_order = r.le<std::uint32_t>();
            category.associations.push_back(std::move(association));
        }
        const auto children = r.le<std::uint32_t>();
        count(children, kMaxAchievementCategories, budget.children, r, 4);
        category.child_ids.reserve(children);
        for (std::uint32_t j = 0; j < children; ++j) category.child_ids.push_back(r.le<std::uint32_t>());
        result.categories.push_back(std::move(category));
    }
    const auto definitions = r.le<std::uint32_t>();
    used = 0; count(definitions, kMaxAchievementDefinitions, used, r, 39);
    result.definitions.reserve(definitions);
    for (std::uint32_t i = 0; i < definitions; ++i) {
        AchievementDefinition definition;
        definition.id = r.le<std::uint32_t>();
        if (!result.definition_indices.emplace(definition.id, result.definitions.size()).second)
            throw std::out_of_range("duplicate achievement identity");
        definition.name = string(r, 4096, budget.text);
        definition.description = string(r, 16384, budget.text);
        definition.icon_id = r.le<std::uint32_t>();
        definition.persistent = boolean(r); definition.version = r.le<std::uint32_t>();
        std::size_t local_components = 0;
        for (std::uint8_t type = 0; type < 4; ++type) {
            const auto components = r.le<std::uint32_t>();
            count(components, kMaxAchievementComponents, budget.components, r, 12);
            if (components > kMaxAchievementComponentsPerDefinition - local_components)
                throw std::out_of_range("too many components in one achievement");
            local_components += components;
            definition.components[type].reserve(components);
            for (std::uint32_t j = 0; j < components; ++j) {
                AchievementComponent component;
                component.id = r.le<std::uint32_t>(); component.type = r.le<std::uint8_t>();
                if (component.type != type || !definition.component_indices[type].emplace(component.id, j).second)
                    throw std::out_of_range("invalid achievement component identity");
                component.required_count = r.le<std::uint32_t>();
                component.name = string(r, 4096, budget.text);
                component.description = string(r, 16384, budget.text);
                component.display_order = r.le<std::uint8_t>();
                definition.components[type].push_back(std::move(component));
            }
        }
        definition.points = r.le<std::uint32_t>();
        const auto reward = r.le<std::uint32_t>();
        if (reward > 1) throw std::out_of_range("invalid achievement reward flag");
        definition.has_reward = reward != 0;
        result.definitions.push_back(std::move(definition));
    }
    if (!r.empty()) throw std::out_of_range("trailing achievement catalog bytes");
    validate_categories(result);
    return result;
}
AchievementState read_state(ByteReader& r, const AchievementDefinition& definition, bool counts) {
    AchievementState result;
    result.status = status(r.le<std::int16_t>());
    for (const auto type : {1u, 2u, 0u}) {
        const auto n = definition.components[type].size();
        result.satisfied[type].resize(n); result.counts[type].resize(n);
        for (std::size_t first = 0; first < n; first += 16) {
            const auto word = r.le<std::uint16_t>();
            for (std::size_t bit = 0; bit < 16 && first + bit < n; ++bit)
                result.satisfied[type][first + bit] = static_cast<std::uint8_t>((word >> bit) & 1u);
        }
    }
    if (result.status == AchievementStatus::Completed) result.completed_at = r.le<std::uint32_t>();
    if (counts) for (const auto type : {1u, 2u, 0u})
        for (auto& value : result.counts[type]) value = r.le<std::uint32_t>();
    return result;
}
template <class T> T decimal(std::string_view value) {
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::out_of_range("invalid achievement link number");
    return result;
}
bool packet(const AppPacket& p, std::uint16_t opcode) {
    return p.opcode == opcode && p.data.size() <= kMaxAchievementPacketBytes;
}
void exact(const ByteReader& r) {
    if (!r.empty()) throw std::out_of_range("trailing achievement bytes");
}
void write_state(ByteWriter& w, const AchievementState& state) {
    w.le(static_cast<std::int16_t>(state.status));
    std::size_t total = 0;
    for (const auto type : {1u, 2u, 0u}) {
        const auto n = state.satisfied[type].size();
        if (n > kMaxAchievementComponentsPerDefinition - total) throw std::out_of_range("achievement link too large");
        total += n;
        for (std::size_t first = 0; first < n; first += 16) {
            std::uint16_t word = 0;
            for (std::size_t bit = 0; bit < 16 && first + bit < n; ++bit)
                if (state.satisfied[type][first + bit]) word = static_cast<std::uint16_t>(word | (1u << bit));
            w.le(word);
        }
    }
    if (state.status == AchievementStatus::Completed) {
        if (!state.completed_at) throw std::out_of_range("missing achievement completion timestamp");
        w.le(*state.completed_at);
    }
    for (const auto type : {1u, 2u, 0u}) for (std::size_t i = 0; i < state.satisfied[type].size(); ++i) {
        const auto value = i < state.counts[type].size() ? state.counts[type][i].value_or(0) : 0;
        if (value > std::numeric_limits<std::uint32_t>::max()) throw std::out_of_range("achievement link count overflow");
        w.le(static_cast<std::uint32_t>(value));
    }
}
} // namespace

const AchievementDefinition* AchievementCatalog::find(std::uint32_t id) const {
    const auto found = definition_indices.find(id);
    return found == definition_indices.end() ? nullptr : &definitions[found->second];
}
std::optional<AchievementCatalog> decode_achievement_catalog(const AppPacket& p) {
    if (!packet(p, kOpAchievementDefinitions)) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        const auto size = r.le<std::uint32_t>();
        if (size < 8 || size > kMaxAchievementCatalogBytes || r.empty())
            throw std::out_of_range("invalid achievement inflate size");
        const auto compressed = r.bytes(r.remaining());
        // A forged size alone must not allocate tens of MiB. Grow only for bytes actually
        // produced by the bounded inflater, and require exact input and output consumption.
        std::vector<std::byte> inflated;
        inflated.reserve(std::min<std::size_t>(size, 32768));
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(compressed.data()));
        stream.avail_in = static_cast<uInt>(compressed.size());
        if (inflateInit(&stream) != Z_OK) throw std::out_of_range("cannot initialize achievement inflater");
        struct End { z_stream* value; ~End() { inflateEnd(value); } } end{&stream};
        std::array<std::byte, 32768> chunk{};
        while (true) {
            stream.next_out = reinterpret_cast<Bytef*>(chunk.data()); stream.avail_out = static_cast<uInt>(chunk.size());
            const auto result = inflate(&stream, Z_NO_FLUSH);
            const auto produced = chunk.size() - stream.avail_out;
            if (produced > size - inflated.size()) throw std::out_of_range("achievement inflate exceeds declared size");
            inflated.insert(inflated.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(produced));
            if (result == Z_STREAM_END) break;
            if (result != Z_OK || (produced == 0 && stream.avail_in == 0))
                throw std::out_of_range("invalid achievement compressed stream");
        }
        if (stream.total_out != size || stream.avail_in != 0)
            throw std::out_of_range("invalid achievement compressed stream");
        return catalog_body(inflated);
    });
}
std::optional<AchievementPrimary> decode_achievement_primary(const AppPacket& p, const AchievementCatalog* catalog) {
    if (!catalog || !packet(p, kOpAchievementPrimary)) return std::nullopt;
    return detail::decode_with(p, [&](ByteReader& r) {
        AchievementPrimary result;
        result.serial = r.le<std::uint32_t>(); result.dense = boolean(r);
        const auto n = r.le<std::uint32_t>();
        if (n > catalog->definitions.size() || n > r.remaining() / (result.dense ? 2 : 6))
            throw std::out_of_range("invalid achievement primary count");
        result.updates.reserve(n); std::set<std::uint32_t> indices;
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto index = result.dense ? i : r.le<std::uint32_t>();
            if (index >= catalog->definitions.size() || !indices.insert(index).second)
                throw std::out_of_range("invalid achievement state index");
            result.updates.push_back({index, read_state(r, catalog->definitions[index], false)});
        }
        exact(r); return result;
    });
}
std::optional<std::vector<AchievementProgress>> decode_achievement_progress(const AppPacket& p) {
    if (!packet(p, kOpAchievementProgress)) return std::nullopt;
    return detail::decode_with(p, [](ByteReader& r) {
        const auto n = r.le<std::uint32_t>();
        if (n > kMaxAchievementRequirements || n != r.remaining() / 20 || r.remaining() % 20)
            throw std::out_of_range("invalid achievement progress count");
        std::vector<AchievementProgress> result; result.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            AchievementProgress value;
            value.achievement_id = r.le<std::uint32_t>(); value.component_id = r.le<std::uint32_t>();
            value.requirement_id = r.le<std::uint32_t>(); value.requirement_type = r.le<std::uint32_t>();
            value.current_count = r.le<std::uint32_t>();
            result.push_back(value);
        }
        return result;
    });
}
std::optional<std::vector<AchievementState>> decode_achievement_snapshot(const AppPacket& p, const AchievementCatalog* catalog) {
    if (!catalog || !packet(p, kOpAchievementSnapshot)) return std::nullopt;
    return detail::decode_with(p, [&](ByteReader& r) {
        if (catalog->definitions.size() > r.remaining() / 2) throw std::out_of_range("truncated achievement snapshot");
        std::vector<AchievementState> result; result.reserve(catalog->definitions.size());
        for (const auto& definition : catalog->definitions) result.push_back(read_state(r, definition, false));
        exact(r); return result;
    });
}
std::optional<AchievementInspect> decode_achievement_inspect(const AppPacket& p, const AchievementCatalog* catalog) {
    if (!catalog || !packet(p, kOpAchievementInspect)) return std::nullopt;
    return detail::decode_with(p, [&](ByteReader& r) {
        AchievementInspect result; std::size_t used = 0;
        result.player_name = string(r, 63, used);
        if (result.player_name.empty()) throw std::out_of_range("empty achievement player name");
        result.achievement_id = r.le<std::uint32_t>();
        const auto* definition = catalog->find(result.achievement_id);
        if (!definition) throw std::out_of_range("unknown inspected achievement");
        result.state = read_state(r, *definition, true); result.trailing = r.le<std::uint8_t>();
        exact(r); return result;
    });
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
        if (!valid_player_name(result.player_name))
            throw std::out_of_range("invalid achievement link player");
        result.achievement_id = decimal<std::uint32_t>(take());
        const auto* definition = catalog.find(result.achievement_id);
        if (!definition) throw std::out_of_range("unknown linked achievement");
        result.state.status = status(decimal<std::int16_t>(take()));
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
std::optional<AchievementEarned> decode_achievement_earned(const AppPacket& p, const AchievementCatalog* catalog) {
    if (!catalog || !packet(p, kOpAchievementEarned)) return std::nullopt;
    return detail::decode_with(p, [&](ByteReader& r) {
        AchievementEarned result;
        result.spawn_id = r.le<std::uint32_t>(); result.achievement_id = r.le<std::uint32_t>();
        result.sound_id = r.le<std::uint32_t>(); std::size_t used = 0;
        const auto metadata = string(r, kMaxAchievementLinkBytes, used);
        const auto link = parse_achievement_link(metadata, *catalog);
        if (!link || link->achievement_id != result.achievement_id)
            throw std::out_of_range("invalid earned achievement link");
        result.link = *link; exact(r); return result;
    });
}
std::optional<AchievementWindow> decode_achievement_window(const AppPacket& p) {
    if (p.opcode != kOpAchievementWindow || !p.data.empty()) return std::nullopt;
    return AchievementWindow{};
}
std::optional<AchievementGuild> decode_achievement_guild(const AppPacket& p, const AchievementCatalog* catalog) {
    if (!catalog || !packet(p, to_wire(Op::GuildUpdate))) return std::nullopt;
    return detail::decode_with(p, [&](ByteReader& r) {
        if (r.le<std::uint32_t>() != 3) throw std::out_of_range("not a guild achievement update");
        r.skip(8); // verified player name starts at 0x0c, not 0x10
        const auto name = r.bytes(64);
        const auto end = std::find(name.begin(), name.end(), std::byte{0});
        if (end == name.end()) throw std::out_of_range("unterminated guild achievement name");
        AchievementGuild result;
        result.player_name.assign(reinterpret_cast<const char*>(name.data()), static_cast<std::size_t>(end - name.begin()));
        if (!valid_player_name(result.player_name)) throw std::out_of_range("invalid guild achievement name");
        r.skip(4); result.achievement_id = r.le<std::uint32_t>();
        std::size_t used = 0;
        const auto metadata = string(r, kMaxAchievementLinkBytes, used);
        const auto link = parse_achievement_link(metadata, *catalog);
        if (!link || link->achievement_id != result.achievement_id || link->player_name != result.player_name)
            throw std::out_of_range("guild achievement metadata mismatch");
        result.link = *link; exact(r); return result;
    });
}
std::optional<AchievementLink> make_achievement_link(std::string_view player_name,
    const AchievementDefinition& definition, const AchievementState& state) {
    if (!valid_player_name(player_name))
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
AppPacket encode_achievement_window_request() {
    ByteWriter w; w.le<std::uint8_t>(0x30); return {kOpAchievementWindowRequest, w.take()};
}
AppPacket encode_achievement_link_request(const AchievementLink& link) {
    ByteWriter w;
    if (!valid_player_name(link.player_name))
        throw std::out_of_range("invalid achievement link player");
    detail::write_cstring(w, link.player_name); w.le(link.achievement_id); write_state(w, link.state);
    w.le<std::uint8_t>(0); return {kOpAchievementLinkRequest, w.take()};
}

} // namespace hailstone::net
