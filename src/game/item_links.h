#pragma once

// Item links inside chat text. A link is `\x12` + tag code + body + visible
// text + `\x12`. Tag code '0' is an
// item: its body is the item id and six augment ids as hex, then is-evolving
// (1 digit), evolve group (4 hex), evolve level (2 hex), ornament icon
// (5 hex) and the link hash (8 hex).
//
// Stock RoF2 writes the ids with 5 hex digits (body 55 characters after the
// code); the NMS client patch and server (rof2_limits.h SAY_LINK_BODY_SIZE
// 77 = code + 76) use 8 digits so item ids above 0xFFFFF fit (NMS Enchanted
// and Legendary tiers are the base id + 1,000,000 / + 2,000,000). Both
// widths are accepted: the wide form when its 76 characters are hex digits,
// else the stock one.
//
// Achievement tags ('3') place caret-delimited metadata before an apostrophe
// and the visible name. Other tag codes (1 player, 2 spam) stay plain text.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "net/messages/item.h"

namespace hailstone::game {

struct ItemLink {
    std::uint32_t item_id = 0;
    std::array<std::uint32_t, 6> augments{};
    std::uint32_t is_evolving = 0;
    std::uint32_t evolve_group = 0;
    std::uint32_t evolve_level = 0;
    std::uint32_t ornament_icon = 0;
    std::uint32_t hash = 0;
    std::string text; // the visible name
    bool wide = false; // 8-digit (NMS) ids

    // The OP_ItemLinkClick request the original builds from a clicked link.
    net::ItemLinkClick click_request() const;
};

// Parses what lies between the two \x12 of an item tag (code included).
std::optional<ItemLink> parse_item_link(std::string_view tag);

// An item tag as the data of an STML link (<a WndNotify="27,data">): 'x' and
// the tag's bytes as hex. The tag's visible text is whatever the sender
// typed, and the STML parsers end a tag at its first '>' and decode no
// entities in attributes, so the raw text could close the <a> tag and inject
// markup into everyone's chat. The original puts the tag in as it is
// (CStmlWnd::MakeWndNotificationTag); the encoding is Hailstone's.
std::string stml_link_data(std::string_view tag);
// Back from stml_link_data; data not in that form is returned as it is (a
// tag written raw by a script, or another window's link data).
std::string stml_link_tag(std::string_view data);
// Text for STML: '<', '>' and '&' as entities.
std::string escape_stml(std::string_view text);

struct AchievementChatLink {
    std::string metadata, text;
};
// The chat envelope only. The metadata still needs net::parse_achievement_link
// against the current received catalog before it can be activated.
std::optional<AchievementChatLink> parse_achievement_chat_link(std::string_view tag);

// A run of chat text: plain text, or a link.
struct ChatSegment {
    std::string text;
    std::optional<ItemLink> link;
    std::optional<AchievementChatLink> achievement = std::nullopt;
};

// Splits a chat line into text and links. Unterminated tags stay as text.
std::vector<ChatSegment> split_links(std::string_view text);

// The line as the player reads it: each tag replaced by its visible text
// (__CleanItemTags @0x4ed580).
std::string strip_links(std::string_view text);

// Utf8HashNoCase @0x809610: h = h * 31 + towupper(c) over the UTF-16 codes of
// a UTF-8 string (1- to 3-byte sequences, decoded with the original's 16-bit
// arithmetic; a 4- to 6-byte sequence repeats the previous code, and as the
// first character it reads a value the original takes from its stack, here
// 0). towupper is the C locale's (MSVC: a..z only).
std::uint32_t utf8_hash_no_case(std::string_view text);

// What ItemBase_CalcItemLinkHash @0x7bb5e0 reads of an item: its definition's
// fields and, as their callees return them, the stat sums.
struct ItemLinkHashInput {
    std::string name;
    std::int32_t item_number = 0;
    std::uint8_t type = 0;            // ItemDefinition::Type +0x11a: 0 common, 1 container, 2 book
    std::int32_t cost = 0;            // +0xf4
    std::int32_t icon = 0;            // +0xf8
    std::uint8_t light = 0;           // +0x181
    std::uint8_t item_class = 0;      // +0x1d4 (EQEmu's ItemType)
    std::int32_t charm_file_id = 0;   // +0x518
    std::uint8_t slots = 0;           // +0x521 (bag slots)
    std::uint8_t weight_reduction = 0; // +0x523
    std::int8_t book_lang = 0;        // +0x525
    std::int32_t favor = 0;           // +0x544
    std::int32_t guild_favor = 0;     // +0x548
    struct Augment {
        std::int32_t item_number = 0;
        std::int32_t charm_file_id = 0;
    };
    std::array<std::optional<Augment>, 6> sockets{}; // GetChildItem(0..5)
    std::int32_t weight = 0;          // SumIntStatRecursive(Weight, ...): without the contents
    std::int32_t contents_value = 0;  // the contents' GetItemValue sum (Type 0 only)
    std::int32_t required_level = 0;  // GetMaxIntFieldRecursive(RequiredLevel, 1)
    std::int32_t size = 0;            // GetSize(1)
    std::int32_t ac = 0, hp = 0, mana = 0; // GetAC / GetHP / GetMana(1, 0, 1)
};

// The text ItemBase_CalcItemLinkHash hashes: "<ItemNumber><Name>", then by
// Type: 1 "%x%d%09X%d" (slots, weight reduction, value, weight); 2
// "%d%d%09X" (weight, language, value); 0 the augments' item numbers, then
// eight numbers when the item or an augment has a CharmFileID, else 13; other
// Types nothing. The value is Cost (+ the contents' value for Type 0). The
// whole text is cut at 0x7ff bytes, each part at 0x7ff.
std::string item_link_hash_text(const ItemLinkHashInput& in);
// Utf8HashNoCase of that text.
std::uint32_t item_link_hash(const ItemLinkHashInput& in);

// The hash input for one of the client's items: the stat getters are
// approximated from the item and its augments (contents 0..5 of a common
// item): weight the item's own, contents value the augments' values summed,
// required level the highest, size the item's, AC/HP/mana summed.
ItemLinkHashInput item_link_hash_input(const net::Item& item);

// Builds a link tag (with both \x12) for an item, in the wide (NMS) or stock
// form, with hash 0 (no item data).
std::string make_item_link(std::uint32_t item_id, std::string_view name, bool wide);
// The link the original builds for an item it has: its augments' ids and the
// hash of ItemBase_CalcItemLinkHash.
std::string make_item_link(const net::Item& item, bool wide);

// ItemBase_GetUnknown0x100Max @0x7b2180 with argument 1: 0 for an item whose Type (ItemDefinition +0x11a, EQEmu's
// ItemClass) is not 0; else the highest BenefitFlag (+0x100) above 0 of the item and, the same way, of each item in
// it (a common item's augments).
std::int32_t benefit_flag_max(const net::Item& item);

// CItemDisplayWnd::WndNotification @0x6ae870, a left click (1) on IconButton: the window's item goes as a link into
// the active chat window's input (GetActiveChatWindow: the Always Chat Here one, else the current one;
// CreateItemTagString, AddItemTag), which then has the focus; only while benefit_flag_max is 0.
inline bool display_icon_links(const net::Item& item) { return benefit_flag_max(item) == 0; }

// The loot window's Link All button (CLootWnd_LinkItemToChat @0x6bcf20) over the active chat window's input line:
// the text typed when it was pressed is kept; each link is added at the end, ", " between links. When ", " does not
// fit (CEditWnd::ReplaceSelection: the raw text, tags included, over MaxChars, 512 for the English client) the line
// is sent (ParentWndNotification 6, Enter) and the kept text put back; when a link does not fit
// (CEditWnd::AddItemTag @0x87d360: the input's tag count +0x308 at 10, or MaxChars), the line is sent and the link
// tried again on the kept text, unless no link was added yet (then it is left out). `sent`: the lines Enter sent, in
// order; `text`: what the input holds after. The tag count is counted from the text here.
struct LinkAllChat {
    std::vector<std::string> sent;
    std::string text;
};
inline constexpr std::size_t kChatInputMaxChars = 0x200; // CChatWindow: MaxChars 0x200 (0xaa for other languages)
inline constexpr int kChatInputMaxTags = 10;
LinkAllChat link_all_into_chat(const std::string& typed, const std::vector<std::string>& tags,
                               std::size_t max_chars = kChatInputMaxChars, int max_tags = kChatInputMaxTags);

} // namespace hailstone::game
