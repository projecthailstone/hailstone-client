// netprobe [-v] login HOST[:PORT] USER PASS [SERVER_NAME]
// Logs into an EQEmu login server (SoD/RoF2 port, default 5999), prints the
// login result and server list, and asks to play on SERVER_NAME (or the
// first server). Prints what the world connection needs. -v hex-dumps every
// protocol packet.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "core/log.h"
#include "net/connection/login_client.h"

namespace {

using namespace hailstone;

void hexdump(bool sent, std::span<const std::byte> p) {
    std::printf("%s %zu bytes\n", sent ? ">>" : "<<", p.size());
    for (std::size_t i = 0; i < p.size(); i += 16) {
        std::printf("   %04zx ", i);
        for (std::size_t j = i; j < i + 16; ++j) {
            if (j < p.size()) std::printf(" %02x", std::to_integer<unsigned>(p[j]));
            else std::printf("   ");
        }
        std::printf("  ");
        for (std::size_t j = i; j < i + 16 && j < p.size(); ++j) {
            auto c = std::to_integer<unsigned char>(p[j]);
            std::putchar(c >= 32 && c < 127 ? c : '.');
        }
        std::putchar('\n');
    }
}

bool split_host_port(const std::string& arg, std::string& host, std::uint16_t& port) {
    std::string h = arg;
    std::string p;
    if (!h.empty() && h.front() == '[') { // [v6]:port
        auto close = h.find(']');
        if (close == std::string::npos) return false;
        if (close + 1 < h.size() && h[close + 1] == ':') p = h.substr(close + 2);
        h = h.substr(1, close - 1);
    } else if (auto colon = h.rfind(':'); colon != std::string::npos && h.find(':') == colon) {
        p = h.substr(colon + 1);
        h = h.substr(0, colon);
    }
    host = h;
    if (!p.empty()) {
        unsigned long v = std::strtoul(p.c_str(), nullptr, 10);
        if (v == 0 || v > 65535) return false;
        port = static_cast<std::uint16_t>(v);
    }
    return !host.empty();
}

const char* status_text(std::int32_t status) {
    if (status & 4) return (status & 1) ? "locked (down)" : "locked";
    if (status & 1) return "down";
    return "up";
}

// Runs the client until it leaves the given state or fails.
bool pump(net::LoginClient& c, net::LoginState waiting_in) {
    while (c.state() == waiting_in || c.state() == net::LoginState::Connecting ||
           c.state() == net::LoginState::Handshake) {
        c.update();
        if (c.state() == net::LoginState::Failed) return false;
        if (c.state() != waiting_in && c.state() != net::LoginState::Connecting &&
            c.state() != net::LoginState::Handshake)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return c.state() != net::LoginState::Failed;
}

int login(const std::string& target, const std::string& user, const std::string& pass, const char* wanted, bool verbose) {
    std::string host;
    std::uint16_t port = 5999;
    if (!split_host_port(target, host, port)) {
        std::fprintf(stderr, "netprobe: bad address %s\n", target.c_str());
        return 2;
    }
    net::LoginClient::Options opt;
    if (verbose) opt.session.trace = hexdump;
    net::LoginClient client(opt);
    std::printf("connecting to %s port %u as %s\n", host.c_str(), port, user.c_str());
    client.start(host, port, user, pass);

    if (!pump(client, net::LoginState::Authenticating)) {
        std::fprintf(stderr, "login failed: %s\n", client.error().c_str());
        return 1;
    }
    const auto& lr = client.login_result();
    std::printf("login accepted: account id %u, session key %s, failed attempts %d, show player count %s\n",
                lr.account_id, lr.session_key.c_str(), lr.failed_attempts, lr.show_player_count ? "yes" : "no");

    client.request_server_list();
    if (!pump(client, net::LoginState::RequestingList)) {
        std::fprintf(stderr, "server list failed: %s\n", client.error().c_str());
        return 1;
    }
    std::printf("%zu server(s):\n", client.servers().size());
    std::printf("  %-4s %-30s %-16s %-14s %-7s %s\n", "id", "name", "address", "status", "players", "type");
    const net::ServerInfo* chosen = nullptr;
    for (const auto& s : client.servers()) {
        std::printf("  %-4u %-30s %-16s %-14s %-7d %d\n", s.id, s.name.c_str(), s.address.c_str(), status_text(s.status),
                    s.players, s.type);
        if (!chosen && (!wanted || s.name == wanted)) chosen = &s;
    }
    if (!chosen) {
        std::fprintf(stderr, "no server named \"%s\"\n", wanted ? wanted : "");
        return 1;
    }

    std::printf("requesting play on %u (%s)\n", chosen->id, chosen->name.c_str());
    client.request_play(chosen->id);
    if (!pump(client, net::LoginState::RequestingPlay)) {
        std::fprintf(stderr, "play failed: %s\n", client.error().c_str());
        return 1;
    }
    auto t = client.ticket();
    std::printf("play accepted (server %u)\n", client.play_result().server_id);
    std::printf("world connection: %s:%u, OP_SendLoginInfo login \"%u\" password \"%s\"\n", t->world_host.c_str(),
                t->world_port, t->account_id, t->session_key.c_str());

    client.close();
    for (int i = 0; i < 100 && client.state() == net::LoginState::PlayAccepted; ++i) {
        client.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool verbose = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; ++i) {
        if (std::strcmp(argv[i], "-v") == 0) verbose = true;
        else {
            std::fprintf(stderr, "netprobe: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    if (argc - i >= 4 && std::strcmp(argv[i], "login") == 0) {
        if (verbose) hailstone::log::set_level(hailstone::log::Level::Debug);
        return login(argv[i + 1], argv[i + 2], argv[i + 3], argc - i >= 5 ? argv[i + 4] : nullptr, verbose);
    }
    std::fprintf(stderr, "usage: netprobe [-v] login HOST[:PORT] USER PASS [SERVER_NAME]\n");
    return 2;
}
