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

// One listing (the 0x2118-byte MarketplaceItem node, ctor @0x7f8970 for the defaults). Strings are NUL-terminated
// on the wire and cut to the node's buffer (name 127 characters, the 64-byte one 63, the others 1023).
struct MarketplaceEntry {
    std::uint32_t id = 0;            // +0x10: the key purchases and the state feed name
    std::uint32_t price = 0;         // +0x8d4: what one costs now
    std::uint32_t icon = 0;          // +0x8d8: items.icon (drawn as icon - 500; 0 draws the blank cell)
    std::uint32_t max_quantity = 0;  // +0x8dc: per purchase; 0 none (the client also stops at 10)
    std::string name;                // +0x454
    std::string category;            // +0x4d4: "A/B/C", the category tree's only source
    std::string description;         // +0x54: STML
    std::string text14;              // +0x14 (64): no reader known
    std::uint8_t flag8e0 = 0;        // +0x8e0: no reader known
    bool is_new = false;             // +0x8e1: the New panel and the "*" mark
    std::string text8e2;             // +0x8e2: no reader known
    std::string purchase_message;    // +0xce2: the start of the purchase result's text (HWM case 0x0a37)
    bool item_of_week = false;       // +0x10e2
    std::int32_t item_of_week_rank = -1; // +0x10e4: the highest wins (ctor -1)
    std::uint32_t kind = 0;          // +0x10e8: 0 in-game; 3 quest-reward gate; 4 and 6 Station Cash (dead pages)
    std::uint32_t u10ec = 0;         // +0x10ec
    std::string text10f0, text14f0, text18f0, text1cf0; // +0x10f0..+0x1cf0: kind 4 compares the first two
    std::uint32_t u20f0 = 0, u20f4 = 0;
    bool is_featured = false;        // +0x20f8: the Featured panel
    std::uint32_t was_price = 0;     // +0x20fc: struck through when price < was_price
    // +0x2100 / +0x2104: relative seconds on the wire, kept as an absolute time (time(NULL) + value; 0 on the
    // wire is -1, never). A packet that ends before one of them stores the read's own time() value instead
    // (the original reads into the variable time() just wrote), as it does for +0x2108.
    std::int64_t available_until = -1;
    std::int64_t sale_until = -1;
    std::uint32_t u2108 = 0;
    std::uint32_t money_type = 0;    // +0x210c: 0 the integer price, 1 "%1.2f" of price * 0.01, else "N/A"
    std::uint32_t u2110 = 0;
    std::uint8_t u2114 = 0;
};

// Station Cash amounts for Add Funds (the 0x94-byte node). The client only shows them on the funding pages, whose
// purchase went to the publisher's web service.
struct MarketplaceFund {
    std::uint32_t amount = 0;   // +0x10
    std::uint32_t u0c = 0;      // +0x0c
    std::string text14, text54; // 63 characters each; the window keys its rows on text54
};

// u32 count, then {u32 entry id, u32 state, u32 item id, u32, u32 length + bytes}. A field the packet lacks keeps
// the previous entry's value (the handler's locals are set once, before its loop); a missing length reads as 0.
struct MarketplaceRowState {
    std::uint32_t entry_id = 0;
    std::uint32_t state = 0;    // 2: not counted (every row starts at 2)
    std::uint32_t item_id = 0;  // the preview's item; 0 none
    std::uint32_t u10 = 0;      // row +0x10
    std::string category;       // a second category for the row; empty none
};

// Read as a struct at fixed offsets (no reader): +0x44 status (0, or only a debug line is written), +0x48 the
// balance, +0x4c a byte (the "billing zip on file" flag: 1 skips the zip page), +0x50 the type (1 the marketplace's;
// 2 the Krono count, not kept here). The original reads 0x54 bytes whatever the size; a shorter packet is refused.
struct MarketplaceWallet {
    std::uint32_t status = 0;
    std::uint32_t balance = 0;
    bool zip_on_file = false;
    std::uint32_t type = 1;
};

// Read as a struct at fixed offsets: +0x40 the entry id, +0xc8 a byte (set: success, result code 0), +0xd8 a dword
// (set: the Station Cash success line, eqstr 7621, else 5361), +0xdc the new balance, +0xe0 where the item went
// (1 inventory, 2 the bank, 4 the shared bank), +0xec the failure code (when +0xc8 is clear), and two strings at
// +0xf0 and +0x4f0 appended to the success text. The original reads them whatever the size; bytes the packet
// lacks read as zero here.
struct MarketplacePurchaseResult {
    std::uint32_t entry_id = 0;
    bool success = false;
    bool station_cash = false;
    std::uint32_t balance = 0;
    std::uint32_t placed = 0;
    std::uint32_t code = 0;
    std::string note_f0, note_4f0;
};


struct MarketplacePurchaseIntent { std::uint32_t entry_id=0, quantity=1; };
struct MarketplacePostalCode { std::string zip; };

} // namespace hailstone::net
