#include "mq/chat_text_filter.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace hailstone::mq {

namespace {

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool iequal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

bool space(char c) { return c == ' ' || c == '\t'; }

std::string_view skip_space(std::string_view s) {
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    return s;
}

// MQ's GetArg(.., 1) and GetNextArg: the first word (a quoted one without its quotes), and the rest of the line after
// it and the blanks that follow.
std::string_view first_arg(std::string_view s, std::string_view& rest) {
    s = skip_space(s);
    std::size_t end = 0;
    std::string_view word;
    if (!s.empty() && s.front() == '"') {
        end = s.find('"', 1);
        word = s.substr(1, end == std::string_view::npos ? std::string_view::npos : end - 1);
        end = end == std::string_view::npos ? s.size() : end + 1;
    } else {
        while (end < s.size() && !space(s[end])) ++end;
        word = s.substr(0, end);
    }
    rest = skip_space(s.substr(end));
    return word;
}

std::string key(std::size_t n) { return std::format("chatfilter.name{}", n); }

} // namespace

void ChatTextFilters::load(Config& config) {
    config_ = &config;
    enabled_ = config.get_bool("chatfilter.enabled", true);
    names_.clear();
    for (std::size_t n = 1;; ++n) {
        auto v = config.get(key(n));
        if (!v) break;
        stored_ = n;
        // MacroQuest.ini's "NOBODY" is its example's placeholder (gbWriteAllConfig), never a filter.
        if (!v->empty() && *v != "NOBODY" &&
            std::none_of(names_.begin(), names_.end(), [&](const std::string& s) { return iequal(s, *v); }))
            names_.push_back(*v);
    }
    ++revision_;
}

void ChatTextFilters::changed() {
    ++revision_;
    if (config_) {
        config_->set_bool("chatfilter.enabled", enabled_);
        for (std::size_t i = 0; i < names_.size(); ++i) config_->set(key(i + 1), names_[i]);
        for (std::size_t n = names_.size() + 1; n <= stored_; ++n) config_->erase(key(n));
        stored_ = names_.size();
    }
    if (on_change_) on_change_();
}

void ChatTextFilters::set_enabled(bool on) {
    if (enabled_ == on) return;
    enabled_ = on;
    changed();
}

bool ChatTextFilters::add(std::string_view text) {
    if (text.empty()) return false;
    if (std::any_of(names_.begin(), names_.end(), [&](const std::string& s) { return iequal(s, text); })) return false;
    names_.emplace_back(text);
    changed();
    return true;
}

bool ChatTextFilters::remove(std::string_view text) {
    auto it = std::find_if(names_.begin(), names_.end(), [&](const std::string& s) { return iequal(s, text); });
    if (it == names_.end()) return false;
    names_.erase(it);
    changed();
    return true;
}

void ChatTextFilters::clear() {
    names_.clear();
    changed();
}

bool ChatTextFilters::hides(std::string_view line) const {
    if (!enabled_) return false;
    for (const std::string& f : names_) {
        if (f.front() == '*') {
            if (line.find(std::string_view(f).substr(1)) != std::string_view::npos) return true;
        } else if (line.size() >= f.size() && iequal(line.substr(0, f.size()), f)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> filter_name_command(ChatTextFilters& filters, std::string_view args) {
    std::vector<std::string> out;
    args = skip_space(args);
    if (args.empty()) {
        out.emplace_back("Names currently filtered:");
        out.emplace_back("---------------------------");
        for (const std::string& n : filters.names()) out.push_back("   " + n);
        return out;
    }
    std::string_view rest;
    const std::string_view word = first_arg(args, rest);
    if (iequal(word, "on") || iequal(word, "off")) {
        filters.set_enabled(iequal(word, "on"));
        out.push_back(std::format("Filtering of custom messages changed to: {}", filters.enabled() ? "on" : "off"));
    } else if (iequal(word, "remove")) {
        if (rest.empty()) {
            out.emplace_back("Remove what?");
        } else if (iequal(rest, "all")) {
            filters.clear();
            out.emplace_back("Cleared all name filters.");
        } else if (filters.remove(rest)) {
            out.push_back(std::format("Stopped filtering on: {}", rest));
        }
    } else if (iequal(word, "add")) {
        if (rest.empty()) out.emplace_back("Add what?");
        else if (!filters.add(rest)) out.push_back(std::format("Name '{}' is already being filtered.", rest));
        else out.push_back(std::format("Started filtering on: {}", rest));
    } else {
        out.emplace_back("Usage: /filter name on|off|add|remove <name>");
    }
    return out;
}

} // namespace hailstone::mq
