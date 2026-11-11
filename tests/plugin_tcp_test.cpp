// plugin/plugin_tcp.h: a plugin's own TCP connection (tcp_open ...) on the loopback: a name looked up off the
// caller's thread with what was sent meanwhile kept, and a link that is always a little behind keeping only what it
// has not taken.
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "platform/tcp_socket.h"
#include "plugin/plugin_tcp.h"

using namespace hailstone;
using platform::Endpoint;
using platform::TcpListener;
using platform::TcpSocket;
using plugin::PluginTcp;

namespace {

// Polls `ready` for up to 30 seconds; this only bounds a broken run.
template <typename F> bool eventually(F&& ready) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < until) {
        if (ready()) return true;
        std::this_thread::yield();
    }
    return ready();
}

std::span<const std::byte> as_bytes(const std::string& s) {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

} // namespace

TEST_CASE("plugin tcp: a host name is looked up without waiting, and what was sent meanwhile goes out") {
    TcpListener listener;
    std::string error;
    REQUIRE(listener.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    const auto at = listener.local_endpoint();
    REQUIRE(at);
    auto conn = PluginTcp::open("localhost", at->port, error); // the hosts file's name, no name server
    REQUIRE(conn);
    CHECK(conn->send(as_bytes("hello")) == 5); // kept while the name is looked up or the connect goes on
    std::optional<TcpSocket> server;
    REQUIRE(eventually([&] {
        conn->state();
        return (server = listener.accept()).has_value();
    }));
    REQUIRE(eventually([&] { return conn->state() == 1; }));
    std::vector<std::byte> buf(64);
    std::size_t got = 0;
    REQUIRE(eventually([&] {
        conn->recv(std::span<std::byte>(buf.data(), 0)); // any call sends what waits
        auto n = server->recv(std::span<std::byte>(buf).subspan(got));
        if (n) got += *n;
        return got == 5;
    }));
    CHECK(std::string(reinterpret_cast<const char*>(buf.data()), 5) == "hello");
}

TEST_CASE("plugin tcp: a numeric address connects at once; a closed one reads -1") {
    TcpListener listener;
    std::string error;
    REQUIRE(listener.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    auto conn = PluginTcp::open("127.0.0.1", listener.local_endpoint()->port, error);
    REQUIRE(conn);
    std::optional<TcpSocket> server;
    REQUIRE(eventually([&] { return (server = listener.accept()).has_value(); }));
    REQUIRE(eventually([&] { return conn->state() == 1; }));
    server->close();
    std::vector<std::byte> buf(16);
    REQUIRE(eventually([&] { return conn->recv(buf) == -1; }));
    CHECK(conn->state() == -1);
    CHECK(conn->send(as_bytes("x")) == -1);
    CHECK_FALSE(conn->error().empty());
}

TEST_CASE("plugin tcp: a link always a little behind keeps only what it has not taken") {
    // The reader takes as much as is sent, so the queue never empties. What the system took must still go, or the
    // queue would hold every byte ever sent (and the 16 MiB limit, on what waits, would never see it).
    TcpListener listener;
    std::string error;
    REQUIRE(listener.listen(Endpoint::v4(127, 0, 0, 1, 0), error));
    auto conn = PluginTcp::open("127.0.0.1", listener.local_endpoint()->port, error);
    REQUIRE(conn);
    std::optional<TcpSocket> server;
    REQUIRE(eventually([&] { return (server = listener.accept()).has_value(); }));
    REQUIRE(eventually([&] { return conn->state() == 1; }));
    const std::vector<std::byte> chunk(64 * 1024, std::byte{0x5a});
    // Fill the system's buffers until 4 MiB more wait here.
    REQUIRE(eventually([&] {
        CHECK(conn->send(chunk) == static_cast<std::int64_t>(chunk.size()));
        return conn->waiting_for_test() >= (std::size_t{4} << 20);
    }));
    std::vector<std::byte> buf(chunk.size());
    std::size_t moved = 0, most = 0, most_waiting = 0;
    REQUIRE(eventually([&] {
        auto n = server->recv(buf);
        REQUIRE(n);
        if (*n) {
            moved += *n;
            REQUIRE(conn->send(std::span<const std::byte>(chunk).first(*n)) == static_cast<std::int64_t>(*n));
            most = std::max(most, conn->kept_for_test());
            most_waiting = std::max(most_waiting, conn->waiting_for_test());
        }
        return moved >= (std::size_t{48} << 20);
    }));
    // Something waited while the reader lagged. Whether some still waits once the loop stops depends on how fast the
    // system drains loopback (on some hosts it drains it all), so that is not checked.
    CHECK(most_waiting > 0);
    // At most about twice what waits, not the 48 MiB that went through.
    CHECK(most <= 2 * most_waiting + 2 * chunk.size());
    CHECK(most < (std::size_t{48} << 20));
}
