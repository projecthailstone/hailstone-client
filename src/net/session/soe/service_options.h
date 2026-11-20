#pragma once

#include "net/messages/opcode.h"
#include "net/session/soe/options.h"

namespace hailstone::net::soe {

// Differences verified in the original HEAD between SOE session scopes.
// Login/UCS retain their existing service-specific override settings; this
// only supplies the stock baseline of a newly selected session.
Options defaults(messages::ServiceScope service);

// Reject incoherent SOE settings *before* creating network state. Other
// session flavors will validate their own settings independently.
bool valid(const Options& options);

} // namespace hailstone::net::soe
