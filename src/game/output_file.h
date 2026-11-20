#pragma once

// /outputfile's files (Cmd_outputfile @0x4febd0 and the
// writers it calls): what each one writes, as text, from the data the windows show. Cmd_outputfile picks the writer
// by its first word (stricmp): guild, raid, spellbook, inventory, guildbank, realestate, guildhall; the second word
// is the file (none: each writer's default name).

#include <cstdint>
#include <ctime>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "net/messages/guild_bank.h"
#include "net/messages/item.h"

namespace hailstone::game {

class EqStrings;
class Inventory;
struct SpellData;

enum class OutputFileKind : std::uint8_t { None, Guild, Raid, Spellbook, Inventory, GuildBank, RealEstate, GuildHall };
// The first word's writer (None for any other word).
OutputFileKind output_file_kind(std::string_view word);
inline constexpr std::uint32_t kStrOutputFileUsage = 3487; // 0xd9f (colour 0)

// "%04d%02d%02d-%02d%02d%02d" of the local time (the raid and guild writers' default names).
std::string output_file_date(const std::tm& t);

// InventoryDump_WriteItem @0x691ce0: "<label>\tEmpty\t0\t0\t0" for no item, else "<label>\t<name>\t<id>\t<count>\t
// <slots>" (count: the stack of a stackable item, else 1; slots: a bag's slots, else 6), then each bag slot's or each
// valid, visible augment socket's item as "<label>-Slot<n>" (n from 1).
void dump_inventory_item(std::string& out, const net::Item* item, const std::string& label);
// OutputFile_WriteInventory @0x692a50 (default "<name>-Inventory.txt"): a header, the 34 possessions slots (cursor
// last) labelled with their eqstr names (the table at 0xa02238), the 24 bank slots ("Bank<n>", 3496) and the 2
// shared bank slots ("SharedBank<n>", 3497).
std::string inventory_file(const Inventory& inv, const EqStrings& strings);
// OutputFile_WriteSpellbook @0x75c820 (default "<name>-Spellbook.txt"): "<level>\t<name>" for each of the 720 book
// slots holding a valid spell id the spell table knows; the level is GetSpellLevelNeeded(the player's class).
std::string spellbook_file(std::span<const std::uint32_t> book,
                           const std::function<const SpellData*(std::uint32_t)>& spell,
                           const std::function<std::uint8_t(const SpellData&)>& level);
// CRaidWnd_DumpRosterToFile @0x72c320 (default "RaidRoster-<date>.txt"): each raid list row with a name other than
// `empty` (eqstr 6699): "<group>\t<name>\t<level>\t<class>\t<column 5>\t<note>"; then each unassigned row with a
// name: "0\t<name>\t...". Rows are the list's columns; `note` gives a member's note (the notes list).
std::string raid_roster_file(const std::vector<std::vector<std::string>>& raid,
                             const std::vector<std::vector<std::string>>& unassigned, const std::string& empty,
                             const std::function<std::string(const std::string&)>& note);
// CGuildMgmtWnd::DumpToFile @0x681430 (separator "\t"): per member list row, its columns 0..n-2 (n the list's column
// count), then the tribute list's and the notes list's columns 1.. of the same row, each part after the separator.
struct DumpList {
    std::vector<std::vector<std::string>> rows;
    int columns = 0;
};
std::string guild_roster_file(const DumpList& members, const DumpList& tribute, const DumpList& notes,
                              std::string_view separator = "\t");
// OutputFile_WriteGuildRoster @0x67e7a0 (the guild bank; default "<name>-GuildBank.txt"): a header, then the 200
// main slots ("Bank-%03d") and the 40 deposit slots ("Deposit-%02d") that hold an item: "<label>\t<name>\t<id>\t
// <count>\t<permission>"; the permission is the member's name for a single-member item (1), else its eqstr name
// (6103 + permission).
std::string guild_bank_file(const std::vector<std::optional<net::GuildBankItem>>& main,
                            const std::vector<std::optional<net::GuildBankItem>>& deposit, const EqStrings& strings);
// OutputFile_WriteRealEstate @0x5b37d0 (default "<name>-RealEstate.txt"): the header, then the items of the
// character's real estate (none under EQEmu, which has no real estate).
std::string real_estate_file();

} // namespace hailstone::game
