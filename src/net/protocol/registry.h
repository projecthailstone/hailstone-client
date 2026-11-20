#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "net/protocol/codec.h"

namespace hailstone::net::protocol {

// A registry is constructed with only the codec implementations actually
// shipped by this build. Selecting an unknown codec fails; it never silently
// downgrades to RoF2 or changes wire layouts based on opcode guesses.
class CodecRegistry {
public:
    using Factory = std::function<std::unique_ptr<IGameCodec>()>;

    CodecRegistry(); // installs the RoF2 world/zone implementation
    bool register_codec(std::string id, Factory factory);
    std::unique_ptr<IGameCodec> create(std::string_view id) const;
    bool register_extension(std::string id, Factory factory);
    std::unique_ptr<IGameCodec> create_extension(std::string_view id) const;
    std::vector<std::string> available_extensions() const;
    std::vector<std::string> available() const;

private:
    std::map<std::string, Factory, std::less<>> factories_;
    std::map<std::string, Factory, std::less<>> extension_factories_;
};

} // namespace hailstone::net::protocol
