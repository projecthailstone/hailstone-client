#pragma once

// The marketplace (CMarketplaceWnd, the in-game store): its catalog, funding tiers, per-row states, wallet and
// purchases. None of these opcodes is in EQEmu's patch_RoF2.conf (stock EQEmu has no marketplace); a server that
// sells through the window answers the original client's opcodes, and so does this client. Layouts follow the
// original's readers and senders (CEverQuest::HandleWorldMessage's cases 0x1e9f @0x4d2969, 0x09bf @0x4d2aef, 0x4b7f
// @0x4d2b3e, 0x77a7 @0x4d2c73, 0x0a37 @0x4d2ee9, 0x0114 @0x4d30ef; MarketplaceItem_Deserialize @0x7f8a30,
// MarketplaceFund_Deserialize @0x7f9170, CMarketplaceWnd::WndNotification @0x6e1f70, AboutToShow @0x6e35d0) as the
// original reads and sends them. Per-packet notes: docs/protocol/rof2-packets.md
// "Marketplace".
//
// The catalog, funding and state feeds are read the way the original's CUnSerializeBuffer reads them: every field
// on its own guard, so a short packet leaves the fields it lacks at their defaults instead of failing (a later u8
// can still be read where a u32 no longer fits). Where the original reads past the packet (an unterminated string,
// a string length longer than the packet) these decoders stop at its end.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/wire/app_packet.h"

#include "net/messages/marketplace.h"

namespace hailstone::net {

// C->S.
inline constexpr std::uint16_t kOpMarketplaceRequestItems = 0x5148;   // 2 bytes: the catalog (list empty on open)
inline constexpr std::uint16_t kOpMarketplaceRequestFunding = 0x76c3; // 2 bytes: the funding tiers (list empty)
inline constexpr std::uint16_t kOpMarketplaceReady = 0x356f;          // 2 bytes: every open, and after a catalog
inline constexpr std::uint16_t kOpMarketplaceHello = 0x5bd5;          // 78 bytes, at every open (Send @0x6d1ab0)
inline constexpr std::uint16_t kOpMarketplacePurchase = 0x4a74;       // 82 bytes: entry id +0x48, quantity +0x4c
// S->C.
inline constexpr std::uint16_t kOpMarketplaceItems = 0x1e9f;          // the catalog, a full replace
inline constexpr std::uint16_t kOpMarketplaceFunding = 0x09bf;        // the funding tiers, a full replace
inline constexpr std::uint16_t kOpMarketplaceStates = 0x77a7;         // the rows' states (answer to Ready)
inline constexpr std::uint16_t kOpMarketplaceWallet = 0x4b7f;         // the balance
inline constexpr std::uint16_t kOpMarketplacePurchaseResult = 0x0a37; // a purchase's answer
inline constexpr std::uint16_t kOpMarketplaceUnavailable = 0x0114;    // eqstr 7631 and the window closed
inline constexpr std::uint16_t kOpMarketplacePreview = 0xb07f;        // OP_ItemPreviewRequest's reply for the window

// ---------------------------------------------------------------- the catalog (0x1e9f)

// `now` is time(NULL) when the packet arrived. Entries are kept in wire order. A count the packet cannot hold stops
// at the packet's end (the original goes on making default entries, 0x2118 bytes each, for the whole count).
std::optional<std::vector<MarketplaceEntry>> decode_marketplace_items(const AppPacket& p, std::int64_t now = 0);
AppPacket encode_marketplace_items(const std::vector<MarketplaceEntry>& v, std::int64_t now = 0); // server direction

// ---------------------------------------------------------------- funding tiers (0x09bf)

std::optional<std::vector<MarketplaceFund>> decode_marketplace_funding(const AppPacket& p);
AppPacket encode_marketplace_funding(const std::vector<MarketplaceFund>& v); // server direction

// ---------------------------------------------------------------- row states (0x77a7)

std::optional<std::vector<MarketplaceRowState>> decode_marketplace_states(const AppPacket& p);
AppPacket encode_marketplace_states(const std::vector<MarketplaceRowState>& v); // server direction

// ---------------------------------------------------------------- the wallet (0x4b7f)

inline constexpr std::size_t kMarketplaceWalletSize = 0x54;
std::optional<MarketplaceWallet> decode_marketplace_wallet(const AppPacket& p);
AppPacket encode(const MarketplaceWallet& v); // server direction

// ---------------------------------------------------------------- the purchase (0x4a74) and its result (0x0a37)

// 80 bytes: zeros, the entry id at +0x48 and the quantity at +0x4c (the original leaves +0x40..+0x47 as stack
// residue; this client sends zeros).
inline constexpr std::size_t kMarketplacePurchaseSize = 80;
AppPacket encode_marketplace_purchase(std::uint32_t entry_id, std::uint32_t quantity);

inline constexpr std::size_t kMarketplacePurchaseResultSize = 0x8f0; // the two strings at 1024 bytes each
std::optional<MarketplacePurchaseResult> decode_marketplace_purchase_result(const AppPacket& p);
AppPacket encode(const MarketplacePurchaseResult& v); // server direction

// ---------------------------------------------------------------- the small requests

// 0x5148 / 0x76c3 / 0x356f: the opcode alone.
AppPacket encode_marketplace_request(std::uint16_t opcode);
// 0x5bd5 (Send @0x6d1ab0): 76 bytes, zero but the dword +0x48 = 1.
inline constexpr std::size_t kMarketplaceHelloSize = 76;
AppPacket encode_marketplace_hello();
// OP_ItemPreviewRequest from the window (CMarketplaceWnd_SendItemPreview @0x6d86c0): 80 bytes, the item id, empty
// ornament names, -1 at +0x40, and the u16 +0x4c = 1, which a server answers on 0xb07f (the object preview window
// sends 0 there and is answered on 0x7f80).
AppPacket encode_marketplace_preview_request(std::uint32_t item_id);

} // namespace hailstone::net
