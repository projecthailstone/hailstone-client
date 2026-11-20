#pragma once

// Placed real estate objects in the world, and item placement mode as ItemPlacement keeps it (the singleton at
// ItemPlacement_GetInstance @0x558540; EQPlacedItem_CreateActor @0x504b60, the 0x626e reply @0x4d058a, ItemPlacement_SetMode @0x557330, its predicate
// 0x5ae160, ItemPlacement_ResetRotation @0x558970, ItemPlacement_AdjustScale @0x558a70 / ResetScale @0x558b30,
// 0x558820's 90-degree turns, the listener's validation 0x5ae010 and AreaManager::Query @0x4b2bf0, and
// Send_Opcode_0x711a @0x5b1f90). Pure logic: no network, no Vulkan.

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "net/messages/real_estate.h"

namespace hailstone::game {

// The actor's position: EQPlacedItem keeps +0x8c..+0x94 in CDisplay::CreateActor's order, the actor's own (world x,
// y, z; a door's (Y, X, Z) is passed the same way).
inline glm::vec3 placed_world_position(const net::RePlacedItem& o) { return o.server_pos; }
// The actor's transform: translated to its position, turned by (heading, pitch, roll) through the actor's vf74
// (CActorData::SetEulerRotation512, as for doors and ground objects: Rz(heading) Ry(-pitch) Rx(roll), 0..512),
// scaled by +0x7c (0x504830). A scale that is not a positive number draws at 1.
glm::mat4 placed_transform(const net::RePlacedItem& o);
// The actor flags: collision off when +0x98 is set (the flags | 5); the client sets it for NPC items.
inline bool placed_solid(const net::RePlacedItem& o) { return !o.no_collision && !o.npc_item; }
// CEverQuest::RMouseUp's test before it asks for a placed object's menu: the actor within the player's height + 50 +
// the actor's radius (CDisplay distance with z), or its estate the current one.
bool placed_menu_in_reach(const glm::vec3& actor, float actor_radius, const glm::vec3& player, float player_height,
                          std::uint32_t estate, std::uint32_t current_estate);
// The same for a real estate NPC item's spawn (0x21ea): within 60, or its estate the current one.
bool npc_item_menu_in_reach(const glm::vec3& spawn, const glm::vec3& player, std::uint32_t estate,
                            std::uint32_t current_estate);

// Placement modes (ItemPlacement +0x04): the key commands set 0 collision, 1 wall, 2 cursor, 3 free; Toggle Mode
// steps 0..5 to the next allowed one. 4 and 5 are never allowed (0x5ae160).
enum class PlaceMode : std::int32_t { Collision = 0, Wall = 1, Cursor = 2, Free = 3 };
// 0x5ae160: collision and cursor unless the object is wall-only and allows walls; wall only when it allows walls
// (+0x9c == 1); free unless +0x9a or an NPC item.
bool place_mode_allowed(int mode, const net::RePlacedItem& o);
// The eqstr ItemPlacement_ShowModeChanged names (5266 collision, 5286 wall, 5267 cursor, 5268 free).
std::uint32_t place_mode_name(int mode);

// AreaManager::Query @0x4b2bf0: the 8 corners of the object's box (its local bounds turned, scaled and moved as the
// actor is) against the areas whose ids the reply's map holds: an area that holds all 8 corners with code 0 allows
// it at once; else the last such area's code; none: the root estate's code, else 1.
std::uint32_t place_query(const std::vector<net::ReArea>& areas,
                          const std::unordered_map<std::uint32_t, std::uint32_t>& codes, std::uint32_t root,
                          const glm::mat4& transform, const glm::vec3& lo, const glm::vec3& hi);

class Placement {
public:
    bool active() const { return active_; } // +0x08
    // BeginPlacement from the 0x626e reply: the ghost (a copy of the object), its index, the flags, the codes; the
    // first allowed mode from cursor (the original's choice of the start mode is not traced: docs/unknowns.md).
    void begin(const net::RePlaceReply& reply);
    // ItemPlacement_Stop: the session over (a commit on 0x1228 {0}, 0x6213, a zone change).
    void stop() { *this = Placement(); }

    const net::RePlacedItem& ghost() const { return ghost_; }
    const net::ReItemIndex& index() const { return index_; }
    int mode() const { return mode_; }
    std::uint32_t code() const { return code_; } // +0x0c: 0 valid (the ghost green), else red
    const std::unordered_map<std::uint32_t, std::uint32_t>& codes() const { return codes_; }

    // ItemPlacement_SetMode: -1 steps to the next allowed mode; a mode not allowed is refused (false: the caller
    // prints ItemPlacement_ShowModeNotAllowed's eqstr 5270 with the mode's name).
    bool set_mode(int mode);
    // 0x558820 (the 90-degree keys pass 128 on one axis): adds to (heading, pitch, roll), each kept in 0..512
    // (the body of 0x558820 is not traced: the wrap is this client's).
    void rotate(float heading, float pitch, float roll);
    // ItemPlacement_ResetRotation(x, y, wheel) (the three reset keys): outside wall mode x sets the heading to
    // +0xb0, y the pitch to +0xb4, the roll keeps its value. In wall mode only the wheel key acts, on the heading or
    // the pitch by 0x5043e0 (not traced): nothing here.
    void reset_rotation(bool heading, bool pitch, bool roll);
    // ItemPlacement_AdjustScale: not for NPC items; clamped to [+0xa0, +0xa4]. ResetScale: +0xa8.
    void adjust_scale(float delta);
    void reset_scale();
    // Where the ghost is (the app moves it with the mouse, as UpdatePosition places the actor), world coordinates.
    void move_to(const glm::vec3& world);
    // The listener's validation (0x5ae010's area query; its surface and player-overlap tests are the app's):
    // recomputes code() from the areas, the root estate and the ghost's local bounds.
    void validate(const std::vector<net::ReArea>& areas, std::uint32_t root, const glm::vec3& lo, const glm::vec3& hi);
    // A refusal the app found (0x1d not flat, 0x1c on a player) after the area query passed.
    void set_code(std::uint32_t code) { code_ = code; }

    // ItemPlacement_OnLeftMouseUp → Send_Opcode_0x711a: the confirm when the code is 0, else none.
    std::optional<net::RePlacementIntent> confirm(const glm::vec3& box_a, const glm::vec3& box_b) const;

private:
    bool active_ = false;
    net::RePlacedItem ghost_;
    net::ReItemIndex index_;
    int mode_ = static_cast<int>(PlaceMode::Cursor);
    std::uint32_t code_ = 1;
    std::uint8_t b1_ = 0, b2_ = 0, b3_ = 0;
    std::uint32_t c1_ = 0, c2_ = 0, c3_ = 0;
    std::unordered_map<std::uint32_t, std::uint32_t> codes_;
};

} // namespace hailstone::game
