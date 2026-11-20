#include "net/protocol/extensions/hailstone/server_files_packets.h"

#include "core/byte_io.h"
#include "net/wire/byte_codec.h"

namespace hailstone::net {

using detail::decode_with;

namespace {

void header(ByteWriter& w, std::uint16_t action) {
    w.le(kServerFilesMagic);
    w.le(kServerFilesVersion);
    w.le(action);
}

} // namespace

AppPacket encode_server_files_list_request(std::uint32_t first) {
    ByteWriter w;
    header(w, kServerFilesList);
    w.le(first);
    return AppPacket(kOpHsFilesRequest, w.take());
}

AppPacket encode_server_files_chunk_request(std::uint32_t list_id, std::uint32_t index, std::uint32_t offset,
                                            std::uint32_t length) {
    ByteWriter w;
    header(w, kServerFilesChunk);
    w.le(list_id);
    w.le(index);
    w.le(offset);
    w.le(length);
    return AppPacket(kOpHsFilesRequest, w.take());
}

std::optional<ServerFilesReply> decode_server_files(const AppPacket& p) {
    if (p.opcode != kOpHsFiles) return std::nullopt;
    auto got = decode_with(p, [](ByteReader& r) -> std::optional<ServerFilesReply> {
        if (r.le<std::uint32_t>() != kServerFilesMagic || r.le<std::uint16_t>() != kServerFilesVersion)
            return std::nullopt;
        switch (r.le<std::uint16_t>()) {
        case kServerFilesList: {
            ServerFilesPage v;
            v.list_id = r.le<std::uint32_t>();
            v.total = r.le<std::uint32_t>();
            v.first = r.le<std::uint32_t>();
            std::uint32_t n = r.le<std::uint32_t>();
            if (v.total > kServerFilesMaxEntries || n > v.total || v.first > v.total - n) return std::nullopt;
            for (std::uint32_t i = 0; i < n; ++i) {
                ServerFileEntry e;
                e.size = r.le<std::uint32_t>();
                for (auto& b : e.sha256) b = r.le<std::uint8_t>();
                auto len = r.le<std::uint16_t>();
                if (len == 0 || len > kServerFilesMaxPath) return std::nullopt;
                auto bytes = r.bytes(len);
                e.path.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                v.entries.push_back(std::move(e));
            }
            return v;
        }
        case kServerFilesChunk: {
            ServerFilesChunk v;
            v.list_id = r.le<std::uint32_t>();
            v.index = r.le<std::uint32_t>();
            v.offset = r.le<std::uint32_t>();
            std::uint32_t len = r.le<std::uint32_t>();
            if (len > kServerFilesChunkMax) return std::nullopt;
            auto bytes = r.bytes(len);
            v.data.assign(bytes.begin(), bytes.end());
            return v;
        }
        case kServerFilesError: {
            ServerFilesError v;
            v.list_id = r.le<std::uint32_t>();
            v.index = r.le<std::uint32_t>();
            v.code = r.le<std::uint32_t>();
            return v;
        }
        default:
            return std::nullopt;
        }
    });
    return got ? *got : std::nullopt;
}

AppPacket encode(const ServerFilesPage& v) {
    ByteWriter w;
    header(w, kServerFilesList);
    w.le(v.list_id);
    w.le(v.total);
    w.le(v.first);
    w.le(static_cast<std::uint32_t>(v.entries.size()));
    for (const auto& e : v.entries) {
        w.le(e.size);
        for (auto b : e.sha256) w.le(b);
        w.le(static_cast<std::uint16_t>(e.path.size()));
        w.bytes(std::as_bytes(std::span(e.path.data(), e.path.size())));
    }
    return AppPacket(kOpHsFiles, w.take());
}

AppPacket encode(const ServerFilesChunk& v) {
    ByteWriter w;
    header(w, kServerFilesChunk);
    w.le(v.list_id);
    w.le(v.index);
    w.le(v.offset);
    w.le(static_cast<std::uint32_t>(v.data.size()));
    w.bytes(v.data);
    return AppPacket(kOpHsFiles, w.take());
}

AppPacket encode(const ServerFilesError& v) {
    ByteWriter w;
    header(w, kServerFilesError);
    w.le(v.list_id);
    w.le(v.index);
    w.le(v.code);
    return AppPacket(kOpHsFiles, w.take());
}

} // namespace hailstone::net
