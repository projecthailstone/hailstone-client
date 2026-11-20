#pragma once

// The player's group and pets as the client knows them: the member list from
// OP_GroupUpdateB / OP_GroupUpdate / OP_GroupDisbandOther, the leader, a
// pending invitation, the group members' mana and endurance percentages; the
// pets (NMS OP_PetList, else the spawns whose owner is the player), the
// focused pet and its window state. Pure state with the original's rules;
// the packets are handled in net/connection/game_connection_group.cpp.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hailstone::game {

inline constexpr int kGroupSlots = 6;

struct GroupMember {
    std::string name;     // as the server sends it (a player's name, or a virtual player's clean name)
    std::uint32_t level = 0; // as sent (EQEmu sends fake levels in OP_GroupUpdateB; prefer the spawn's)
    bool merc = false;
    std::string owner_name;
    bool main_tank = false, main_assist = false, puller = false;
    bool offline = false;
};

struct GroupState {
    // CGroupBase::m_groupMembers: slots 0..5; empty name = free slot.
    std::array<GroupMember, kGroupSlots> slots{};
    std::string leader;
    bool active = false; // PcClient::Group != 0

    // A pending invitation (__Inviter, PlayerClient::SetInvited).
    std::string inviter;
    bool invite_via_first_opcode = false; // answered with OP_GroupFollow, else OP_GroupFollow2
    std::uint32_t invite_cookie = 0;

    // Mana / endurance percentages of other spawns (OP_MobManaUpdate,
    // OP_MobEnduranceUpdate), by spawn id.
    std::unordered_map<std::uint32_t, std::uint8_t> mana_percent, endurance_percent;

    int count() const;
    bool has_member(const std::string& name) const; // case-insensitive
    int slot_of(const std::string& name) const;     // -1 when absent
    bool is_leader(const std::string& name) const;
    // OP_GroupUpdate: adds the name in the first free slot unless it is already
    // there (HandleWorldMessage_Group). Returns true when it was added.
    bool add(const std::string& name, std::uint32_t level, bool merc, const std::string& owner);
    // OP_GroupDisbandOther: removes the name, and the leader with it when the leader leaves; `spawn_id` (0: not known)
    // drops that member's mana and endurance. The last member gone, there is no group (clear). Returns true when the
    // name was there.
    bool remove(const std::string& name, std::uint32_t spawn_id = 0);
    // OP_GroupDisbandYou / leaving the zone: no group.
    void clear();
};

// One of the player's pets.
struct PetInfo {
    std::uint32_t spawn_id = 0;
    std::uint32_t class_id = 0; // summoning class (NMS); 0 unknown or charmed
};

// The extended target list (ExtendedTargetList; OP_XTargetResponse): `max` slots, each with its type (client
// side: 1 auto hater, 2 / 3 a specific PC / NPC, ...), the server's status (0 empty, 1 a spawn, 2 a named
// absent one), the spawn and the name.
struct XTargetSlot {
    std::uint32_t type = 1;
    std::uint32_t status = 0;
    std::uint32_t spawn_id = 0;
    std::string name;
};
struct XTargetState {
    std::vector<XTargetSlot> slots; // resized to the response's max
    bool auto_haters = false;       // +0x14: auto add haters (Send_OP_XTargetAutoAddHaters @0x548850)
    int current = 0;                // +0x18: the slot TargetNextSlot (CYCLE_XTARGET) last targeted
    int selected = -1;              // +0x1c: the slot the window's menu acts on (0x548960)
    // This client: counts the zone-ins (GameConnection::reset_group_for_zone empties the list), so the window can
    // send its settings and restore its roles once per zone.
    std::uint32_t zone = 0;
};

struct PetState {
    // NMS: the server's pet list (OP_PetList); have_list false until one arrived.
    std::vector<PetInfo> list;
    bool have_list = false;
    // The pet the native pet window shows (the local player's PetID): the
    // latest spawn to arrive whose owner is the player. NMS focuses a pet by
    // despawning and respawning it (Mob::ConfigurePetWindow), so this follows
    // the server's focused pet.
    std::uint32_t focused = 0;
    std::uint32_t pet_target = 0; // OP_PetHoTT
    void clear() { *this = {}; }
};

// What a /pet word does to the pet window's toggle bytes (cmd_pet @0x4e2150).
enum class PetToggleAction : std::uint8_t {
    None,
    Flip,  // FlipButtonToggleState(index)
    On,    // SetButtonToggleState(index, 1)
    Off,   // SetButtonToggleState(index, 0)
    OnOff, // SetButtonToggleState(index, 1), then (index2, 0): follow and guard
};
// Parses the argument of /pet the way cmd_pet does (case-insensitive whole
// words; "attack NAME" / "kill NAME" with a name). Returns the PET_* command,
// or nullopt when the words are not a pet command. `named_target` is set to
// the name after an attack word.
struct ParsedPetCommand {
    std::uint32_t command = 0;
    bool uses_target = false; // attack / qattack: the current target goes in the packet
    bool leader_target = false; // leader: the target if it is an NPC
    std::string named_target;
    // The pet window's toggle byte the word writes first (command indexes 4..13, game/pet_buttons.h), whether or
    // not the command is then sent.
    PetToggleAction toggle = PetToggleAction::None;
    int toggle_index = 0;
    int toggle_index2 = 0;
};
std::optional<ParsedPetCommand> parse_pet_command(const std::string& args);

} // namespace hailstone::game
