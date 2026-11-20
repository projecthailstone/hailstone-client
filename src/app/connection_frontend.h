#pragma once

#include <functional>
#include <optional>
#include <string>

#include "net/connection/game_connection.h"

namespace hailstone::app {

// Processes front-end actions before a required-addon wait can stop the frame: a pending /loadskin, then /quit or
// /exit, then a dropped connection. ClientApp::update_connection runs it after a new hailstone.cfg's assets are
// applied (as /loadskin always was) and before its add-on gate. False means a quit or disconnected callback handled
// this connection; the caller must not run its add-on gate or otherwise use the connection again.
bool connection_frontend(net::GameConnection& connection, std::optional<std::string>& pending_skin,
                         const std::function<void(const std::string&)>& load_skin,
                         const std::function<void(net::GameConnection::QuitRequest)>& quit,
                         const std::function<void()>& disconnected);

} // namespace hailstone::app
