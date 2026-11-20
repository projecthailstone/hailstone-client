#include "game/kept_items.h"

#include <charconv>

#include "net/messages/item.h"

namespace hailstone::game {

bool KeptItems::holds_kept(const net::Item& item) const {
    if (kept(item.id)) return true;
    for (const auto& c : item.contents)
        if (holds_kept(c.item)) return true;
    return false;
}

bool KeptItems::set(std::uint32_t id, std::string_view name, bool on) {
    if (id == 0) return false;
    if (!on) return items_.erase(id) != 0;
    auto [it, added] = items_.try_emplace(id, name);
    if (!added && it->second == name) return false;
    it->second = std::string(name);
    return true;
}

void KeptItems::parse(std::string_view text) {
    items_.clear();
    while (!text.empty()) {
        std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line[first] == '#') continue;
        line.remove_prefix(first);
        std::uint32_t id = 0;
        auto [p, ec] = std::from_chars(line.data(), line.data() + line.size(), id);
        if (ec != std::errc() || id == 0) continue;
        std::string_view name(p, static_cast<std::size_t>(line.data() + line.size() - p));
        std::size_t n = name.find_first_not_of(" \t");
        name = n == std::string_view::npos ? std::string_view() : name.substr(n);
        items_[id] = std::string(name);
    }
}

std::string KeptItems::format() const {
    std::string out = "# Items marked Keep in the Advanced Inventory window: <item id> <name>\n";
    for (const auto& [id, name] : items_) {
        out += std::to_string(id);
        if (!name.empty()) {
            out += ' ';
            for (char c : name) out += (c == '\n' || c == '\r') ? ' ' : c;
        }
        out += '\n';
    }
    return out;
}

} // namespace hailstone::game
