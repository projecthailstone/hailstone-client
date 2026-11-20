#pragma once
// Server file sync values and bounded requests; no application wire records.
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include "core/sha256.h"
namespace hailstone::net {
inline constexpr std::uint32_t kServerFilesChunkMax = 32768;
// The most entries a list may have, and the longest path (a bigger one is a bad packet).
inline constexpr std::uint32_t kServerFilesMaxEntries = 65536;
inline constexpr std::uint16_t kServerFilesMaxPath = 512;
inline constexpr std::uint32_t kServerFilesStale = 1, kServerFilesUnreadable = 2;

struct ServerFileEntry {
    std::uint32_t size = 0;
    Sha256Digest sha256{};
    std::string path;
};

struct ServerFilesPage {
    std::uint32_t list_id = 0;
    std::uint32_t total = 0;
    std::uint32_t first = 0;
    std::vector<ServerFileEntry> entries;
};

struct ServerFilesChunk {
    std::uint32_t list_id = 0;
    std::uint32_t index = 0;
    std::uint32_t offset = 0;
    std::vector<std::byte> data;
};

struct ServerFilesError {
    std::uint32_t list_id = 0;
    std::uint32_t index = 0;
    std::uint32_t code = 0;
};

using ServerFilesReply = std::variant<ServerFilesPage, ServerFilesChunk, ServerFilesError>;

struct ServerFilesRequest {
    enum class Kind : std::uint8_t { List, Chunk } kind = Kind::List;
    std::uint32_t first = 0;
    std::uint32_t list_id = 0;
    std::uint32_t index = 0;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};
} // namespace hailstone::net
