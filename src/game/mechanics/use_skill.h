#pragma once

// CharacterZoneClient::UseSkill @0x45b2d0 (9935 bytes): the /doability path. It decides locally whether a skill
// may be used, starts its reuse timer, prints the original's messages and sends the skill's packet; the server
// decides the outcome. Worked from the disassembly and checked against the original's vectors
// (UseSkill_Combat / _Activity / _Target / _Gather; tests/use_skill_vectors_test.cpp), which list every
// callee in call order with its answer.
//
// The order, in short:
//  1. no player (+8) or Cur_HP(0, 1) below 1: nothing. A skill below 100 needs GetAdjustedSkill above 0 (forage
//     27 also passes with TotalEffect(SPA 372)). StandState 0x66: 12368 (colour 13). Mounted (EQPlayer::GetMount)
//     or GetPCSpellAffect(SPA 113): only skills 10, 16, 27, 30, 32, 40, 53, 55, 71 and 73, else 12393 (13).
//  2. 114 / 115 / 116: Send_OP_LDoNInspect / Send_OP_LDoNOpen / Cmd_reveal(0, 0), then on.
//  3. Slam (111) or SkillCombatType 2: the combat switch, DoAttack(100, skill, pinstTarget) (bash's weapon rule,
//     slam as bash with InnateSkill[11]); when it returns true: SetSkillCanUse(skill, 0) and SetSkillLastUsed(skill,
//     CombatSkillUsed[skill == 52]).
//  4. Other skills start the reuse timer (not sneak while sneaking, nor hide while PcBase::GetHide): IsAvailable
//     false ends it; the duration max(GetSkillReuseTime, 0), last used = CDisplay +0x154, can use 0; begging (67)
//     and pick pockets (48) start each other's. Then the skill's case (see the .cpp); a refusal gives the timer
//     back with SetSkillCanUse(skill, 1) except where noted there.
// Packets go through the shared send buffer: not connected (__gWorld null): DebugLog(opcode) unless 0xddf654;
// Send false: NetMgr_CloseConnection. Bytes the original never writes (feign death's payload 9..11, disarm's
// 13..15) are sent as zeros.
//
// The objects are plain structs (fields at the original's offsets, named); every callee the original calls is a
// Queries call in the original's order, named as the vectors name it.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game/mechanics/fpu.h"
#include "game/mechanics/abilities.h"

namespace hailstone::game::mechanics::useskill {

// The character objects UseSkill hands its callees as `this` or an argument.
enum class Obj : std::uint8_t {
    Null,
    Character,       // this CharacterZoneClient
    Pc,              // a PcClient: pinstLocalPC, or the CharacterBase's virtual +0x4c when not null
    PcCharacter,     // that PcClient's CharacterZoneClient (+0x2dc8)
    NullPcCharacter, // a null PcClient + 0x2dc8 (the virtual +0x4c returned null)
};

// An item reference, an affect or an animation object a query hands out (0: null).
using Handle = std::uintptr_t;

// The PlayerClient fields UseSkill reads and writes.
struct Spawn {
    std::uint8_t type = 0;              // +0x125: 0 player, 1 NPC, 2+ corpse
    std::uint32_t spawn_id = 0;         // +0x148
    std::uint8_t stand_state = 100;     // +0x35c: 0x64 standing, 0x66 frozen
    bool has_mount = false;             // +0x154 Mount (EQPlayer::GetMount)
    std::uint8_t sneak = 0;             // +0x264
    std::int32_t hide_mode = 0;         // +0x338 HideMode
    std::uint8_t pvp = 0;               // +0x349 PvPFlag
    std::uint8_t body_wet = 0;          // +0xa2
    std::uint8_t mercenary = 0;         // +0x208
    std::uint8_t fishing_event = 0;     // +0x3fd
    std::int32_t fishing_eta = 0;       // +0x260
    std::array<std::int32_t, 2> combat_skill_used{}; // +0x390 CombatSkillUsed (1: tiger claw)
    std::int32_t next_intimidate_time = 0; // +0x3c8
    std::uint8_t intimidate_count = 0;  // +0x434
    std::uint32_t y = 0, x = 0, z = 0;  // +0x64 / +0x68 / +0x6c (float bits)
    std::uint32_t speed_y = 0, speed_x = 0, speed_z = 0; // +0x70 / +0x74 / +0x78 (float bits)
    std::uint32_t heading = 0;          // +0x80 (float bits)
    std::string name;                   // +0xa4 Name
    std::string displayed_name;         // +0xe4 DisplayedName
};

// The state UseSkill reads and the globals it writes.
struct State {
    Spawn* me = nullptr;                // the character's player (+8)
    Spawn* target = nullptr;            // pinstTarget
    Spawn* local_player = nullptr;      // pinstLocalPlayer
    Spawn* controlled_player = nullptr; // pinstControlledPlayer
    // the character
    std::uint8_t status = 0;            // CharacterBase +0xfc (1: no pick pockets)
    std::uint8_t stand_state = 100;     // CharacterBase +0x104 (forage, fishing want 0x64)
    // the current profile (read after ProfileManager::GetCurrentProfile)
    std::int32_t skill_disarm = 0;      // Skill[16] +0x30e0
    std::int32_t skill_pick_pockets = 0; // Skill[48] +0x3160
    std::int32_t skill_begging = 0;     // Skill[67] +0x31ac
    std::int32_t innate_slam = 0xff;    // InnateSkill[11] +0x325c (0xff: none)
    // globals
    bool connected = true;              // __gWorld
    bool quiet_log = false;             // 0xddf654
    bool attack = false;                // __Attack 0xe635aa
    bool autofire = false;              // __Autofire 0xe635ab
    std::int32_t item_pending = 0;      // EverQuestinfo IsItemPending 0xe635ac (written)
    std::int32_t currently_interacting = 0; // 0xe635b0 (written)
    std::int32_t ruleset = 0;           // 0xdcee70 (4: the deity rule)
    std::int32_t activity = 0;          // 0xddfccc: 0x1d hiding, 9 binding, 0x37 fishing
    std::int32_t feign_value = 0;       // 0xddf708 (sent with feign death)
    std::int32_t display_time = 0;      // CDisplay +0x154
    bool tracking_window_visible = false; // CTrackingWnd +0x196
    bool compass_window = false;        // pinstCCompassWnd
    std::array<std::int32_t, 3> hide_position{}; // 0xddf8d8: y, x, z truncated (written)
    std::int32_t send_count = 0;        // 0xf99584, one less per send (written)
    FpuPrecision precision = FpuPrecision::Single24;
};

// Every callee UseSkill calls, in the original's terms (the vectors' names).
class Queries {
public:
    virtual ~Queries() = default;
    virtual std::int32_t cur_hp(std::int32_t a0, std::int32_t a1) = 0;            // CharacterZoneClient::Cur_HP
    virtual std::int32_t get_adjusted_skill(std::int32_t skill) = 0;
    virtual std::int32_t total_effect(std::int32_t spa, std::uint8_t a1, std::int32_t a2, std::uint8_t a3,
                                      std::uint8_t a4) = 0;
    virtual std::string get_string(std::uint32_t id, std::int32_t a1) = 0;         // StringTable::getString
    virtual void chat(const std::string& text, std::int32_t color, std::uint8_t a2, std::uint8_t a3) = 0; // dsp_chat
    // GetPCSpellAffect(spa, slot, other) on `self`; writes *slot (-1 none) when given.
    virtual Handle get_pc_spell_affect(Obj self, std::int32_t spa, std::int32_t* slot, bool other) = 0;
    virtual std::int32_t get_skill_combat_type(std::uint32_t skill) = 0;           // CSkillMgr_GetSkillCombatType
    virtual bool is_available(std::uint32_t skill) = 0;                            // CSkillMgr::IsAvailable
    virtual std::int32_t get_skill_reuse_time(std::uint32_t skill) = 0;
    virtual void set_skill_timer_duration(std::uint32_t skill, std::int32_t ms) = 0;
    virtual void set_skill_last_used(std::uint32_t skill, std::int32_t time) = 0;
    virtual void set_skill_can_use(std::uint32_t skill, std::int32_t can_use) = 0;
    // UdpConnection::Send(4, buffer, 2 + payload): the opcode (CPacketScrambler::hton) and the payload.
    virtual bool send(std::int32_t channel, const SkillSend& action) = 0;
    virtual void close_connection() = 0;                                           // NetMgr_CloseConnection
    virtual void debug_log_void_connection(std::uint32_t opcode) = 0;              // DebugLog_Printf
    virtual void send_ldon_inspect(std::int32_t a0, std::int32_t a1) = 0;
    virtual void send_ldon_open(std::int32_t a0, std::int32_t a1) = 0;
    virtual void cmd_reveal(std::int32_t a0, std::int32_t a1) = 0;
    virtual bool pc_get_hide(Obj pc) = 0;                                          // PcBase_GetHide
    virtual void pc_set_hide(Obj pc, std::uint8_t hide) = 0;                       // PcBase_SetHide
    virtual void pc_set_sneak(Obj pc, std::uint8_t sneak) = 0;                     // PcBase_SetSneak
    virtual void get_current_profile() = 0;                                        // ProfileManager::GetCurrentProfile
    virtual Handle get_item(std::int32_t slot) = 0;                                // unk_Container_GetItem
    virtual void item_ref_release() = 0;                                           // VePointer::~VePointer
    virtual bool can_use_item(Handle item, std::int32_t a1, std::int32_t a2) = 0;  // CharacterZoneClient_CanUseItem
    virtual std::int32_t get_item_type(Handle item) = 0;                           // ItemClient::GetItemType
    virtual std::int32_t get_item_class(Handle item) = 0;                          // ItemBase_GetType
    virtual std::int32_t get_item_id(Handle item) = 0;                             // ItemBase_GetID
    virtual bool do_attack(Spawn& self, std::int32_t slot, std::uint32_t skill, Spawn* target) = 0;
    virtual std::string trim_name(const std::string& name) = 0;                    // CEverQuest::trimName
    virtual std::string build_token_string(std::uint32_t id, const std::string& p1, std::int32_t p2) = 0;
    virtual void change_position(Spawn& spawn, std::uint8_t stand_state) = 0;      // PlayerZoneClient::ChangePosition
    virtual void set_auto_attack(std::uint8_t on) = 0;
    virtual void set_auto_range_attack(std::uint8_t on) = 0;
    virtual void update_my_visible_status(Obj self, std::uint8_t a0) = 0;         // EQ_Character
    virtual void set_activity(std::int32_t kind, std::int32_t spawn) = 0;          // Send_OP_TrackTarget_004ed870
    virtual std::int32_t rand_nbr() = 0;                                           // Random
    virtual std::int32_t random_low_high(std::int32_t low, std::int32_t high) = 0; // RandomNumberLowHigh
    virtual void compass_reset() = 0;                                              // 0x65b650
    virtual void send_track() = 0;                                                 // Send_OP_Track
    virtual std::int32_t not_on_same_deity(Spawn& self, Spawn& other, std::int32_t a1) = 0;
    virtual std::int32_t get_level(Spawn& spawn) = 0;                              // PlayerZoneClient::GetLevel (al)
    virtual void push_pending_reason(std::int32_t reason) = 0;
    virtual void push_interact_reason(std::int32_t reason) = 0;
    virtual void make_me_visible(Obj self, std::int32_t a0, std::uint8_t a1) = 0;
    virtual bool can_i_hit(Spawn& self, Spawn* target, std::uint32_t extra_bits) = 0; // PlayerZoneClient::CanIHit
    virtual void remove_pc_affect(Obj self, Handle affect, std::int32_t a1) = 0;  // RemovePCAffectex
    virtual void notify_pc_affect_change(Obj self, std::int32_t slot, std::uint8_t a1) = 0;
    virtual void player_per_frame_update(Spawn* spawn) = 0;                        // unk_PlayerClient_PerFrameUpdate
    // token_string_to_client(character or null, id, color, a3).
    virtual void token_message(Obj character, std::int32_t id, std::int32_t color, std::int32_t a3) = 0;
    virtual bool find_item_by_item_type(std::int32_t item_type, std::int32_t* slot, std::int32_t* bag_slot) = 0;
    virtual std::string get_id_file(Handle item, std::int32_t a0) = 0;            // ItemBase_GetIDFile
    virtual void update_item_slot(Spawn& self, std::uint8_t slot, const std::string& id_file, std::int32_t a2,
                                  std::int32_t a3) = 0;                            // PlayerClient (SetHeldModel)
    virtual Handle get_animation(Spawn& self) = 0;                                 // PlayerClient_GetAnimation
    virtual void animation_vf0c(Handle animation, std::int32_t a0, std::int32_t a1, std::int32_t a2,
                                std::uint32_t a3_bits, std::int32_t a4, std::int32_t a5, std::int32_t a6) = 0;
    virtual std::int32_t get_time() = 0;                                           // __EQGetTime
    virtual void chat_template(Obj character, std::uint32_t id, std::int32_t color, const std::string& p1,
                               std::int32_t p2) = 0;                               // dsp_chat_EqStrTemplate
    // Virtuals.
    virtual std::int32_t get_base_skill(std::int32_t skill, std::int32_t flag) = 0; // CharacterZoneClient +0x18
    virtual bool get_pc_client() = 0;                    // CharacterBase +0x4c: the PcClient is not null
    virtual bool is_interactive_object(Spawn& spawn) = 0; // PlayerClient +0x98
    virtual bool is_vehicle_race(Spawn& spawn) = 0;       // +0x50
    virtual bool is_gm(Spawn& spawn) = 0;                 // +0xdc
    virtual void set_hide_mode(Spawn& spawn, std::int32_t mode) = 0; // +0xd8 PlayerClient::SetHideMode (stores +0x338)
    virtual void tracking_window_show(std::uint8_t show, std::uint8_t bring_to_top, std::uint8_t update_layout) = 0;
    // Float-returning callees (bits): CDisplay::PlayerDistance(a, b, extra), __get_melee_range(a, b).
    virtual std::uint32_t player_distance(Spawn& a, Spawn& b, std::uint32_t extra_bits) = 0;
    virtual std::uint32_t get_melee_range(Spawn& a, Spawn& b) = 0;
};

// CharacterZoneClient::UseSkill(skill, ignored): the skill id is a dword (the original compares its low byte and
// hands the whole value to DoAttack).
void use_skill(State& s, std::uint32_t skill, Queries& q);

} // namespace hailstone::game::mechanics::useskill
