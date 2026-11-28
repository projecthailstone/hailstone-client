#pragma once

// MacroQuest's /filter name: chat lines hidden by their text (MQCommands.cpp Filter's "name" branch, MQ2Utilities.cpp
// AddFilter and WriteFilterNames, and MQ2ChatHook.cpp's dsp_chat detour, used as the specification). The list is
// shared by every character, as MacroQuest.ini's [Filter Names] is; hailstone.ini keeps it (chatfilter.*).

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/config.h"

namespace hailstone::mq {

class ChatTextFilters {
public:
    // chatfilter.enabled (MQ's [MacroQuest] FilterCustom, on by default) and chatfilter.name1.. (MQ's Filter1..).
    void load(Config& config);
    // Called after each change once the keys are written (the client saves hailstone.ini).
    void set_on_change(std::function<void()> fn) { on_change_ = std::move(fn); }

    // MQ's gFilterCustom: off, no line is hidden and the list stays.
    bool enabled() const { return enabled_; }
    void set_enabled(bool on);
    const std::vector<std::string>& names() const { return names_; }
    // False for an empty text, or one already listed (case ignored, MQ's _stricmp).
    bool add(std::string_view text);
    // False when no listed text matches (case ignored).
    bool remove(std::string_view text);
    void clear();
    // Changes with every change, for a window that shows the list.
    std::uint64_t revision() const { return revision_; }

    // MQ's match: a text starting with '*' hides a line that contains the rest (strstr, case kept); any other a line
    // that starts with it (_strnicmp, ASCII case ignored). Nothing while off.
    bool hides(std::string_view line) const;

private:
    void changed();
    Config* config_ = nullptr;
    std::function<void()> on_change_;
    bool enabled_ = true;
    std::vector<std::string> names_;
    std::size_t stored_ = 0; // chatfilter.name keys written, to erase the ones past the list
    std::uint64_t revision_ = 0;
};

// /filter name [on|off|add <text>|remove <text>|remove all]: MQ's replies, one chat line each.
std::vector<std::string> filter_name_command(ChatTextFilters& filters, std::string_view args);

} // namespace hailstone::mq
