#pragma once

#include <string>
#include <string_view>

namespace hailstone::app {

// A login server's replacement token, held for an explicit next sign-in to that same endpoint. Launcher tickets
// never enter this store. The caller remembers only a successful reply and transfers a token at most once.
class LoginTokenSession {
public:
    LoginTokenSession() = default;
    ~LoginTokenSession();
    LoginTokenSession(const LoginTokenSession&) = delete;
    LoginTokenSession& operator=(const LoginTokenSession&) = delete;

    // Every reply replaces the earlier token, including an empty or invalid replacement which forgets it.
    bool remember(std::string_view host, int port, std::string_view token);
    bool available(std::string_view host, int port) const;
    // Wipes out first. A mismatched endpoint cannot consume the stored credential; a match transfers it once.
    bool take(std::string_view host, int port, std::string& out);
    void clear();

private:
    std::string host_;
    int port_ = 0;
    std::string token_;
};

} // namespace hailstone::app
