#pragma once

// The extended target list's client rules (ExtendedTargetList @0x548820..0x5492a0 on PcClient +0x31b8, the slot
// state is game/group.h's XTargetState) and the extended target window's role strings and role sets
// (CExtendedTargetWnd_BuildRoleSetString @0x54a050, CExtendedTargetWnd_ApplyRoleSetString @0x54a990, the ten
// "TargetSet" role sets at 0xe5f834 that ReadCharacterIniSettings / SaveExtendedTargetRoleSetsToIni keep in the
// character ini's [ExternalTargetRoles]). Follows the original's CExtendedTargetWnd and
// XTarget_* functions; addresses at each function.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "game/group.h"

namespace hailstone::game {

// The slot types, as the protocol carries them (EQEmu XTargetType, zone/client.h; the SDK's HS_BULK_XTARGET_*).
// Unscoped over std::uint32_t, the type XTargetSlot::type and the packets hold, so a slot's type compares with them
// as it is.
enum XTargetType : std::uint32_t {
    kXTargetEmpty = 0,
    kXTargetAutoHater = 1,
    kXTargetSpecificPc = 2,
    kXTargetSpecificNpc = 3,
    kXTargetTargetsTarget = 4,
    kXTargetGroupTank = 5,
    kXTargetGroupTanksTarget = 6,
    kXTargetGroupAssist = 7,
    kXTargetGroupAssistTarget = 8,
    kXTargetGroupPuller = 9,
    kXTargetGroupPullerTarget = 10,
    kXTargetGroupMark1 = 11,
    kXTargetGroupMark2 = 12,
    kXTargetGroupMark3 = 13,
    kXTargetRaidAssist1 = 14,
    kXTargetRaidAssist2 = 15,
    kXTargetRaidAssist3 = 16,
    kXTargetRaidAssist1Target = 17,
    kXTargetRaidAssist2Target = 18,
    kXTargetRaidAssist3Target = 19,
    kXTargetRaidMark1 = 20,
    kXTargetRaidMark2 = 21,
    kXTargetRaidMark3 = 22,
    kXTargetMyPet = 23,
    kXTargetMyPetTarget = 24,
    kXTargetMyMercenary = 25,
    kXTargetMyMercenaryTarget = 26,
    kXTargetTypes = 27, // the count, not a type
};
inline constexpr int kXTargetMaxSlots = 20; // the window's 20 rows (CExtendedTargetWnd's ctor), EQEmu's hard cap

// __GetXTargetType @0x8d3be0: the type's name ("Auto Hater", "Group Tank", ...); null past 26.
const char* xtarget_type_name(std::uint32_t type);
// XTarget_ParseTargetType @0x4e3780: a type from its name or short name ("GroupTank", "GT"; any case). There is no
// name for 3: "CurrentTarget" / "CT" is 2, which /xtarget set turns into 2 or 3 by the target.
std::optional<std::uint32_t> parse_xtarget_type(std::string_view name);
// CExtendedTargetWnd_UpdateAutoHateIcon @0x549a20: the A_TargetRoleIcon cell a slot of that type shows, -1 none
// (the role button is hidden).
int xtarget_role_icon_cell(std::uint32_t type);

// ExtendedTargetList_FindFirstAutoHaterSlot @0x548d70: the first auto hater slot, -1 none. `skip` (this client)
// passes slots by (GameConnection::xtarget_slot_hidden).
int xtarget_first_auto_hater_slot(const XTargetState& list, const std::function<bool(int slot)>& skip = {});
// ExtendedTargetList_FindLastSpecificTargetSlot @0x548c70: the last slot of type 2 or 3, -1 none.
int xtarget_last_specific_slot(const XTargetState& list);
// ExtendedTargetList_FindSpecificTargetSlotBySpawnID @0x548cf0: the first slot of type 2 or 3 holding that spawn.
int xtarget_specific_slot_by_spawn(const XTargetState& list, std::uint32_t spawn_id);

// Send_OP_XTargetRequest's slot change, applied to the client's list: for a slot in range whose type or name
// (compared case-insensitively over 0x40 characters) differs, the slot takes the type and name (at most 63
// characters) with its status and spawn cleared, and true is returned (the request {1, slot, type, name} is due).
bool xtarget_set_slot(XTargetState& list, int slot, std::uint32_t type, std::string_view name);

// The request a role string or a command makes: Send_OP_XTargetRequest(slot, type, name).
using XTargetSend = std::function<void(int slot, std::uint32_t type, const std::string& name)>;

// CExtendedTargetWnd_BuildRoleSetString @0x54a050: "<flag>^<name>^<count>" then "^<slot>^<type>" for each slot that
// is not an auto hater, with "^<name>" after a specific PC (type 2). The window's slot count is the list's.
std::string xtarget_role_string(const XTargetState& list, std::string_view name, int flag);
// CExtendedTargetWnd_ApplyRoleSetString @0x54a990: with flag 1, the name (at most 23 characters) to `name` and each
// listed slot's request (a specific NPC, type 3, is not restored), then auto hater for each of the list's
// `slot_count` slots the string does not list. False (nothing sent) for any other flag.
bool xtarget_apply_role_string(std::string_view text, std::size_t slot_count, std::string* name,
                               const XTargetSend& send);
// 0x54a910: the name of a flag-1 role string (at most 23 characters), else empty.
std::string xtarget_role_string_name(std::string_view text);

// The ten role sets (0xe5f834, 0x5f7 bytes each: the name +0 (0x19), the role string +0x19 (0x5dc), used +0x5f5,
// to-save +0x5f6).
struct XTargetRoleSet {
    std::string name;
    std::string roles;
    bool used = false;
    bool dirty = false;
};
struct XTargetRoleSets {
    static constexpr int kCount = 10;
    std::array<XTargetRoleSet, kCount> sets;

    // ReadCharacterIniSettings @0x546ea0: TargetSet1..10 of [ExternalTargetRoles]; an empty one is unused (name and
    // roles cleared), else used with its roles (at most 1499 characters) and its name from them; none to save.
    void load(const std::function<std::string(const std::string& key)>& get);
    // SaveExtendedTargetRoleSetsToIni @0x546410: each set marked to save, as its roles (empty when unused).
    void save(const std::function<void(const std::string& key, const std::string& value)>& put) const;
    // 0x519a80: sorted (used first, by name without case: 0x519920), all marked to save, then saved.
    void sort_and_save(const std::function<void(const std::string& key, const std::string& value)>& put);
    // The role set a name saves to (CExtendedTargetWnd::WndNotification 0x26): the used set of that name (any
    // case), else the first unused one; -1 when all ten are used.
    int slot_for_name(std::string_view name) const;
};

} // namespace hailstone::game
