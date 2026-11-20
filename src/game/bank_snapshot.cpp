#include "game/bank_snapshot.h"

#include <stdexcept>

#include "core/byte_io.h"
#include "net/wire/item_serialization.h"

namespace hailstone::game {

namespace {

constexpr char kMagic[4] = {'H', 'S', 'B', 'K'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kMaxItems = 4096; // as OP_CharInventory's limit

bool banked(std::int16_t type) { return type == net::kInvBank || type == net::kInvSharedBank; }

} // namespace

BankSnapshot bank_snapshot_of(const Inventory& inv, std::int64_t seen) {
    BankSnapshot s;
    s.seen = seen;
    for (const auto& [key, item] : inv.items())
        if (banked(key.first)) s.items.push_back(item);
    return s;
}

std::string encode_bank_snapshot(const BankSnapshot& snapshot) {
    ByteWriter w;
    for (char c : kMagic) w.le(static_cast<std::uint8_t>(c));
    w.le(kVersion);
    w.le(snapshot.seen);
    w.le(static_cast<std::uint32_t>(snapshot.items.size()));
    for (const auto& it : snapshot.items) net::write_item(w, it);
    const auto& d = w.data();
    return std::string(reinterpret_cast<const char*>(d.data()), d.size());
}

std::optional<BankSnapshot> decode_bank_snapshot(std::span<const std::byte> data) {
    try {
        ByteReader r(data);
        for (char c : kMagic)
            if (r.le<std::uint8_t>() != static_cast<std::uint8_t>(c)) return std::nullopt;
        if (r.le<std::uint32_t>() != kVersion) return std::nullopt;
        BankSnapshot s;
        s.seen = r.le<std::int64_t>();
        const std::uint32_t count = r.le<std::uint32_t>();
        if (count > kMaxItems) return std::nullopt;
        for (std::uint32_t i = 0; i < count; ++i) {
            net::Item it = net::read_item(r);
            if (banked(it.slot.type) && it.slot.slot >= 0 && it.slot.sub < 0) s.items.push_back(std::move(it));
        }
        if (!r.empty()) return std::nullopt;
        return s;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

} // namespace hailstone::game
