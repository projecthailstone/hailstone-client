#pragma once

#include <string>
#include <vector>

#include "net/messages/opcode.h"
#include "net/session/configuration.h"

namespace hailstone::net::protocol {

// Endpoint-specific selection; application and session implementations are
// independent. A profile does not carry wire opcode values or packet layouts.
// SOE is the only implemented session flavor in the RoF2-first milestone.
struct ProtocolProfile {
    messages::ServiceScope service = messages::ServiceScope::Zone;
    std::string game_codec = "rof2";
    std::string session_protocol = "soe";
    session::Configuration session_config{};
    // Optional application overlays, never silently inferred from RoF2.
    // Probe messages do not grant capabilities until a valid reply arrives.
    std::vector<std::string> extension_codecs{};

    static ProtocolProfile rof2_world(session::Configuration options = {});
    static ProtocolProfile rof2_zone(session::Configuration options = {});
};

} // namespace hailstone::net::protocol
