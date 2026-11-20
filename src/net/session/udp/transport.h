#pragma once

// Raw UDP datagrams and name resolution only. No SOE opcodes, negotiated
// parameters, sequence numbers or game application payloads cross this API.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "platform/udp_socket.h"

namespace hailstone::net::udp {

using Endpoint = platform::Endpoint;

class DatagramTransport {
public:
    virtual ~DatagramTransport() = default;
    virtual bool open(Endpoint::Family family, std::string& error) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual bool send_to(const Endpoint&, std::span<const std::byte>) = 0;
    virtual std::optional<std::size_t> recv_from(std::span<std::byte>, Endpoint&) = 0;
    virtual const std::string& last_error() const = 0;
};

std::unique_ptr<DatagramTransport> make_socket_transport();
std::optional<Endpoint> numeric_endpoint(const std::string& host, std::uint16_t port);
void resolve_async(std::string host, std::uint16_t port,
                   std::function<void(std::vector<Endpoint>, std::string)> done);

} // namespace hailstone::net::udp
