#pragma once

// The SOE implementation, not a generic protocol profile, owns these options.
#include <utility>
#include "net/session/configuration.h"
#include "net/session/soe/options.h"

namespace hailstone::net::soe {
inline session::Configuration configuration(Options options = {}) {
    session::Configuration result;
    result.protocol = "soe";
    result.clock = options.clock;
    result.parameters = std::move(options);
    return result;
}
} // namespace hailstone::net::soe
