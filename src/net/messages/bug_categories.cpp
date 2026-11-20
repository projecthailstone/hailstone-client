#include "net/messages/player_commands.h"

namespace hailstone::net {

const char* bug_category_name(std::uint32_t category) {
    static const char* const kNames[kBugCategories] = {"Other", "Video", "Audio", "Pathing", "Quest",
                                                       "Tradeskills", "Spell Stacking", "Doors and Portals", "Items",
                                                       "NPC", "Dialogs", "LoN - TCG", "Mercenaries"};
    return category < kBugCategories ? kNames[category] : kNames[0];
}

} // namespace hailstone::net
