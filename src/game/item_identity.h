#pragma once

// Stable identity of a loot offer, independent of any game's item wire format.
// Used only to detect an item changing while a confirmation is open.

#include <string>

namespace hailstone::net { struct Item; }

namespace hailstone::game {

// Includes the instance, quantity, charges, augments and recursive bag contents.
// Excludes volatile display/progression data (evolving progress, cast time,
// merchant quotes) so a background refresh does not invalidate an offer.
std::string loot_item_identity(const net::Item& item);

} // namespace hailstone::game
