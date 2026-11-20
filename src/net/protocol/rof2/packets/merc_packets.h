#pragma once
#include <optional>
#include "net/messages/mercenary.h"
#include "net/wire/app_packet.h"
namespace hailstone::net::rof2 {
std::optional<messages::MercenaryNotice> decode_mercenary_notice(const AppPacket& packet);
std::optional<AppPacket> encode_mercenary_request(const messages::MercenaryRequest& request);
} // namespace hailstone::net::rof2
