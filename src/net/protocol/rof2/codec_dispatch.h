#pragma once

#include <optional>

#include "net/messages/message.h"
#include "net/wire/app_packet.h"
#include "net/messages/result.h"
#include "net/protocol/codec.h"

namespace hailstone::net::rof2::detail {
std::optional<AppPacket> make_packet(messages::GameOpcode, const messages::GameMessage&);
std::optional<messages::GameMessage> parse_packet(messages::GameOpcode, const AppPacket&);
std::optional<messages::CodecError> validate_command_variant(const messages::GameMessage&);
bool implemented(messages::GameOpcode, protocol::CodecDirection);
std::optional<AppPacket> encode_control(messages::GameOpcode, const messages::GameMessage&);
std::optional<AppPacket> encode_tasks(messages::GameOpcode, const messages::GameMessage&);
std::optional<AppPacket> encode_social(messages::GameOpcode, const messages::GameMessage&);
std::optional<AppPacket> encode_items(messages::GameOpcode, const messages::GameMessage&);
std::optional<AppPacket> encode_commerce(messages::GameOpcode, const messages::GameMessage&);
std::optional<AppPacket> encode_marketplace(messages::GameOpcode, const messages::GameMessage&);
std::optional<AppPacket> encode_world(messages::GameOpcode, const messages::GameMessage&);
std::optional<AppPacket> encode_progression(messages::GameOpcode, const messages::GameMessage&);
std::optional<AppPacket> encode_zone(messages::GameOpcode, const messages::GameMessage&);
std::optional<AppPacket> encode_combat_spell(messages::GameOpcode, const messages::GameMessage&);
std::optional<messages::GameMessage> decode_service(messages::GameOpcode, const AppPacket&);
std::optional<messages::GameMessage> decode_zone_state(messages::GameOpcode, const AppPacket&);
std::optional<messages::GameMessage> decode_spell_combat(messages::GameOpcode, const AppPacket&);
std::optional<messages::GameMessage> decode_items_trade(messages::GameOpcode, const AppPacket&);
std::optional<messages::GameMessage> decode_social(messages::GameOpcode, const AppPacket&);
std::optional<messages::GameMessage> decode_commerce(messages::GameOpcode, const AppPacket&);
std::optional<messages::GameMessage> decode_marketplace(messages::GameOpcode, const AppPacket&);
std::optional<messages::GameMessage> decode_world_zone(messages::GameOpcode, const AppPacket&);

} // namespace hailstone::net::rof2::detail
