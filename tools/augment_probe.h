#pragma once

// playprobe's augment check (included by tools/playprobe.cpp):
//   --augment HOST|SOCKET  once the cursor holds an augment (summon it with --cmd "#summonitem ID") and the bags hold
//                          the item HOST (an item id; --cmd "#summonitem HOST" then "/autoinventory" first), clicks
//                          socket SOCKET (0-based) of HOST's item display with it (GameConnection::augment_click, the
//                          display's socket click: insert), answers yes to a confirmation, and prints the refusal or
//                          the host's sockets as the server sends the item back
// Prints the augment packets it sees.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game/augments.h"
#include "net/connection/game_connection.h"
#include "net/protocol/rof2/opcode_map.h"

namespace hailstone::probe {

class AugmentProbe {
public:
    using Clock = std::chrono::steady_clock;

    bool option(const char* name, const char* value) {
        if (std::strcmp(name, "--augment") != 0) return false;
        std::string v = value;
        auto bar = v.find('|');
        host_id_ = static_cast<std::uint32_t>(std::atoi(v.substr(0, bar).c_str()));
        socket_ = bar == std::string::npos ? 0 : std::atoi(v.c_str() + bar + 1);
        return true;
    }

    void tap(const net::AppPacket& p) {
        if (!host_id_) return;
        net::rof2::Op op = net::rof2::from_wire(p.opcode);
        if (op == net::rof2::Op::AugmentItem || op == net::rof2::Op::AugmentInfo)
            std::printf("  AUGMENT: got %s (%zu bytes)\n", net::rof2::op_name(op), p.data.size());
    }

    void zone_in(net::GameConnection&) { since_ = Clock::now(); }

    bool busy() const { return host_id_ && phase_ != Phase::Done; }

    void step(net::GameConnection& conn) {
        if (!host_id_ || phase_ == Phase::Done) return;
        auto now = Clock::now();
        if (now - since_ > std::chrono::seconds(90)) {
            std::printf("  AUGMENT: gave up in phase %d\n", static_cast<int>(phase_));
            phase_ = Phase::Done;
            return;
        }
        switch (phase_) {
        case Phase::Wait: {
            const net::Item* aug = conn.inventory().cursor();
            const net::Item* host = find_host(conn);
            if (!aug || !host || now - since_ < std::chrono::seconds(4)) return;
            std::printf("  AUGMENT: host %s (id %u) at %d/%d/%d, cursor %s (id %u, aug type %#x)\n", host->name.c_str(),
                        host->id, host->slot.type, host->slot.slot, host->slot.sub, aug->name.c_str(), aug->id,
                        aug->aug_type);
            print_sockets(*host);
            conn.augment_click(*host, socket_);
            phase_ = Phase::Clicked;
            at_ = now;
            return;
        }
        case Phase::Clicked:
            if (conn.confirmation() && !answered_) {
                std::printf("  AUGMENT: confirmation %#x: %s -> yes\n", conn.confirmation()->id,
                            conn.confirmation()->text.c_str());
                conn.answer_confirmation(true);
                answered_ = true;
            }
            if (now - at_ < std::chrono::seconds(5)) return;
            if (const net::Item* host = find_host(conn)) print_sockets(*host);
            std::printf("  AUGMENT: cursor now %s\n", conn.inventory().cursor() ? conn.inventory().cursor()->name.c_str() : "empty");
            phase_ = Phase::Done;
            return;
        default: return;
        }
    }

private:
    enum class Phase { Wait, Clicked, Done };

    const net::Item* find_host(const net::GameConnection& conn) const {
        for (const auto& [key, it] : conn.inventory().items())
            if (it.id == host_id_ && it.slot.slot != net::kSlotCursor) return &it;
        return nullptr;
    }

    void print_sockets(const net::Item& host) const {
        std::printf("  AUGMENT: %s sockets:", host.name.c_str());
        for (int i = 0; i < 6; ++i) {
            const net::Item* a = game::augment_in(host, i);
            std::printf(" [%d type %u: %s]", i, game::aug_socket_type(host, i), a ? a->name.c_str() : "-");
        }
        std::printf("\n");
    }

    std::uint32_t host_id_ = 0;
    int socket_ = 0;
    bool answered_ = false;
    Phase phase_ = Phase::Wait;
    Clock::time_point since_{Clock::now()}, at_{};
};

} // namespace hailstone::probe
