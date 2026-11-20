#pragma once

// Typed RoF2 zone-server packets: the zone-in handshake, the player profile,
// spawns, movement, chat, zoning and camping. Layouts follow the NMS server's
// EQEmu RoF2 translator (common/patches/rof2.cpp ENCODE/DECODE, rof2_structs.h)
// and, for pass-through opcodes, common/eq_packet_structs.h; per-packet notes
// are in docs/protocol/rof2-packets.md.
//
// Positions are EQ units: x/y/z in world units, heading 0..512 (a full turn),
// deltas in units per tick as the server stores them. The wire uses fixed
// point: coords ×8 (19-bit), deltas ×64 (13-bit), heading ×4 (12-bit),
// delta heading ×20 (10-bit) (common/misc_functions.cpp FloatToEQ*).
//
// Handshake packets without payload (OP_ReqClientSpawn, OP_SendExpZonein,
// OP_WorldObjectsSent, OP_ClientReady, OP_SendAATable, OP_UpdateAA,
// OP_Logout, OP_WorldClientReady) are built with make_empty() from
// net/protocol/rof2/wire_io.h.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/banner_appearance.h"
#include "net/wire/app_packet.h"

#include "net/messages/zone.h"
#include "net/messages/guild_banner.h"

namespace hailstone::net {



// ---------------------------------------------------------------- client → zone

inline constexpr std::size_t kZoneEntryRequestSize = 76;
AppPacket encode(const ZoneEntryRequest& v);
std::optional<ZoneEntryRequest> decode_zone_entry_request(const AppPacket& p);

inline constexpr std::size_t kClientPositionUpdateSize = 46;
AppPacket encode(const ClientPositionUpdate& v);
std::optional<ClientPositionUpdate> decode_client_position_update(const AppPacket& p);

// OP_Camp: u32 kind (0 = character select, 1 = desktop, 2 = server); EQEmu
// ignores the payload and starts its 29 s camp timer.
AppPacket encode_camp(std::uint32_t kind);

inline constexpr std::size_t kSaveRequestSize = 484;
AppPacket encode_save(const SaveRequest& v);         // OP_Save
AppPacket encode_save_on_zone(const SaveRequest& v); // OP_SaveOnZoneReq
std::optional<SaveRequest> decode_save(const AppPacket& p);

// ---------------------------------------------------------------- zone → client

// Spawns arrive one per packet with opcode OP_ZoneEntry (the server sends
// OP_ZoneSpawns/OP_NewSpawn as one OP_ZoneEntry each, rof2.cpp
// ENCODE(OP_ZoneSpawns)); OP_NewSpawn is accepted too. The first one after
// the client's OP_ZoneEntry is the player's own spawn.
// True for the races that use the long equipment block (rof2.cpp and the
// client's PlayerZoneClient::LegalPlayerRace: < 13, 128, 130, 330, 522).
bool is_pc_model_race(std::uint32_t race);
AppPacket encode(const Spawn& v); // server direction (OP_ZoneEntry), for tests
std::optional<Spawn> decode_spawn(const AppPacket& p);
// Some servers send variable-length spawn records in a single zone-spawns
// application payload. EQEmu's RoF2 translator normally emits one ZoneEntry
// application packet per spawn instead; both layouts use the same record parser.
std::optional<std::vector<Spawn>> decode_spawns(const AppPacket& p);

// OP_MobRename, 200 bytes: old name twice (64 + 64), new name (64),
// two server-side control dwords (normally 0 and 1).
inline constexpr std::size_t kMobRenameSize = 200;
std::optional<MobRename> decode_mob_rename(const AppPacket& p);

std::optional<GuildBannerAppearance> decode_guild_banner_appearance(const AppPacket& p);

// OP_ClientUpdate, zone → client (24 bytes, PlayerPositionUpdateServer_Struct,
// passed through untranslated).
inline constexpr std::size_t kSpawnPositionUpdateSize = 24;
AppPacket encode(const SpawnPositionUpdate& v); // server direction, for tests
std::optional<SpawnPositionUpdate> decode_spawn_position_update(const AppPacket& p);
// OP_NPCMoveUpdate, zone → client: the same update as a bit stream
// (Handle_OP_NPCMoveUpdate, game/mechanics/remote_spawns.h); any length.
std::optional<SpawnPositionUpdate> decode_npc_move_update(const AppPacket& p);

// OP_PlayerProfile (variable, ~25 KB): rof2.cpp ENCODE(OP_PlayerProfile)
// writes it field by field with count-prefixed arrays.
// One weapon or potion of a bandolier set / the potion belt (BandolierItem_Struct:
// the name, then the item id and icon; id 0 = empty).
std::optional<PlayerProfile> decode_player_profile(const AppPacket& p);


// OP_NewZone (948 bytes, NewZone_Struct).
inline constexpr std::size_t kNewZoneSize = 948;
AppPacket encode(const NewZone& v); // server direction, for tests
std::optional<NewZone> decode_new_zone(const AppPacket& p);

// OP_TimeOfDay (8 bytes).
AppPacket encode(const TimeOfDay& v);
std::optional<TimeOfDay> decode_time_of_day(const AppPacket& p);
// EQWorldData::AdvanceTime @0x7dbff0: the game date `elapsed_s` whole real seconds (Util::FastTime's) after `ref`
// came. Twenty game seconds a real second (one game minute per 3 s), the 32-bit product wrapping as the original's; the whole
// minutes, hours, days, months and years are added to `ref` with its byte arithmetic and carries (hours 1..24, days
// 1..28, months 1..12), and a year past 9999 becomes 0.

AppPacket encode(const Weather& v);
std::optional<Weather> decode_weather(const AppPacket& p);

// OP_DeleteSpawn: the server's (5 bytes: u32 id, u8 1) and the client's own (4 bytes: its spawn id; MoveToZone
// @0x541f0a and DoMainLoop's camp exit send PlayerClient +0x148), which EQEmu answers by dropping the link.
AppPacket encode_delete_spawn(std::uint32_t spawn_id);
AppPacket encode_delete_own_spawn(std::uint32_t spawn_id);
std::optional<std::uint32_t> decode_delete_spawn(const AppPacket& p);

AppPacket encode(const SpawnAppearance& v);
std::optional<SpawnAppearance> decode_spawn_appearance(const AppPacket& p);

std::optional<ChangeSize> decode_change_size(const AppPacket& p);

AppPacket encode(const HpUpdate& v); // server direction, for tests
std::optional<HpUpdate> decode_hp_update(const AppPacket& p);

std::optional<MobHealth> decode_mob_health(const AppPacket& p);

AppPacket encode(const ChannelMessage& v);
// Opcodes 0x150e / 0x1f65 (not in EQEmu's table): the player's name; set / clear the chat block.
inline constexpr std::uint16_t kOpChatBlockOn = 0x150e, kOpChatBlockOff = 0x1f65;
std::optional<ChannelMessage> decode_channel_message(const AppPacket& p);

std::optional<SpecialMessage> decode_special_message(const AppPacket& p);

std::optional<FormattedMessage> decode_formatted_message(const AppPacket& p);

AppPacket encode(const SimpleMessage& v); // server direction, for tests
std::optional<SimpleMessage> decode_simple_message(const AppPacket& p);

AppPacket encode(const ZoneChange& v);
std::optional<ZoneChange> decode_zone_change(const AppPacket& p);

std::optional<RequestClientZoneChange> decode_request_client_zone_change(const AppPacket& p);

std::optional<std::vector<ZonePoint>> decode_zone_points(const AppPacket& p);

// ---------------------------------------------------------------- combat

// OP_TargetMouse / OP_TargetCommand (4 bytes, ClientTarget_Struct, both
// passed through): u32 target spawn id, 0 = no target. The original sends
// OP_TargetMouse whenever its target changes (CharacterZoneClient::
// DoPassageOfTime @0x45daf0); the server echoes OP_TargetCommand when it
// accepts a /target and sends it for pets.
AppPacket encode_target(bool command, std::uint32_t spawn_id);
std::optional<std::uint32_t> decode_target(const AppPacket& p);

// OP_AutoAttack and OP_AutoAttack2 (u32 each: 1 on, 0 off). The original
// sends both whenever __Attack changes; EQEmu reads byte 0 of OP_AutoAttack
// and ignores OP_AutoAttack2.
AppPacket encode_auto_attack(bool second, bool on);
std::optional<bool> decode_auto_attack(const AppPacket& p);
// OP_AutoFire (u8: 1 on, 0 off): DoPassageOfTime @0x45daf0 sends it whenever __Autofire differs from the last sent
// value. EQEmu then shoots from the range slot on its own timer (Client::Process, RangedAttack / ThrowingAttack).
AppPacket encode_auto_fire(bool on);

inline constexpr std::size_t kConsiderSize = 20;
AppPacket encode(const Consider& v);
std::optional<Consider> decode_consider(const AppPacket& p);

inline constexpr std::size_t kCombatDamageSize = 30;
AppPacket encode(const CombatDamage& v); // also the client's own hit report (ApplyDamage)
std::optional<CombatDamage> decode_combat_damage(const AppPacket& p);

inline constexpr std::size_t kDeathSize = 32;
AppPacket encode(const Death& v); // server direction, for tests
std::optional<Death> decode_death(const AppPacket& p);

AppPacket encode(const ManaChange& v); // server direction, for tests
std::optional<ManaChange> decode_mana_change(const AppPacket& p);

AppPacket encode_points_update(bool endurance, const PointsUpdate& v); // server direction, for tests
std::optional<PointsUpdate> decode_points_update(const AppPacket& p);

AppPacket encode(const ExpUpdate& v); // server direction, for tests
std::optional<ExpUpdate> decode_exp_update(const AppPacket& p);

AppPacket encode(const LevelUpdate& v); // server direction, for tests
std::optional<LevelUpdate> decode_level_update(const AppPacket& p);

AppPacket encode(const Animation& v);
std::optional<Animation> decode_animation(const AppPacket& p);

AppPacket encode(const ZonePlayerToBind& v); // server direction, for tests
std::optional<ZonePlayerToBind> decode_zone_player_to_bind(const AppPacket& p);

std::optional<RespawnWindow> decode_respawn_window(const AppPacket& p);
AppPacket encode_respawn_selection(std::uint32_t option_id);


std::optional<WearChange> decode_wear_change(const AppPacket& p);
AppPacket encode(const WearChange& v);

std::optional<Illusion> decode_illusion(const AppPacket& p);

// TODO: OP_SpawnDoor, OP_GroundSpawn, OP_Action, OP_BeginCast. Items,
// inventory and loot are in net/protocol/rof2/packets/item_packets.h.

} // namespace hailstone::net
