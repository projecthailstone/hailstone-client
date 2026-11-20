#pragma once

// The guild bank in EQEmu's layouts (zone/guild_mgr.cpp SendGuildBank, zone/client_packet.cpp Handle_OP_GuildBank,
// common/patches/rof2.cpp ENCODE / DECODE OP_GuildBank: RoF2's actions are EQEmu's less one); the client's requests
// as CGuildBankWnd sends them (Send_OP_GuildBank_0067cc00 / _0067ea40 / _0067ec10 and WndNotification),
// the server's messages as Handle_OP_GuildBank @0x67e470 reads them.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "net/wire/app_packet.h"

#include "net/messages/guild_bank.h"

namespace hailstone::net {



std::optional<GuildBankList> decode_guild_bank_list(const AppPacket& p);
AppPacket encode(const GuildBankList& v); // server direction, for tests

std::optional<GuildBankMessage> decode_guild_bank(const AppPacket& p);
AppPacket encode(const GuildBankMessage& v); // server direction, for tests

AppPacket encode_guild_bank_deposit(std::uint32_t banker);
AppPacket encode_guild_bank_item(GuildBankAction action, std::uint32_t banker, std::int32_t key, std::uint32_t item_id,
                                 std::uint32_t value);
AppPacket encode_guild_bank_permissions(std::uint32_t banker, std::int32_t key, std::uint32_t item_id,
                                        GuildBankPermission permission, std::string_view member);

} // namespace hailstone::net
