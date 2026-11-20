#pragma once

#include <span>

#include "net/messages/guild_banner.h"
#include "net/wire/app_packet.h"

namespace hailstone::net {
AppPacket encode_guild_banner_open(std::uint32_t guild_id, std::uint32_t character_id);
AppPacket encode_guild_banner_kit_request(std::uint32_t character_id, bool show_all, unsigned filter,
                                         std::span<const std::uint32_t> owned);
AppPacket encode_guild_banner_status_request(std::uint32_t guild_id, std::uint32_t character_id);
AppPacket encode_guild_banner_save(std::uint32_t character_id, const GuildBannerStyle& style, std::uint32_t token_id);
// Position is actor/world XYZ, in server Y, X, Z order.
AppPacket encode_guild_banner_plant(std::uint32_t kit_id, bool from_list, float x, float y, float z,
                                   bool reapply_style = false);
AppPacket encode_guild_banner_remove(std::uint32_t guild_id);

std::optional<GuildBannerKitList> decode_guild_banner_kits(const AppPacket& p);
std::optional<GuildBannerStatus> decode_guild_banner_status(const AppPacket& p);
std::optional<GuildBannerModificationToken> decode_guild_banner_token(const AppPacket& p);

} // namespace hailstone::net
