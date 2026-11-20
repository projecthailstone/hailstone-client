#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// This test deliberately does not include all_types.h: public message
// ownership must not instantiate the entire game message type universe.
#include "net/messages/message.h"

namespace hm = hailstone::net::messages;

namespace {
struct SmallPayload { std::uint32_t sequence = 0; };
struct LargePayload { std::array<std::uint64_t, 64> words{}; std::string name; };
}

TEST_CASE("semantic payload keeps empty and wrong types distinct") {
    hm::GameMessage empty{hm::GameOpcode::Unknown, std::monostate{}};
    CHECK(hm::message_is<std::monostate>(empty));
    CHECK(hm::message_get<SmallPayload>(empty) == nullptr);
    hm::GameMessage small{hm::GameOpcode::NewSpawn, SmallPayload{37}};
    CHECK(hm::message_get<SmallPayload>(small));
    CHECK(hm::message_get<SmallPayload>(small)->sequence == 37);
    CHECK(hm::message_get<LargePayload>(small) == nullptr);
    CHECK(hm::message_get<SmallPayload>(small, hm::GameOpcode::Unknown) == nullptr);
    CHECK(hm::message_get<SmallPayload>(small, hm::GameOpcode::NewSpawn) != nullptr);
}

TEST_CASE("copy and move retain independent mutable payload ownership") {
    hm::GameMessage a{hm::GameOpcode::NewSpawn, SmallPayload{42}};
    hm::GameMessage b = a;
    hm::message_get<SmallPayload>(b)->sequence = 99;
    CHECK(hm::message_get<SmallPayload>(a)->sequence == 42);
    hm::GameMessage moved = std::move(b);
    CHECK(hm::message_get<SmallPayload>(moved)->sequence == 99);
    CHECK(hm::message_is<std::monostate>(b));

    LargePayload source;
    source.words[12] = 1234;
    source.name = "source";
    hm::GameMessage large{hm::GameOpcode::NewSpawn, source};
    hm::GameMessage copy = large;
    hm::message_get<LargePayload>(copy)->name = "changed";
    CHECK(hm::message_get<LargePayload>(large)->name == "source");
    CHECK(hm::message_get<LargePayload>(copy)->words[12] == 1234);
    hm::GameMessage assigned;
    assigned = std::move(copy);
    CHECK(hm::message_get<LargePayload>(assigned)->name == "changed");
    CHECK(hm::message_is<std::monostate>(copy));
}

TEST_CASE("queued semantic messages retain their own ownership") {
    std::vector<hm::GameMessage> pending;
    pending.emplace_back(hm::GameOpcode::NewSpawn, LargePayload{});
    pending.push_back(pending.front());
    hm::message_get<LargePayload>(pending[0])->words[0] = 22;
    CHECK(hm::message_get<LargePayload>(pending[1])->words[0] == 0);
    pending.erase(pending.begin());
    CHECK(hm::message_get<LargePayload>(pending[0])->words[0] == 0);
}

TEST_CASE("empty payload survives copy, assignment and queue removal") {
    hm::GameMessage request{hm::GameOpcode::ClientReady, std::monostate{}};
    hm::GameMessage copy(request);
    CHECK(hm::message_is<std::monostate>(request));
    CHECK(hm::message_is<std::monostate>(copy));
    request = hm::GameMessage{hm::GameOpcode::NewSpawn, SmallPayload{8}};
    CHECK(hm::message_get<SmallPayload>(request)->sequence == 8);
    CHECK(hm::message_is<std::monostate>(copy));
    request.value.reset();
    CHECK(hm::message_is<std::monostate>(request));
    CHECK(hm::message_get<SmallPayload>(request) == nullptr);
    std::vector<hm::GameMessage> queue;
    queue.push_back(std::move(copy));
    CHECK(hm::message_is<std::monostate>(queue.front()));
    CHECK(hm::message_is<std::monostate>(copy));
}
