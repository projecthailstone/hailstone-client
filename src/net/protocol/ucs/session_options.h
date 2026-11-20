#pragma once
#include "net/session/configuration.h"
namespace hailstone::net {
// UCS uses its own one-byte application opcode session and keepalive profile.
session::Configuration ucs_session_configuration(bool short_keepalive = false);
} // namespace hailstone::net
