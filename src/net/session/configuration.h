#pragma once

// Session settings are opaque to the game/application codec. The selected
// session factory validates the concrete settings type; generic profiles do
// not include SOE/Daybreak transport definitions.
#include <any>
#include <chrono>
#include <functional>
#include <string>

namespace hailstone::net::session {
struct Configuration {
    using Clock = std::chrono::steady_clock;
    std::string protocol = "soe";
    std::any parameters;
    std::function<Clock::time_point()> clock;

    Clock::time_point now() const { return clock ? clock() : Clock::now(); }
};
} // namespace hailstone::net::session
