#include "net/session/udp/transport.h"

#include <utility>

namespace hailstone::net::udp {
namespace {

class SocketTransport final : public DatagramTransport {
public:
    bool open(Endpoint::Family family, std::string& error) override { return socket_.open(family, error); }
    void close() override { socket_.close(); }
    bool is_open() const override { return socket_.is_open(); }
    bool send_to(const Endpoint& to, std::span<const std::byte> data) override { return socket_.send_to(to, data); }
    std::optional<std::size_t> recv_from(std::span<std::byte> buffer, Endpoint& from) override {
        return socket_.recv_from(buffer, from);
    }
    const std::string& last_error() const override { return socket_.last_error(); }
private:
    platform::UdpSocket socket_;
};

} // namespace

std::unique_ptr<DatagramTransport> make_socket_transport() {
    return std::make_unique<SocketTransport>();
}

std::optional<Endpoint> numeric_endpoint(const std::string& host, std::uint16_t port) {
    return platform::numeric_endpoint(host, port);
}

void resolve_async(std::string host, std::uint16_t port,
                   std::function<void(std::vector<Endpoint>, std::string)> done) {
    platform::resolve_async(std::move(host), port, std::move(done));
}

} // namespace hailstone::net::udp
