#include "net/protocol/profile.h"

#include <utility>

namespace hailstone::net::protocol {

ProtocolProfile ProtocolProfile::rof2_world(session::Configuration options) {
    return {messages::ServiceScope::World, "rof2", "soe", std::move(options)};
}

ProtocolProfile ProtocolProfile::rof2_zone(session::Configuration options) {
    ProtocolProfile profile{messages::ServiceScope::Zone, "rof2", "soe", std::move(options)};
    // Stock RoF2 never opts into vendor extensions. A saved server profile or
    // an explicit ConnectionOptions selection can enable the Hailstone overlay.
    return profile;
}

} // namespace hailstone::net::protocol
