// platform::TcpSocket and TcpListener on the loopback: a connection made without waiting, bytes both ways, the
// peer's close, and a refused connect.
#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "platform/tcp_socket.h"

using namespace hailstone::platform;

namespace {

// Polls `ready` for up to ten seconds; this only bounds a broken run. Linux answers a loopback connect in microseconds,
// but Windows retries a refused one for about two seconds before it fails it.
template <typename F> bool eventually(F&& ready) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < until) {
        if (ready()) return true;
        std::this_thread::yield();
    }
    return ready();
}

// A loopback port that was free a moment ago: the listener is gone again.
std::uint16_t freed_port() {
    TcpListener l;
    std::string error;
    REQUIRE(l.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    return l.local_endpoint()->port;
}

// Connects `s` to `to(port)` for a freed port until one is refused (at once, `at_once`, or closed later), up to 5
// ports: true when it was. A freed port can be taken again before the connect (another test run's listener), and
// Linux can connect a loopback socket to itself when its own port comes out as the freed one; either ends connected.
template <typename To> bool refused_connect(TcpSocket& s, std::string& error, bool& at_once, To&& to) {
    for (int tries = 0; tries < 5; ++tries) {
        s = TcpSocket{};
        error.clear();
        at_once = !s.connect(to(freed_port()), error);
        if (at_once) return true;
        if (!eventually([&] { return s.state() != TcpSocket::State::Connecting; })) return true; // the CHECKs fail
        if (s.state() != TcpSocket::State::Connected) return true;
    }
    return false;
}

std::span<const std::byte> as_bytes(const std::string& s) {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

} // namespace

TEST_CASE("tcp socket: connects without waiting, carries bytes both ways and sees the peer's close") {
    TcpListener listener;
    std::string error;
    REQUIRE(listener.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    const auto at = listener.local_endpoint();
    REQUIRE(at);
    REQUIRE(at->port != 0);

    TcpSocket client;
    REQUIRE(client.connect(*at, error));
    std::optional<TcpSocket> server;
    REQUIRE(eventually([&] { return (server = listener.accept()).has_value(); }));
    REQUIRE(eventually([&] { return client.state() == TcpSocket::State::Connected; }));
    CHECK(server->state() == TcpSocket::State::Connected);
    // Neither end is handed to a child process (close-on-exec; on Windows not inheritable).
    CHECK_FALSE(client.inherited_by_children());
    CHECK_FALSE(server->inherited_by_children());

    CHECK(client.send(as_bytes("hello")) == 5u);
    std::vector<std::byte> buf(64);
    std::size_t got = 0;
    REQUIRE(eventually([&] {
        auto n = server->recv(buf);
        if (n) got += *n;
        return got == 5;
    }));
    CHECK(std::string(reinterpret_cast<const char*>(buf.data()), 5) == "hello");
    CHECK(server->recv(buf) == 0u); // nothing more yet

    server->close();
    REQUIRE(eventually([&] { return !client.recv(buf).has_value(); }));
    CHECK(client.state() == TcpSocket::State::Closed);
    CHECK_FALSE(client.last_error().empty());
    CHECK_FALSE(client.send(as_bytes("x")).has_value());
}

TEST_CASE("tcp socket: a connect nobody listens for ends closed") {
    TcpSocket s;
    std::string error;
    bool at_once = false;
    REQUIRE(refused_connect(s, error, at_once, [](std::uint16_t port) { return Endpoint::v4(127, 0, 0, 1, port); }));
    if (at_once) {
        CHECK_FALSE(error.empty());
        return;
    }
    CHECK(s.state() == TcpSocket::State::Closed);
    CHECK_FALSE(s.last_error().empty());
}

TEST_CASE("tcp socket: an address that fails hands over to the next one") {
    const std::uint16_t closed = freed_port();
    TcpListener listener;
    std::string error;
    REQUIRE(listener.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    // The first refuses (at once, or later: Windows retries a refused loopback connect for about two seconds), the
    // second listens: as a name's IPv4 address that fails before its IPv6 one does.
    TcpSocket s;
    REQUIRE(s.connect(std::vector<Endpoint>{Endpoint::v4(127, 0, 0, 1, closed), *listener.local_endpoint()}, error));
    std::optional<TcpSocket> server;
    REQUIRE(eventually([&] {
        if (!server) server = listener.accept();
        return s.state() != TcpSocket::State::Connecting;
    }));
    CHECK(s.state() == TcpSocket::State::Connected);

    // None left: closed (at once or later), with the last one's reason.
    TcpSocket none;
    bool at_once = false;
    REQUIRE(refused_connect(none, error, at_once, [](std::uint16_t port) {
        return std::vector<Endpoint>{Endpoint::v4(127, 0, 0, 1, port)};
    }));
    if (at_once) {
        CHECK_FALSE(error.empty());
        return;
    }
    CHECK(none.state() == TcpSocket::State::Closed);
    CHECK_FALSE(none.last_error().empty());
}

TEST_CASE("tcp socket: a connect with no answer times out and tries the next address") {
    // A listener whose queue is full and never accepted: Linux and macOS drop further connects' SYNs, so those
    // connects get no answer at all (Windows refuses them, which hands over the same way).
    TcpListener full;
    std::string error;
    REQUIRE(full.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    std::vector<TcpSocket> filler(24);
    for (auto& f : filler) f.connect(*full.local_endpoint(), error);
    for (int i = 0; i < 20; ++i)
        for (auto& f : filler) f.state();

    TcpListener good;
    REQUIRE(good.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    TcpSocket s;
    s.set_connect_timeout(std::chrono::milliseconds(200));
    REQUIRE(s.connect(std::vector<Endpoint>{*full.local_endpoint(), *good.local_endpoint()}, error));
    std::optional<TcpSocket> server;
    REQUIRE(eventually([&] {
        if (!server) server = good.accept();
        return s.state() != TcpSocket::State::Connecting;
    }));
    CHECK(s.state() == TcpSocket::State::Connected);

    // Alone, the silent one ends closed once the timeout passes.
    TcpSocket alone;
    alone.set_connect_timeout(std::chrono::milliseconds(200));
    REQUIRE(alone.connect(*full.local_endpoint(), error));
    REQUIRE(eventually([&] { return alone.state() != TcpSocket::State::Connecting; }));
    if (alone.state() == TcpSocket::State::Connected) {
        MESSAGE("this system answered a connect to a full listen queue; the timeout itself was not exercised");
        return;
    }
    CHECK_FALSE(alone.last_error().empty());
}
