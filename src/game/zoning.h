#pragma once

// Teleports and the respawn at bind as the original does them (the
// disassembly of Teleport_ResolveTeleportIndex
// @0x506630, its zone point lookup @0x513420, DoTeleportB @0x507ca0 and
// HandleWorldMessage's OP_ZonePlayerToBind case @0x4c8af4).
//
// Each function takes the game as hooks (what the original calls) and
// returns what it decided, so the client and the vector tests
// (Teleport_ResolveTeleportIndex, PlayerZoneClient_DoTeleport,
// SendTeleportIndexRequestAndWait, PlayerZoneClient_DoTeleportB,
// ZonePlayerToBind) run the same code.

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace hailstone::game {

// A row of the zone point table (0xdd02e0, OP_SendZonePoints; 0x20 bytes).
struct ZonePointRow {
    std::int32_t number = 0; // +0
    float x = 0, y = 0, z = 0, heading = 0; // +4.. (x is the server's y, as the player's +0x64)
    std::int32_t zone = 0;          // +0x14: zone | instance << 16
    std::int32_t index_request = 0; // +0x18: nonzero: ask the server for a teleport by index (0xcadf)
};

// PcClient::PrepareForRepop @0x586d80's coin rule: the carried coin (profile +0x33b8..) is cleared above level 5
// (150 when the PcBase virtual +0xc holds and the profile flag it tests is clear), at any level under PvP rule set 6
// (0xdcee70), or whenever that flag is set.
bool repop_clears_coin(std::int32_t pvp_rules, std::int32_t level, bool virtual_0c, bool flag);

// The text CheckRegionChange passes for an ATP region: "DRNTP00255%05d000000000000000" of atoi(name + 4),
// so the zone point number read back is 10 x the ATP number.
std::string atp_teleport_text(std::string_view region_name);

// Teleport_ResolveTeleportIndex(text, index, ...): the teleport an encoded text (or, with index >= 0, a zone
// point number) describes.
struct ResolvedTeleport {
    bool ok = false;
    float x = 0, y = 0, z = 0, heading = 0; // the destination (0 when a field is missing)
    std::int32_t found = 0;          // the zone out-parameter (0 when it failed)
    std::int32_t destination = 0;    // the destination out-parameter: the same zone (set only on success)
    bool same_zone = false;          // the same-zone out-flag: the zone is `current_zone` (set only on success)
    std::int32_t zone_point = -1;    // the zone point out-parameter: the number read or given, else -1
    std::int32_t index_request = 0;  // the index out-parameter: the row's +0x18
    bool camera_flag = false;        // EverQuest::Cameras[cameraType] +0x46 = 1 on success
};
// `location` is the player's position (+0x64..), which a few zone pairs move (34 <-> 10: y -/+ 1044; 3 -> 4:
// z + 2).
ResolvedTeleport resolve_teleport_index(std::string_view text, std::int32_t index, std::int32_t current_zone,
                                        float location[3], std::span<const ZonePointRow> zone_points);

// The quieting copy through the FPU of a float's bits (a signalling NaN comes out quiet).
std::uint32_t through_fpu(std::uint32_t bits);

// PlayerZoneClient::DoTeleport @0x508c10 for the local player (or another spawn: only the Eye of Zomm check).
struct DoTeleportHooks {
    std::function<ResolvedTeleport(std::int32_t index, bool& has_description)> resolve;
    // DoTeleportB(zone, x, y, z, heading bits through the FPU, the description, reason 0, -1, 0).
    std::function<bool(std::int32_t zone, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t heading)>
        teleport_b;
    std::function<void(std::int32_t zone_point)> send_index_request; // SendTeleportIndexRequestAndWait
    std::function<void(int kind, const float point[3])> position_sample; // PlayerClient_RecordPositionSample
    std::function<void()> free_description;                              // CXStr::FreeRep
    std::function<std::int32_t(std::int32_t spa)> get_spell_affect;      // GetPCSpellAffect (SPA 67)
    std::function<void(std::int32_t slot, std::int32_t arg)> remove_affect; // RemovePCAffectex
};
struct DoTeleportInput {
    bool is_local_player = true;
    std::int32_t index = -1;
    float point[3] = {0, 0, 0};       // the point that was tested (raw floats)
    std::uint8_t waiting_for_port = 0; // +0x1f05 (cleared here)
    bool virtual_6c = false, virtual_90 = false; // another spawn: both drop our Eye of Zomm
};
bool do_teleport(DoTeleportInput& in, const DoTeleportHooks& hooks);

// SendTeleportIndexRequestAndWait @0x506560: +0x1f05 = 1, opcode 0xcadf {u32 index} on channel 4 (6 bytes),
// then until the server's 0xcadf clears the flag: Sleep(100) and UdpManager_DeliverEvents(500).

// PlayerZoneClient::DoTeleportB @0x507ca0.
struct TeleportBPlayer {
    float x = 0, y = 0, z = 0, heading = 0; // +0x64, +0x68, +0x6c, +0x80
    float floor_height = 0;                 // +0x28
    std::int32_t colliding_type = 0;        // +0x24
    bool standing_on = false;               // +0x150 (a vehicle)
    bool touching_switch = false;           // +0x2d4
    bool waiting_for_port = false;          // +0x1f05
    std::uint8_t stand_state = 100;         // +0x35c (0x66 refuses)
    bool linkdead = false;                  // +0x3d0 (refuses)
    std::int32_t zone = 0;                  // +0x210
    std::int32_t race = 0;                  // virtual +0x88
};
struct TeleportBArgs {
    std::int32_t zone = 0;
    float x = 0, y = 0, z = 0, heading = 0; // kept when not below 999999 (heading: 513)
    std::int32_t reason = 0;                // 0 first sends an OP_ClientUpdate
    std::int32_t arg8 = -1, arg9 = 0;       // passed to MoveToZone
};
struct MoveToZoneCall {
    std::int32_t zone = 0;
    std::int32_t save_flag = 0;
    std::int32_t reason = 0;
    float x = 0, y = 0, z = 0;
    std::int32_t heading = 0; // __ftol2 of the heading
    std::int32_t arg8 = -1, arg9 = -1, arg10 = 0;
};
struct TeleportBHooks {
    std::function<void()> send_client_update;                  // reason 0: the current position, OP_ClientUpdate
    std::function<void(int kind)> position_sample;             // 2 before the update, 3 for a local move
    std::function<void()> save_physics;                        // PlayerZoneClient_SavePhysicsBeforePort
    std::function<void(float x, float y, float z)> move_object; // the +0x2cc object's virtual +4, when there is one
    // CDisplay::GetPlayerFloorHeight(x, y, z, flag) as the x87 holds it (a double, so a tail below the float's
    // last bit survives as at 53-bit precision).
    std::function<double(float x, float y, float z, int flag)> floor_height;
    std::function<void()> update_actor;        // SetCCreateCamera (UpdatePlayerActor)
    std::function<void()> find_person_request; // 0x59a9c0: Send_OP_FindPersonRequest while pinstPlayerPath has a target
    std::function<void()> rewind_reset;        // 0x5091e0
    std::function<void(const MoveToZoneCall&)> move_to_zone;
};
inline constexpr float kNoFloor = -9.99999988e26f; // 0x9c5760: GetPlayerFloorHeight's "none"
// `game_state` is CEverQuest +0x5c8 (1: always the local path). Returns DoTeleportB's al.
bool do_teleport_b(TeleportBPlayer& p, const TeleportBArgs& a, std::int32_t game_state, const TeleportBHooks& hooks);

// HandleWorldMessage's OP_ZonePlayerToBind case.
struct ZoneToBindPacket {
    std::uint32_t zone = 0; // zone | instance << 16
    float x = 0, y = 0, z = 0, heading = 0;
    std::string zone_name;
    std::uint8_t keep_items = 0;
    std::int32_t hp = 0, mana = 0, endurance = 0;
};
struct ZoneToBindHooks {
    std::function<void()> deactivate_respawn_window;  // only when there is one; +0x1f5 = +0x196
    std::function<void()> activate_main_ui;           // unk_ApplyWindowPositions (ActivateMainUI)
    std::function<void(std::uint32_t eqstr, const std::string& zone_name, std::uint32_t color)> chat; // 539, 13
    std::function<void()> prepare_for_repop;          // CEverQuest::PrepareLocalPCForRepop
    std::function<void(std::int32_t hp, std::int32_t mana, std::int32_t endurance)> set_vitals;
    std::function<void()> fill_air;                   // air supply = MaxAirSupply
    std::function<void()> update_spell_gems;          // CCastSpellWnd::UpdateSpellGems
    std::function<void()> stop_death_music;           // SetMusicSelection(8, 0), same zone
    std::function<void()> rewind_reset;               // same zone
    std::function<void()> send_position_list;         // 0x58ff50, same zone
    std::function<void(const MoveToZoneCall&)> move_to_zone; // another zone (reason 10, save flag 1)
    std::function<void()> local_view_setup;           // another zone
    std::function<void()> restore_camera;             // RestoreSavedCameraType
    std::function<void(std::uint8_t keep_items)> set_keep_items; // PcBase +0x2288: set, then cleared
    std::function<void()> finish;                     // +0x2dd4 = 1, +0x2f6c = 0, stats dirty
};
// `size` is the packet's payload size (20 or less is ignored); `current_zone` unk_CurrentZoneID.
void zone_player_to_bind(std::size_t size, const ZoneToBindPacket& p, std::uint32_t current_zone,
                         bool have_respawn_window, bool have_cast_spell_window, const ZoneToBindHooks& hooks);

} // namespace hailstone::game
