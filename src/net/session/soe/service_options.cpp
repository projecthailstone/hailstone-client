#include "net/session/soe/service_options.h"

namespace hailstone::net::soe {

Options defaults(messages::ServiceScope service) {
    Options options;
    switch (service) {
    case messages::ServiceScope::Login:
        options.opcode_bytes = 2;
        options.hold_time = std::chrono::milliseconds{0};
        options.stat_interval = std::chrono::milliseconds{0};
        options.keepalive_interval = std::chrono::milliseconds{14800};
        options.idle_timeout = std::chrono::milliseconds{14800};
        options.resend_delay_adjust = 2000;
        options.window_slots = 100;
        break;
    case messages::ServiceScope::Chat:
        options.opcode_bytes = 1;
        options.keepalive_interval = std::chrono::milliseconds{44000};
        options.idle_timeout = std::chrono::milliseconds{0};
        options.request_interval = std::chrono::milliseconds{5000};
        options.window_bytes = 10000;
        options.reliable_timeout_ms = 120000;
        break;
    case messages::ServiceScope::World:
    case messages::ServiceScope::Zone:
        // Both inherit SOE's original 150ms hold, 20ms service pass,
        // 15s keepalive and two-byte application opcode.
        break;
    }
    return options;
}

bool valid(const Options& x) {
    return (x.opcode_bytes == 1 || x.opcode_bytes == 2) &&
           x.max_packet_size >= 64 && x.max_packet_size <= 65535 &&
           x.window_slots > 0 && x.window_slots <= 65536 &&
           x.window_bytes > 0 && x.max_received_bytes > 0 &&
           x.connect_timeout.count() > 0 &&
           x.request_interval.count() > 0 &&
           x.service_interval.count() >= 0 && x.hold_time.count() >= 0 &&
           x.reliable_timeout_ms > 0;
}

} // namespace hailstone::net::soe
