#pragma once

// The item preview (CObjectPreviewWnd, the 3D view an Alt + right click on an item opens): which items it shows, and
// on whom. Read from the disassembly (no vectors):
//  - CInvSlot::HandleRButtonUp @0x697250 with Alt (keyboard flags & 0xc), the item display's Alt + right click, the
//    merchant's preview, the guild bank's Alt + right click on a row (0x67cb80) ... send OP_ItemPreviewRequest
//    (Send_OP_ItemPreviewRequest @0x6fff10 with an item, @0x6ffc20 with an item id: net::encode_item_preview_request)
//    and show the window (vtable +0xd8 (1, 1, 1)); a request without an item only hides the view (0x6ff980(0, 1)).
//  - The reply (HandleWorldMessage case 0x7f80) is the serialized item and one byte (EQEmu sends none: 0): the view
//    shows (item_preview_show) and 0x577700 puts the item on a copy of the local player (preview_appearance)
//    or, for an item worn nowhere visible, shows the item's own model (0x577440).

#include <cstdint>
#include <string_view>

#include "game/world_state.h"
#include "net/messages/item.h"

namespace hailstone::game {

// ItemBase_IsWornInVisibleSlot @0x7b2280: GetEquipSlots(1) has a visible slot (head 2, arms 7, wrist 10, range 11,
// hands 12, primary 13, secondary 14, chest 17, legs 18, feet 19: the masks 0xe7c00 and 0x84).
bool worn_in_visible_slot(const net::Item& item);
// 0x4dcbe0: the material slot of a possessions slot (head 2 -> 0, chest 17 -> 1, arms 7 -> 2, wrist 10 -> 3,
// hands 12 -> 4, legs 18 -> 5, feet 19 -> 6, primary 13 -> 7, secondary 14 -> 8), else -1.
int preview_material_slot(int possessions_slot);
// "IT63" -> 63 (an IDFile's model number), 0 when it names none.
std::uint32_t item_model_number(std::string_view id_file);

struct ItemPreviewShow {
    bool show = false;      // the view and its buttons (else the "No item preview available." label)
    bool on_player = false; // 0x577700: on the player (worn visible, a house pet, or the byte set), else alone
};
// The reply's choice: show when the item is worn in a visible slot, or its IDFile is neither IT63 nor IT64, or the
// trailing byte is set; on the player when worn visible or the byte is set (house pets, ItemBase_IsHousePet, are not
// in EQEmu's serialization).
ItemPreviewShow item_preview_show(const net::Item& item, std::uint8_t trailing);

// 0x5769a0's copy of the player wearing `item` (`primary` / `secondary`: what the player holds now): the first
// visible slot of the item's equip slots, 2..19, that has a material slot takes it: a primary goes in the right hand
// (the left keeps the secondary unless the item is two-handed: item types 1, 4 and 35), a secondary in the left (the
// right keeps the primary unless that one is two-handed), anything else is worn there (PcClient_RefreshWornSlotVisual:
// its material, tint and hero's forge model); an item with no such slot but the range slot (0x800) is held in the
// left hand alone.
Appearance preview_appearance(Appearance a, const net::Item& item, const net::Item* primary, const net::Item* secondary);

} // namespace hailstone::game
