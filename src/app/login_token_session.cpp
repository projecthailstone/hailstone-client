#include "app/login_token_session.h"

#include <utility>

#include "core/ascii_case.h"
#include "core/wipe.h"
#include "net/messages/login.h"

namespace hailstone::app {

LoginTokenSession::~LoginTokenSession() { clear(); }

bool LoginTokenSession::remember(std::string_view host, int port, std::string_view token) {
    clear();
    if (host.empty() || host.find('\0') != std::string_view::npos || port < 1 || port > 65535 ||
        !net::messages::valid_login_token(token)) return false;
    host_ = host;
    port_ = port;
    token_ = token;
    return true;
}

bool LoginTokenSession::available(std::string_view host, int port) const {
    return !token_.empty() && port == port_ && iequals(host_, host);
}

bool LoginTokenSession::take(std::string_view host, int port, std::string& out) {
    core::wipe(out);
    if (!available(host, port)) return false;
    out = std::move(token_);
    // A short moved-from string can retain its bytes in the source object's own storage.
    clear();
    return true;
}

void LoginTokenSession::clear() {
    core::wipe(token_);
    host_.clear();
    port_ = 0;
}

} // namespace hailstone::app
