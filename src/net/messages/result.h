#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "net/messages/opcode.h"

namespace hailstone::net::messages {

enum class CodecErrorKind : std::uint8_t {
    UnsupportedOpcode,
    UnsupportedPayload,
    InvalidValue,
    TruncatedPacket,
    MalformedPacket,
    ResourceLimit,
    IncompleteMessage,
    DeliveryRejected, // not admitted (disconnected, filtered, or empty encode batch)
};

struct CodecError {
    CodecErrorKind kind = CodecErrorKind::MalformedPacket;
    std::string detail;
    // Wire attribution is diagnostic metadata; no wire opcode is treated as
    // a semantic operation unless the selected stock codec identifies it.
    std::optional<ServiceScope> service;
    std::optional<std::uint16_t> wire_opcode;
    std::optional<GameOpcode> opcode;
    // Application payload bytes delivered by the session (excludes framing).
    // Even unknown/server-specific wire opcodes retain size for diagnostics;
    // recording raw payload bytes here would leak character/account data.
    std::optional<std::size_t> payload_size;
};

// Explicit outcome for codec encode/decode; a successful empty batch is
// distinct from an error, including unsupported packet handling.
template <class T>
class CodecResult {
public:
    static CodecResult success(T value) {
        CodecResult result;
        result.value_ = std::move(value);
        return result;
    }
    static CodecResult failure(CodecError error) {
        CodecResult result;
        result.error_ = std::move(error);
        return result;
    }

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }
    const T* value() const { return value_ ? &*value_ : nullptr; }
    T* value() { return value_ ? &*value_ : nullptr; }
    const CodecError* error() const { return error_ ? &*error_ : nullptr; }

private:
    std::optional<T> value_;
    std::optional<CodecError> error_;
};

} // namespace hailstone::net::messages
