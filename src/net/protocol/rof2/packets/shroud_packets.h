#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include "net/messages/shroud.h"
#include "net/wire/app_packet.h"

namespace hailstone::net {

std::optional<ShroudSelection> decode_shroud_selection(const AppPacket& p);
std::optional<ShroudStats> decode_shroud_stats(const AppPacket& p);
std::optional<std::vector<ShroudProgress>> decode_shroud_progress(const AppPacket& p);
std::optional<ShroudAbilityReply> decode_shroud_ability(const AppPacket& p);
std::optional<ShroudItemReply> decode_shroud_item(const AppPacket& p);
std::optional<ShroudTransition> decode_shroud_transition(const AppPacket& p, const PlayerProfile* previous = nullptr);

// Native sends at 0x41375f, 0x413842, 0x413912, 0x720951 and 0x720677 include
// the opcode in the send length. AppPacket stores that opcode separately.
AppPacket encode_shroud_request_stats(std::uint32_t selection_id);
AppPacket encode_shroud_select(std::uint32_t selection_id);
AppPacket encode_shroud_cancel();
AppPacket encode_shroud_request_ability(std::uint32_t selection_id, std::uint32_t rank_id);
AppPacket encode_shroud_request_item(std::uint32_t selection_id, std::uint32_t item_id);

std::optional<MonsterMissionCatalog> decode_monster_mission_catalog(const AppPacket& p);
std::optional<ShroudStats> decode_monster_mission_stats(const AppPacket& p);
std::optional<ShroudAbilityReply> decode_monster_mission_ability(const AppPacket& p);
std::optional<ShroudItemReply> decode_monster_mission_item(const AppPacket& p); // No selection ID on the wire.
std::optional<MonsterMissionAssignment> decode_monster_mission_assignment(const AppPacket& p);
std::optional<MonsterMissionResult> decode_monster_mission_result(const AppPacket& p);

AppPacket encode_monster_mission_catalog_request();
AppPacket encode_monster_mission_stats_request(std::uint32_t template_id);
AppPacket encode_monster_mission_ability_request(std::uint32_t template_id, std::uint32_t rank_id);
AppPacket encode_monster_mission_item_request(std::uint32_t template_id, std::uint32_t item_id);
AppPacket encode_monster_mission_select(std::uint32_t template_id);

std::optional<AltStorageControl> decode_alt_storage(const AppPacket& p);
std::optional<AltStorageList> decode_alt_storage_list(const AppPacket& p);
AppPacket encode_alt_storage_open(std::uint32_t entity_id);
AppPacket encode_alt_storage_withdraw(std::uint64_t key);
AppPacket encode_alt_storage_inspect(std::uint64_t key, std::uint32_t modifier = 0);

} // namespace hailstone::net
