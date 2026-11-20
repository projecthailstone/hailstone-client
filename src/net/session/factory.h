#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "net/session/session.h"
#include "net/session/configuration.h"
#include "net/session/soe/session.h"

namespace hailstone::net::session {

class SessionRegistry {
public:
    // Each session implementation validates its own opaque parameters.
    using Factory = std::function<std::unique_ptr<ISession>(const Configuration&)>;

    SessionRegistry() {
        register_factory("soe", [](const Configuration& config) -> std::unique_ptr<ISession> {
            soe::Options options;
            if (config.parameters.has_value()) {
                const auto* selected = std::any_cast<soe::Options>(&config.parameters);
                if (!selected) return {}; // wrong settings type; never default silently
                options = *selected;
            }
            if (config.clock) options.clock = config.clock;
            return soe::create_session(std::move(options));
        });
    }

    bool register_factory(std::string id, Factory factory) {
        if (id.empty() || !factory) return false;
        return factories_.emplace(std::move(id), std::move(factory)).second;
    }

    std::unique_ptr<ISession> create(std::string_view id, const Configuration& config) const {
        if (id.empty() || config.protocol != id) return {};
        auto it = factories_.find(id);
        if (it == factories_.end()) return {};
        return it->second(config);
    }

private:
    std::map<std::string, Factory, std::less<>> factories_;
};

} // namespace hailstone::net::session
