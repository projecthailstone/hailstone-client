#include "net/protocol/ucs/session_options.h"
#include "net/session/soe/configuration.h"
namespace hailstone::net {
session::Configuration ucs_session_configuration(bool short_keepalive) {
    soe::Options opt;
    opt.connect_timeout = std::chrono::milliseconds(30000);
    opt.request_interval = std::chrono::milliseconds(5000);
    // Hailstone uses a 60 s no-data timeout to recover from silent UCS peers.
    // The original client left this timer disabled.
    opt.idle_timeout = std::chrono::milliseconds(60000);
    opt.keepalive_interval = std::chrono::milliseconds(short_keepalive ? 14800 : 44000);
    opt.opcode_bytes = 1;
    opt.hold_time = std::chrono::milliseconds(0);
    opt.stat_interval = std::chrono::milliseconds(0);
    opt.window_slots = 24;
    opt.window_bytes = 10000;
    opt.reliable_timeout_ms = 120000;
    return soe::configuration(std::move(opt));
}

} // namespace hailstone::net
