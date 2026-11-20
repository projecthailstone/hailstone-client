#include <doctest/doctest.h>

#include <string>
#include <type_traits>

#include "app/login_token_session.h"
#include "net/messages/login.h"

using namespace hailstone;

static_assert(!std::is_copy_constructible_v<app::LoginTokenSession>);
static_assert(!std::is_copy_assignable_v<app::LoginTokenSession>);

TEST_CASE("login token session: a returned token is bound to its login endpoint and can be taken once") {
    app::LoginTokenSession session;
    std::string taken;
    CHECK_FALSE(session.available("login.example", 5999));
    REQUIRE(session.remember("Login.Example", 5999, "synthetic-returned-token"));
    CHECK(session.available("login.example", 5999));
    CHECK(session.available("LOGIN.EXAMPLE", 5999));
    CHECK_FALSE(session.available("other.example", 5999));
    CHECK_FALSE(session.available("login.example", 5997));
    CHECK_FALSE(session.take("other.example", 5999, taken));
    CHECK(taken.empty());
    CHECK_FALSE(session.take("login.example", 5997, taken));
    CHECK(session.available("login.example", 5999));
    CHECK(session.take("login.EXAMPLE", 5999, taken));
    CHECK(taken == "synthetic-returned-token");
    CHECK_FALSE(session.available("login.example", 5999));
    CHECK_FALSE(session.take("login.example", 5999, taken));
    CHECK(taken.empty()); // an earlier output cannot become an implicit fallback
}

TEST_CASE("login token session: each successful reply rotates the next-login token and endpoint") {
    app::LoginTokenSession session;
    std::string taken;
    REQUIRE(session.remember("one.example", 5999, "earlier-token"));
    REQUIRE(session.remember("one.example", 5999, "replacement-token"));
    CHECK(session.take("one.example", 5999, taken));
    CHECK(taken == "replacement-token");
    REQUIRE(session.remember("one.example", 5999, "old-endpoint-token"));
    REQUIRE(session.remember("two.example", 6000, "new-endpoint-token"));
    CHECK_FALSE(session.available("one.example", 5999));
    CHECK_FALSE(session.take("one.example", 5999, taken));
    CHECK(taken.empty());
    CHECK(session.take("two.example", 6000, taken));
    CHECK(taken == "new-endpoint-token");
}

TEST_CASE("login token session: an empty or invalid replacement clears the earlier token") {
    app::LoginTokenSession session;
    std::string taken;
    for (const auto& invalid : {std::string(), std::string("one\0two", 7),
                                std::string(net::messages::kMaxLoginTokenBytes + 1, 'x')}) {
        REQUIRE(session.remember("login.example", 5999, "earlier-token"));
        CHECK_FALSE(session.remember("login.example", 5999, invalid));
        CHECK_FALSE(session.available("login.example", 5999));
        CHECK_FALSE(session.take("login.example", 5999, taken));
        CHECK(taken.empty());
    }
    const std::string maximum(net::messages::kMaxLoginTokenBytes, 'x');
    REQUIRE(session.remember("login.example", 5999, maximum));
    CHECK(session.take("login.example", 5999, taken));
    CHECK(taken == maximum);
}

TEST_CASE("login token session: invalid endpoints forget an earlier token") {
    app::LoginTokenSession session;
    for (const int port : {0, -1, 65536}) {
        REQUIRE(session.remember("login.example", 5999, "earlier-token"));
        CHECK_FALSE(session.remember("login.example", port, "replacement-token"));
        CHECK_FALSE(session.available("login.example", 5999));
    }
    REQUIRE(session.remember("login.example", 5999, "earlier-token"));
    CHECK_FALSE(session.remember({}, 5999, "replacement-token"));
    CHECK_FALSE(session.available("login.example", 5999));
    REQUIRE(session.remember("login.example", 5999, "earlier-token"));
    CHECK_FALSE(session.remember(std::string("one\0two", 7), 5999, "replacement-token"));
    CHECK_FALSE(session.available("login.example", 5999));
}

TEST_CASE("login token session: explicit forget removes the available token") {
    app::LoginTokenSession session;
    std::string taken = "discarded-output-token";
    REQUIRE(session.remember("login.example", 5999, "synthetic-token"));
    session.clear();
    session.clear();
    CHECK_FALSE(session.available("login.example", 5999));
    CHECK_FALSE(session.take("login.example", 5999, taken));
    CHECK(taken.empty());
}
