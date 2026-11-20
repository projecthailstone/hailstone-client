#pragma once

// The server's client files (docs/layout.md "Server files"): the files a server owner puts in its client_files/
// folder, sent to the client through the zone by Hailstone's server script (server/README.md) and kept in
// servers/<profile>/assets/. Two custom opcodes beside the settings (net/protocol/extensions/hailstone/settings_packets.h).
//
// OP_HsFilesRequest 0x1434, client -> zone: {u32 magic 'HSFL'; u16 version 1; u16 action; ...}
//   action 1, a page of the list: {u32 first (the index of its first entry)}
//   action 2, a piece of a file: {u32 list id; u32 index; u32 offset; u32 length (at most kServerFilesChunkMax)}
// OP_HsFiles 0x1435, zone -> client: {u32 magic; u16 version 1; u16 action; ...}
//   action 1, a page of the list: {u32 list id; u32 total; u32 first; u32 count; count x {u32 size; u8 sha256[32];
//     u16 length; char path[length] ('/' separators, relative to client_files/)}}. A total of 0: nothing to send.
//   action 2, the piece: {u32 list id; u32 index; u32 offset; u32 length; u8 data[length]}
//   action 3, an error: {u32 list id; u32 index; u32 code (kServerFilesStale, kServerFilesUnreadable)}
// The list id changes whenever the server's files do; a piece asked with an old one gets kServerFilesStale.

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/sha256.h"
#include "net/wire/app_packet.h"
#include "net/messages/server_files.h"

namespace hailstone::net {

inline constexpr std::uint16_t kOpHsFilesRequest = 0x1434;
inline constexpr std::uint16_t kOpHsFiles = 0x1435;
inline constexpr std::uint32_t kServerFilesMagic = 0x4c465348; // "HSFL" on the wire
inline constexpr std::uint16_t kServerFilesVersion = 1;
inline constexpr std::uint16_t kServerFilesList = 1, kServerFilesChunk = 2, kServerFilesError = 3;
AppPacket encode_server_files_list_request(std::uint32_t first);
AppPacket encode_server_files_chunk_request(std::uint32_t list_id, std::uint32_t index, std::uint32_t offset,
                                            std::uint32_t length);
std::optional<ServerFilesReply> decode_server_files(const AppPacket& p);
// The server's side, for tests.
AppPacket encode(const ServerFilesPage& v);
AppPacket encode(const ServerFilesChunk& v);
AppPacket encode(const ServerFilesError& v);

} // namespace hailstone::net
