#pragma once

// Shared item serialization used by RoF2 item packets and by bank.dat v1.
// The persistent v1 format uses the same SerializeItem field order; keep that
// byte representation stable even if network protocols evolve independently.

#include "core/byte_io.h"
#include "net/messages/item.h"

namespace hailstone::net {

// The reader throws std::out_of_range for malformed/truncated items, excessive
// nesting, or invalid bag indices. A caller controls full-buffer consumption.
Item read_item(ByteReader& reader);
void write_item(ByteWriter& writer, const Item& item);

} // namespace hailstone::net
