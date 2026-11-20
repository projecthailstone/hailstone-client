#include "net/protocol/login/session_settings.h"
#include "net/session/soe/configuration.h"

namespace hailstone::net {
session::Configuration login_session_configuration() {
    // eqmain login: 14.8 s keepalive, no buffering/stats, 100 slots.
    soe::Options options;
    options.idle_timeout = std::chrono::milliseconds(14800);
    options.keepalive_interval = std::chrono::milliseconds(14800);
    options.hold_time = std::chrono::milliseconds(0);
    options.stat_interval = std::chrono::milliseconds(0);
    options.resend_delay_adjust = 2000;
    options.window_slots = 100;
    return soe::configuration(std::move(options));
}
}
