#include "app/world_objects.h"

#include "game/zone_ladders.h"
#include "net/connection/game_connection.h"

namespace hailstone::app {

// EQObject_Construct @0x503e20 gives the actor type 6 (Ladder) when the
// object's actor tag starts with "LADDER" (case-insensitive, 6 characters).
// The ladder actor's height (its vtable +0x60) is not traced: the top of the
// model's solid bounds at its scale is used.
std::optional<game::mechanics::LadderSwitch> WorldObjects::ladder(const net::GameConnection& conn,
                                                                  std::uint32_t drop_id) const {
    for (const auto& o : conn.ground_objects()) {
        if (o.drop_id != drop_id) continue;
        if (!game::is_ladder_actor(o.name)) return std::nullopt;
        game::mechanics::LadderSwitch l;
        l.type = game::mechanics::kLadderSwitchType;
        l.x = o.position.x;
        l.y = o.position.y;
        l.z = o.position.z;
        l.heading = o.heading;
        auto it = objects_.find(drop_id);
        if (it != objects_.end() && it->second.model) l.height = it->second.model->shape.hi.z * it->second.scale;
        return l;
    }
    return std::nullopt;
}

} // namespace hailstone::app
