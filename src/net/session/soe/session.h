#pragma once

#include <memory>
#include "net/session/session.h"
#include "net/session/soe/options.h"

namespace hailstone::net::soe {

// The only session implementation shipped in this milestone; additional
// session protocols can register independent factories in later work.
std::unique_ptr<ISession> create_session(Options options = {});

} // namespace hailstone::net::soe
