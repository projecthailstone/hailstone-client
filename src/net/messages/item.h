#pragma once

// Protocol-independent gameplay values extracted from the RoF2 packet declarations.
// Application codecs translate these values to their own wire layouts.
// Serial lengths, packet opcodes and binary read/write remain in protocol/rof2/.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hailstone::net {

// InventorySlot_Struct (12 bytes): the client's ItemGlobalIndex copied as is:
// i16 type (ItemContainerInstance),
// i16 unknown, i16 slot, i16 bag sub-slot (-1 = none), i16 augment (-1),
// i16 unknown.
struct ItemSlot {
    std::int16_t type = 0;  // kInv* below
    std::int16_t slot = -1;
    std::int16_t sub = -1;  // index inside the bag in `slot`, -1 for the slot itself
    std::int16_t aug = -1;
    std::int16_t unknown02 = 0;
    std::int16_t unknown01 = 0;

    bool operator==(const ItemSlot&) const = default;

    static constexpr ItemSlot possessions(std::int16_t slot, std::int16_t sub = -1) { return {0, slot, sub, -1, 0, 0}; }
    static constexpr ItemSlot corpse(std::int16_t slot) { return {11, slot, -1, -1, 0, 0}; }
    // The "nowhere" slot the client sends to destroy an item: Location -1 as
    // an int32 (so the unknown word is -1 too) and every slot -1
    // (PcZoneClient::DestroyHeldItemOrMoney @005862f0).
    static constexpr ItemSlot invalid() { return {-1, -1, -1, -1, -1, 0}; }

    bool valid() const { return type >= 0 && type < 0x18 && slot != -1; } // ItemGlobalIndex_IsValid @00441a00
    bool top_level() const { return sub == -1; }
    ItemSlot parent() const { return {type, slot, -1, -1, 0, 0}; }
    // Same location (the two unknown words are ignored).
    bool same(const ItemSlot& o) const { return type == o.type && slot == o.slot && sub == o.sub && aug == o.aug; }
};

// ItemContainerInstance values (rof2_limits.h invtype).
inline constexpr std::int16_t kInvPossessions = 0, kInvBank = 1, kInvSharedBank = 2, kInvTrade = 3, kInvWorld = 4,
                              kInvLimbo = 5, kInvTribute = 6, kInvGuildTribute = 8,
                              kInvMerchant = 9, kInvCorpse = 11, kInvRealEstate = 14,
                              kInvAltStorage = 19, kInvArchived = 20, kInvMail = 21;

// Possessions slot numbers (rof2_limits.h invslot): 0..22 equipment,
// 23..32 general inventory, 33 cursor.
inline constexpr std::int16_t kSlotCharm = 0, kSlotEar1 = 1, kSlotHead = 2, kSlotFace = 3, kSlotEar2 = 4, kSlotNeck = 5,
                              kSlotShoulders = 6, kSlotArms = 7, kSlotBack = 8, kSlotWrist1 = 9, kSlotWrist2 = 10,
                              kSlotRange = 11, kSlotHands = 12, kSlotPrimary = 13, kSlotSecondary = 14,
                              kSlotFinger1 = 15, kSlotFinger2 = 16, kSlotChest = 17, kSlotLegs = 18, kSlotFeet = 19,
                              kSlotWaist = 20, kSlotPowerSource = 21, kSlotAmmo = 22, kSlotGeneral1 = 23,
                              kSlotGeneral10 = 32, kSlotCursor = 33;

inline constexpr int kEquipmentSlots = 23;

inline constexpr int kGeneralSlots = 10;

// Display labels for inventory positions, independent of the wire serializer.
inline constexpr const char* possessions_slot_name(int slot) {
    constexpr const char* kNames[] = {
        "Charm", "Left Ear", "Head", "Face", "Right Ear", "Neck", "Shoulders", "Arms", "Back", "Left Wrist",
        "Right Wrist", "Range", "Hands", "Primary", "Secondary", "Left Finger", "Right Finger", "Chest", "Legs",
        "Feet", "Waist", "Power Source", "Ammo", "General 1", "General 2", "General 3", "General 4", "General 5",
        "General 6", "General 7", "General 8", "General 9", "General 10", "Cursor"};
    return slot >= 0 && slot < 34 ? kNames[slot] : "?";
}

// Corpse slots the server uses for loot: CORPSE_BEGIN 23 .. CORPSE_END 56.
inline constexpr std::int16_t kCorpseSlotBegin = 23, kCorpseSlotEnd = 56;

// One spell block of an item (ClickEffectStruct / ProcEffectStruct /
// WornEffectStruct, 30 bytes each, then a NUL-terminated name and a u32).
// The five dwords after `level` mean, for the click block: max charges,
// cast time (ms), recast delay (s), recast type, unknown; for the proc block:
// four unknowns and the proc rate; for worn, focus, scroll and bard: unknowns.
struct ItemEffect {
    std::int32_t effect = 0; // spell id (-1 / 0 = none)
    std::uint8_t level2 = 0;
    std::uint32_t type = 0;
    std::uint8_t level = 0;
    std::array<std::uint32_t, 5> extra{};
    std::string name;
    std::uint32_t trailer = 0;

    bool operator==(const ItemEffect&) const = default;

    std::int32_t max_charges() const { return static_cast<std::int32_t>(extra[0]); }
    std::int32_t cast_time() const { return static_cast<std::int32_t>(extra[1]); }
    std::uint32_t recast_delay() const { return extra[2]; }
    std::int32_t recast_type() const { return static_cast<std::int32_t>(extra[3]); }
    std::uint32_t proc_rate() const { return extra[4]; }
    bool present() const { return effect > 0; }
};

enum ItemEffectKind : std::uint8_t { kEffectClick, kEffectProc, kEffectWorn, kEffectFocus, kEffectScroll, kEffectBard, kEffectCount };

// EvolvingItem_Struct (25 bytes), present when the header's is_evolving is set.
struct EvolvingInfo {
    std::uint32_t final_item_id = 0;
    std::int32_t evolve_level = 0;
    double progress = 0.0;
    std::uint8_t activated = 0;
    std::int32_t evolve_max_level = 0;
    std::array<std::uint8_t, 4> unknown005{};

    bool operator==(const EvolvingInfo&) const = default;
};

// OP_EvolveItem: the display's Final Result link (XWM_LINK action 1) sends wire action 4.
// These six dwords share their 24-byte layout with action 0's instance progress update.
struct EvolvingFinalResult {
    std::uint32_t item_id = 0, final_item_id = 0, level = 0, hash = 0;
    std::uint32_t display_flags = 1; // native !Shift
};

struct EvolvingItemUpdate {
    std::uint64_t unique_id = 0;
    std::uint32_t percentage = 0, activated = 0;
};

struct BagItem;

// One serialized item instance, in SerializeItem's order. Field names follow
// rof2_structs.h; unknowns are kept so a decoded item re-encodes to the same
// bytes.
struct Item {
    // ItemSerializationHeader (77 bytes)
    std::string serial;               // char[17]: the server's 16-digit serial number
    std::uint32_t stack_count = 1;    // "stacksize": the count of a stackable item, else 1
    std::uint32_t unknown004 = 0;
    ItemSlot slot;                    // header slot_type (u8), main_slot, sub_slot, aug_slot (u16 each)
    std::uint32_t price = 0;          // merchant quote
    std::uint32_t merchant_slot = 1;  // merchant count for merchant items, else 1
    std::uint32_t scaled_value = 0;
    std::uint32_t instance_id = 0;
    std::uint32_t parcel_item_id = 0;
    std::uint32_t last_cast_time = 0;
    std::uint32_t charges = 0;        // 0xffffffff = unlimited
    std::uint32_t attuned = 0;        // inst_nodrop
    std::uint32_t unknown044 = 0, unknown048 = 0, unknown052 = 0;
    std::uint8_t is_evolving = 0;
    EvolvingInfo evolving;            // written only when is_evolving
    std::string ornament_main, ornament_off; // "IT<n>" ornamentation models, usually empty

    // ItemSerializationHeaderFinish (26 bytes)
    std::uint32_t ornament_icon = 0;
    std::int32_t unknowna1 = -1;
    std::uint32_t ornament_hero_model = 0;
    std::int32_t unknown063 = 0;
    std::uint8_t copied = 0;
    std::int32_t unknowna4 = -1;
    std::int32_t unknowna5 = 0;
    std::uint8_t item_class = 0;      // 0 common, 1 container, 2 book

    std::string name, lore, id_file, id_file2;

    // ItemBodyStruct (255 bytes)
    std::uint32_t id = 0;
    std::int32_t weight = 0;          // tenths
    std::uint8_t no_rent = 0;         // 0 = temporary
    std::uint8_t no_drop = 0;         // 0 = NO TRADE (EQEmu's NoDrop: 255 tradeable)
    std::uint8_t attuneable = 0;
    std::uint8_t size = 0;            // 0 tiny .. 4 giant
    std::uint32_t equip_slots = 0;    // bit n = possessions slot n
    std::uint32_t value = 0;          // base price in copper
    std::uint32_t icon = 0;
    std::uint8_t unknown1 = 1, unknown2 = 1;
    std::uint32_t benefit_flag = 0;
    std::uint8_t tradeskills = 0;
    std::int8_t cr = 0, dr = 0, pr = 0, mr = 0, fr = 0, sv_corruption = 0;
    std::int8_t astr = 0, asta = 0, aagi = 0, adex = 0, acha = 0, aint = 0, awis = 0;
    std::int32_t hp = 0, mana = 0;
    std::uint32_t endurance = 0;
    std::int32_t ac = 0, regen = 0, mana_regen = 0, end_regen = 0;
    std::uint32_t classes = 0, races = 0, deity = 0;
    std::int32_t skill_mod_value = 0, skill_mod_max = 0, skill_mod_type = 0;
    std::uint32_t skill_mod_extra = 0;
    std::uint32_t bane_dmg_race = 0, bane_dmg_body = 0, bane_dmg_race_amt = 0;
    std::int32_t bane_dmg_amt = 0;
    std::uint8_t magic = 0;
    std::int32_t cast_time_ = 0;      // food/drink duration
    std::uint32_t req_level = 0, rec_level = 0, rec_skill = 0, bard_type = 0;
    std::int32_t bard_value = 0;
    std::uint8_t light = 0, delay = 0, elem_dmg_type = 0, elem_dmg_amt = 0, range = 0;
    std::uint32_t damage = 0, color = 0, prestige = 0;
    std::uint8_t item_type = 0;       // EQEmu ItemType (0 1HS, 1 2HS, 10 armor, 11 misc, 14 food, ...)
    std::uint32_t material = 0, material_unknown1 = 0, elite_material = 0, heros_forge_model = 0, material_unknown2 = 0;
    float sell_rate = 0.0f;
    std::int32_t combat_effects = 0, shielding = 0, stun_resist = 0, strike_through = 0, extra_dmg_skill = 0,
                 extra_dmg_amt = 0, spell_shield = 0, avoidance = 0, accuracy = 0;
    std::uint32_t charm_file_id = 0;
    std::array<std::uint32_t, 4> faction_mod{};
    std::array<std::int32_t, 4> faction_amt{};
    std::string charm_file;

    // ItemSecondaryBodyStruct (74 bytes)
    std::uint32_t aug_type = 0;
    std::int32_t aug_restrict2 = -1;
    std::uint32_t aug_restrict = 0;
    struct AugSocket {
        std::uint32_t type = 0;
        std::uint8_t visible = 0, unknown = 0;
        bool operator==(const AugSocket&) const = default;
    };
    std::array<AugSocket, 6> aug_sockets{};
    std::uint32_t ldon_point_type = 0, ldon_theme = 0, ldon_price = 0, ldon_sellback_rate = 0, ldon_sold = 0;
    std::uint8_t bag_type = 0, bag_slots = 0, bag_size = 0, weight_reduction = 0;
    std::uint8_t book = 0, book_type = 0;
    std::string book_file;

    // ItemTertiaryBodyStruct (76 bytes)
    std::int32_t lore_group = 0;      // 0 none, -1 lore by item id, > 0 lore group
    std::uint8_t artifact = 0, summoned = 0;
    std::uint32_t favor = 0;
    std::uint8_t fv_no_drop = 0;
    std::int32_t dot_shield = 0, attack = 0, haste = 0, damage_shield = 0;
    std::uint32_t guild_favor = 0, aug_distiller = 0;
    std::int32_t unknown3 = -1;
    std::uint32_t unknown4 = 0;
    std::uint8_t no_pet = 0, unknown5 = 0;
    std::uint8_t potion_belt_enabled = 0;
    std::uint32_t potion_belt_slots = 0;
    std::uint32_t max_stack = 0;      // "stacksize": the stack limit (0 = not stackable)
    std::uint8_t no_transfer = 0;
    std::uint16_t expendable_arrow = 0;
    std::array<std::uint32_t, 4> unknown8_11{};
    std::array<std::uint8_t, 3> unknown12_14{};

    std::array<ItemEffect, kEffectCount> effects{};

    // ItemQuaternaryBodyStruct (171 bytes)
    std::uint32_t script_file_id = 0;
    std::uint8_t quest_item = 0;
    std::uint32_t power = 0, purity = 0;
    std::uint8_t unknown16 = 0;
    std::uint32_t backstab_dmg = 0, ds_mitigation = 0;
    std::int32_t heroic_str = 0, heroic_int = 0, heroic_wis = 0, heroic_agi = 0, heroic_dex = 0, heroic_sta = 0,
                 heroic_cha = 0, heroic_mr = 0, heroic_fr = 0, heroic_cr = 0, heroic_dr = 0, heroic_pr = 0,
                 heroic_sv_corrup = 0;
    std::int32_t heal_amt = 0, spell_dmg = 0, clairvoyance = 0, sub_type = 0;
    std::uint8_t evolve_string = 0, unknown19 = 0;
    std::uint16_t unknown20 = 0;
    std::uint8_t unknown21 = 0, heirloom = 0, placeable = 0;
    std::array<std::uint8_t, 3> unknown22{};
    std::array<std::uint32_t, 3> unknown23_25{};
    float unknown26 = 0.0f, unknown27 = 0.0f;
    std::uint32_t unknown_rof_6 = 0;
    std::uint32_t unknown28 = 0xffffffff;
    std::uint16_t unknown29 = 0;
    std::uint32_t unknown30 = 0xffffffff;
    std::uint16_t unknown31 = 0;
    std::uint32_t unknown32 = 0;
    float unknown33 = 0.0f;
    std::array<std::uint32_t, 4> unknown34_37{};
    std::uint8_t no_zone = 0;
    std::array<std::uint8_t, 4> unknown_rof_7{};
    std::uint8_t no_ground = 0;
    std::array<std::uint8_t, 2> unknown_rof_8{};
    std::uint8_t unknown37a = 0, unknown38 = 0, unknown39 = 1;

    // Bag contents: each preceded on the wire by its u32 index in the bag.
    // SerializeItem gives every content the header slot of the bag's first
    // sub-slot, so the index is the content's real position.
    std::vector<BagItem> contents;

    // The instance's stats-disabled bytes, ItemBase +0x100 (eqlib bDisableAugTexture) and +0x101 (bRankDisabled):
    // client state, not in the packet (ItemClient_UnSerializeItemTree @0x5553b0 never writes them, the constructor
    // clears them); 0x7b28a0 sets them (game::update_stats_disabled).
    bool aug_texture_disabled = false;
    bool rank_disabled = false;

    // ItemDefinition +0x60c (count +0x609): the host subclasses an augment may go into (ItemBase_CanAugmentGoInItem).
    // Read by the original's unserializer but not part of EQEmu's RoF2 serialization, so always empty from the
    // server; not written by write_item.
    std::vector<std::int32_t> socket_subclasses;

    bool operator==(const Item&) const = default;

    bool stackable() const { return max_stack > 1; }
    bool is_bag() const { return item_class == 1; }
    bool is_lore() const { return lore_group != 0; } // ItemBase::IsLore (the item part)
    bool no_trade() const { return no_drop == 0; }
    // Fields ItemDefinition_UnSerialize @0x7f6bf0 reads where EQEmu's RoF2 struct has unknowns:
    // the Placeable byte and the three after it are one int, the placement flags
    // (+0x5ac: 1 open zones, 2 guild halls, 4 houses, 8 yards, 0x80 guild yards; -1 all); unknown30 is the trophy
    // benefit id (+0x5ec, -1 none); bytes 1 and 2 of unknown37 are Collectible (+0x110) and No Destroy (+0x111).
    std::int32_t placeable_flags() const {
        return static_cast<std::int32_t>(placeable | (std::uint32_t{unknown22[0]} << 8) |
                                         (std::uint32_t{unknown22[1]} << 16) | (std::uint32_t{unknown22[2]} << 24));
    }
    std::int32_t trophy_benefit() const { return static_cast<std::int32_t>(unknown30); }
    bool collectible() const { return ((unknown34_37[3] >> 8) & 0xff) != 0; }
    bool no_destroy() const { return ((unknown34_37[3] >> 16) & 0xff) != 0; }
    const Item* content(std::uint32_t index) const;
    Item* content(std::uint32_t index);
};

struct BagItem {
    std::uint32_t index = 0;
    Item item;
    bool operator==(const BagItem&) const = default;
};

// The most contents a bag may hold, and one past the highest bag sub-slot read_item and game::Inventory accept.
// EQEmu's invbag::SLOT_COUNT is 200; Hailstone accepts up to 1000 so a server with larger bags is not refused
// (a deliberate difference; the item's own BagSlots is a u8 on the wire, so a bag still declares at most 255).
inline constexpr std::uint32_t kMaxBagContents = 1000;

// ItemPacketType (eq_packet_structs.h), the u32 before an item in OP_ItemPacket.
inline constexpr std::uint32_t kItemPacketViewLink = 0x00, kItemPacketMerchant = 0x64, kItemPacketTradeView = 0x65,
                               kItemPacketLoot = 0x66, kItemPacketTrade = 0x67, kItemPacketCharInventory = 0x69,
                               kItemPacketLimbo = 0x6a, kItemPacketWorldContainer = 0x6b, kItemPacketRecovery = 0x71,
                               kItemPacketParcel = 0x73;

// OP_ItemPacket / OP_ItemLinkResponse (zone → client): u32 ItemPacketType,
// one item; parcels (0x73) add u32 sent time, u32 + name, u32 + note
// (ENCODE(OP_ItemPacket); OP_ItemLinkResponse is forwarded to it).
struct ItemPacket {
    std::uint32_t type = 0;
    Item item;
    std::uint32_t parcel_sent_time = 0;
    std::string parcel_from, parcel_note;
};

// The reply (S->C on the same opcode, HandleWorldMessage case 0x7f80): the serialized item, then one byte the
// original reads if it is there (EQEmu sends none: 0).
struct ItemPreviewReply {
    Item item;
    std::uint8_t trailing = 0;
};

// OP_MoveItem (28 bytes, both directions; MoveItem_Struct): from, to,
// u32 count. The client sends count 0 for a whole-item move or swap and a
// count only for stack merges and splits; to = invalid
// destroys the item. The server's OP_MoveItem moves or (to invalid) deletes.
// OP_DeleteItem (DeleteItem_Struct) and OP_DeleteCharge (encoded as
// OP_MoveItem) share the layout.
struct MoveItem {
    ItemSlot from;
    ItemSlot to;
    std::uint32_t count = 0;
};

// OP_MoveMultipleItems (client → zone, Send_OP_MoveMultipleItems @0x56fef0; the recorded vectors
// Send_OP_MoveMultipleItems.jsonl): u32 count, then count × 0x24-byte MultiMoveEntry {from (12), to (12), u32
// count, u32 flags (+0x1c), u32 0 (the room mode +0x20, cleared in the list before sending)}. The Ctrl bag moves
// (MoveItem with bMoveBagContents). Each index's last two bytes (ItemSlot::unknown01) are padding the original
// never writes; the client sends 0. An empty list sends nothing (nullopt). EQEmu: Client::Handle_OP_MoveMultipleItems (MultiMoveItem_Struct).
struct MultiMoveEntry {
    ItemSlot from;
    ItemSlot to;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

// OP_LootItem (20 bytes, LootingItem_Struct, both directions): corpse,
// looter, u16 corpse slot (23..56), u16, i32 auto loot, u32. The server
// echoes it once the item is looted.
struct LootItem {
    std::uint32_t lootee = 0;   // corpse spawn id
    std::uint32_t looter = 0;   // our spawn id
    std::uint16_t slot = 0;     // corpse slot from the loot item packet
    std::uint16_t unknown10 = 0;
    std::int32_t auto_loot = 0; // > 0: the server places the item itself, else on the cursor
    std::uint32_t unknown16 = 0;
};

// OP_MoneyOnCorpse (20 bytes, moneyOnCorpseStruct, passed through): the
// loot answer. response 0 someone else is looting, 1 / 3 ok, 2 not at this
// time, 4 hostiles nearby, 5 too far, 6 every item has been sent (SoD+).
struct MoneyOnCorpse {
    std::uint8_t response = 0;
    std::uint8_t unknown1 = 0, unknown2 = 0, unknown3 = 0;
    std::uint32_t platinum = 0, gold = 0, silver = 0, copper = 0;
};

// OP_MoneyUpdate (zone → client, 16 bytes, MoneyUpdate_Struct): the coin the
// player carries now (Client::SendMoneyUpdate, after money is added).
struct Coins {
    std::uint32_t platinum = 0, gold = 0, silver = 0, copper = 0;
};

// OP_ItemLinkClick (52 bytes, ItemViewRequest_Struct; the original fills it
// in ParseItemLinkBody @0x4ed020 and sends it from CChatWindow::
// WndNotification): item id, 6 augment ids, link hash, u32 flags (4, plus 2 /
// 8 / 0x10 with shift / ctrl / alt), is evolving, evolve group, evolve level
// (one dword each), u32 ornament icon. The server answers with
// OP_ItemLinkResponse (an item packet of type 0) or, for a say link, says
// the phrase.
struct ItemLinkClick {
    std::uint32_t item_id = 0;
    std::array<std::uint32_t, 6> augments{};
    std::uint32_t hash = 0;
    std::uint32_t flags = 4;
    std::uint32_t is_evolving = 0, evolve_group = 0, evolve_level = 0;
    std::uint32_t ornament_icon = 0;
};

// OP_ItemLinkResponse sent by the client (Send_OP_ItemLinkResponse @0x69baf0; 0x48 bytes): u32 item id, u32 flags
// (| 2 with shift held), char name[64]; the krono slot's Inspect sends {88888, "Krono", 0} (0x6949d0). The
// server answers with an item packet (OP_ItemLinkResponse).
struct ItemLinkRequest {
    std::uint32_t item_id = 0;
    std::uint32_t flags = 0;
    std::string name;
};

// OP_Consume (C→Z, 24 bytes, RoF2 Consume_Struct): the item's slot (12 B), u32 0xffffffff when eaten automatically
// (999 on a right click: force-fed, half as filling), u32 1 food / 2 drink, u32 2. EQEmu deletes one of the item
// without telling the client and answers with OP_Stamina.
// Coin the server adds without a coin update (EQEmu AddMoneyToPP with no client update): OP_Sound carrying a
// QuestReward_Struct (68 bytes: u32 giver, u32 our id, u32 exp, u32 faction, i32 mod, then u32 copper, silver,
// gold, platinum at +20; also used for plain sounds, with no coin), and OP_CashReward (16 bytes: copper, silver,
// gold, platinum).
struct CoinReward {
    std::uint32_t target_id = 0; // OP_Sound only; 0 for OP_CashReward
    Coins coins;
};

inline constexpr std::uint8_t kItemTypeFood = 14, kItemTypeDrink = 15;

// The other item types Handle_OP_ItemVerifyReply tests (ItemClient::GetItemType).
inline constexpr std::uint8_t kItemTypeScroll = 20, kItemTypePotion = 21, kItemTypeTome = 31, kItemTypeAlcohol = 38,
                              kItemTypePoison = 42;

// OP_ItemVerifyReply (Z→C, 20 bytes, RoF2 ItemVerifyReply_Struct): the slot, the spell to cast (0: the item's own
// click spell; negative: nothing, the dispatch @0x4c579a skips the handler) and the target. EQEmu answers every
// request with spell 0 before it acts on it.
struct ItemVerifyReply {
    ItemSlot slot;
    std::int32_t spell = 0;
    std::uint32_t target_id = 0;
};

// OP_ApplyPoison (both ways, 12 bytes, RoF2 ApplyPoison_Struct): a TypelessInventorySlot {i16 slot, i16 sub, i16 aug,
// i16 unknown} and u32 success. DoPassageOfTime @0x45e1e8 sends the poison's top-level slot as ItemIndex(slot, -1, -1)
// (0x460160); its unknown word and success are stack left-overs there (0 here, and EQEmu reads neither). The
// reply (Handle_OP_ApplyPoison @0x4b5c20) reads only success.
struct ApplyPoison {
    std::int16_t slot = -1;
    std::int16_t sub = -1;
    std::int16_t aug = -1;
    std::int16_t unknown = 0;
    std::uint32_t success = 0;
};


} // namespace hailstone::net
