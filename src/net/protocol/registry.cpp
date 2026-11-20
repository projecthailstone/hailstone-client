#include "net/protocol/registry.h"

#include <utility>

#include "net/protocol/rof2/codec.h"
#include "net/protocol/extensions/hailstone/codec.h"

namespace hailstone::net::protocol {

CodecRegistry::CodecRegistry() {
    register_codec("rof2", [] { return std::make_unique<rof2::Codec>(); });
    register_extension("hailstone", [] { return std::make_unique<hailstone_extension::Codec>(); });
}

bool CodecRegistry::register_codec(std::string id, Factory factory) {
    if (id.empty() || !factory) return false;
    return factories_.emplace(std::move(id), std::move(factory)).second;
}

std::unique_ptr<IGameCodec> CodecRegistry::create(std::string_view id) const {
    const auto it = factories_.find(id);
    return it == factories_.end() ? nullptr : it->second();
}

std::vector<std::string> CodecRegistry::available() const {
    std::vector<std::string> ids;
    ids.reserve(factories_.size());
    for (const auto& [id, factory] : factories_) ids.push_back(id);
    return ids;
}

bool CodecRegistry::register_extension(std::string id, Factory factory) {
    if (id.empty() || !factory) return false;
    return extension_factories_.emplace(std::move(id), std::move(factory)).second;
}
std::unique_ptr<IGameCodec> CodecRegistry::create_extension(std::string_view id) const {
    const auto it = extension_factories_.find(id);
    return it == extension_factories_.end() ? nullptr : it->second();
}
std::vector<std::string> CodecRegistry::available_extensions() const {
    std::vector<std::string> ids;
    for (const auto& [id, factory] : extension_factories_) ids.push_back(id);
    return ids;
}
} // namespace hailstone::net::protocol
