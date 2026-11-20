#pragma once

// The tradeskill window (CTradeSkillWnd, EQUI_TradeskillWnd.xml): the recipes a tradeskill container can make, as
// EQEmu's recipe packets give them (net/protocol/rof2/packets/door_packets.h "recipes"), one recipe's components, favourites and the
// auto combine (CTradeSkillWnd::WndNotification @0x77e7f0, HandleCombine @0x77e100, the button states of
// sub_0077c9b0, the favourites of CTradeSkillWnd_LoadFavoritesFromIni @0x77c0c0 / SaveFavoritesToIni @0x77b7a0).

#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "game/inventory.h"
#include "net/messages/door.h"
#include "net/messages/item.h"

namespace hailstone::game {

// ItemBase_IsCombineContainer @0x7b0300: a tradeskill container (CContainerWnd's ContainerType 2, Combine).
bool is_combine_container(std::uint32_t bag_type);

// IsTradeskillContainerType @0x77b710: the container types CContainerMgr::OpenContainer may show in the tradeskill
// window (9..0x36 less a few).
bool is_tradeskill_container_type(std::uint32_t bag_type);

// PcZoneClient_GetContainerTradeskill @0x580cc0: the tradeskill (55..69) a container of this type gives the
// character, or -1. The item's class, deity and race masks (0: any) must take the character (else 5942 / 5943 / 5944,
// not said for a type 13 or 30 container); some types need a class or a race (else the type's own line). `message`
// is the line said (colour 0xd), 0 none.
struct ContainerSkill {
    std::int32_t skill = -1;
    std::uint32_t message = 0;
};
struct ContainerUser {
    std::uint8_t class_id = 0; // the profile's class byte
    std::int32_t race = 0;
    std::int32_t deity = 0;
    // A server extension's (game/server_ext.h research_classes): the research containers' class tests take any of
    // these classes (bit c-1); 0 the class byte alone (a server patch's stubs; the projecttriune plugin switches it on).
    std::uint32_t research_classes = 0;
};
ContainerSkill container_tradeskill(std::uint32_t bag_type, std::uint32_t item_classes, std::uint32_t item_deities,
                                    std::uint32_t item_races, const ContainerUser& user);

// CContainerMgr::OpenContainer(item, where, tradeskill) @0x65cae0: whether the container shows in the tradeskill
// window instead of its own. `tradeskill` is what CInvSlot::HandleRButtonUp passes: TradeskillWnd CtrlBypass (0xde0d59)
// != ctrl held; the world container passes 1 (CContainerMgr::OpenWorldContainer). Only while the tradeskill window
// is not already up, for a top-level general slot (23..32) or the world container, a tradeskill container type and a
// tradeskill the character has (GetContainerTradeskill not -1 / 0xff, CharacterZoneClient::HasSkill: its adjusted
// value above 0).
struct OpenContainerInput {
    bool tradeskill = false;
    bool tradeskill_window_active = false;
    bool top_level_or_world = false;
    std::uint32_t bag_type = 0;
};
// `has_skill` asks GetContainerTradeskill and HasSkill, only when the first checks pass (it may say a line).
bool opens_in_tradeskill_window(const OpenContainerInput& in, const std::function<bool()>& has_skill);
// CInvSlot::HandleRButtonUp's argument: the tradeskill window by default (CtrlBypass 1), ctrl for the container;
// CtrlBypass 0 swaps them.
inline bool container_tradeskill_wanted(bool ctrl_bypass, bool ctrl) { return ctrl_bypass != ctrl; }

// Hailstone: Combine N / Combine All (not the original's: its window combines once per click). The combines run one
// after another through the window's own auto combine, each result put away before the next; it stops when the
// count is made, the components run out, the inventory has no room or anything else changes.
struct CombineBatch {
    std::uint32_t recipe_id = 0;
    std::uint32_t wanted = 0;           // 0: until the components run out
    std::uint32_t done = 0;             // combines the server took (made or failed)
    std::uint32_t result_id = 0;        // the last item put away from the cursor (a stack of it is room too)
    std::int64_t next_ms = 0, sent_ms = 0;
    std::int64_t collect_until_ms = 0;  // cursor items until then are the combine's (put away); later, the player's
};

struct TradeskillState {
    bool open = false;
    // The container: a bag (its slot, bag type and item id) or the open world container (its type, id 0).
    bool world = false;
    net::ItemSlot container_slot = net::ItemSlot::invalid();
    std::uint32_t object_type = 0, container_id = 0;
    std::string container_name;
    std::uint32_t container_icon = 0;

    std::vector<net::RecipeReply> recipes; // the last favourites or search answer, in arrival order
    std::uint32_t selected = 0;            // the selected recipe's id (0 none)
    std::optional<net::RecipeDetails> details; // its components, once they came
    bool combining = false;                // an auto combine waits for its answer (IsItemPending)
    // +0x444: the search and favourites buttons wait until then (5 s after each request); +0x36c +4: the combine
    // button until then (2.1 s after a combine). Milliseconds of GameConnection::now_ms().
    std::int64_t request_ready_ms = 0, combine_ready_ms = 0;

    std::optional<CombineBatch> batch;     // Hailstone's repeated combine, while it runs

    const net::RecipeReply* selected_recipe() const;
    // A recipe answer for this container (others are dropped): added once.
    void add_recipe(const net::RecipeReply& r);
};

// The favourites (+0x450, 500 recipe ids, 0 = an empty place), kept in the character's UI ini, [TradeskillWnd]
// Favorites: the ids in order, each followed by ",". Read once (the first favourites request, +0xc20): a value
// starting with '*' (none: the default "***") empties them; else each place takes the next token (GetDelimitedToken
// on ','), "0x..." in hex, else atoi; a place whose text starts with '0' is 0 without taking a token (so a "0x.."
// token, and everything after it, reads as 0).
inline constexpr int kRecipeFavorites = 500;
using RecipeFavorites = std::array<std::uint32_t, kRecipeFavorites>;
RecipeFavorites parse_recipe_favorites(std::string_view value);
std::string format_recipe_favorites(const RecipeFavorites& favorites);
inline bool is_recipe_favorite(const RecipeFavorites& f, std::uint32_t id) {
    for (std::uint32_t r : f)
        if (r != 0 && r == id) return true;
    return false;
}
// Add Favorite (sub_0077bf60): into the first empty place (none: nothing); Remove (sub_0077c010): every place
// holding it. True when something changed (then saved, eqstr 3458 / 3459 with the recipe's name, colour 15).
bool add_recipe_favorite(RecipeFavorites& f, std::uint32_t id);
bool remove_recipe_favorite(RecipeFavorites& f, std::uint32_t id);

// The trivial column's colour (sub_0077bd30, CTradeSkillWnd_UpdateRecipeTrivialColors @0x77bef0): trivial - skill
// against 0, 10, 25, 50, 500 gives green, light blue, blue, yellow, red; above, red. ARGB.
std::uint32_t recipe_trivial_color(std::int32_t trivial, std::int32_t skill);

// Hailstone: what a recipe takes and what the character carries of it, for the window's green / red components
// and its "can make" count. EQEmu's HandleAutoCombine looks in the general slots and their bags (HasItem with
// invWherePersonal: not the worn slots, the cursor or the bank). A stack counts its items, anything else one
// (EQEmu counts a non-stackable item's charges, but takes the whole item for each one it needs).
std::uint32_t carried_count(const Inventory& inventory, std::uint32_t item_id);
struct ComponentNeed {
    std::uint32_t item_id = 0, icon = 0;
    std::string name;
    std::uint32_t need = 0, have = 0;
};
struct RecipeNeeds {
    std::vector<ComponentNeed> components; // one per item, in the order the details list them
    // The details name every component (SendTradeskillDetails lists at most 10: a recipe of more, by the reply's
    // component count, needs more than shown and `makeable` is at most what it says).
    bool complete = true;
    std::uint32_t makeable = 0;
};
// SendTradeskillDetails lists an item once per one needed. `component_count` is the RecipeReply's (EQEmu: the sum
// of the recipe's component counts; 0 unknown).
RecipeNeeds recipe_needs(const net::RecipeDetails& details, std::uint32_t component_count, const Inventory& inventory);

// Hailstone: whether a combine's result has somewhere to go without the worn slots: an empty general slot the
// character has (`slot_available`), an empty place in a bag in one, or a stack of `result_id` (0 unknown) with
// room. A bag's place may still be too small for the result; putting it away says so then.
bool has_room_for_result(const Inventory& inventory, const std::function<bool(int)>& slot_available,
                         std::uint32_t result_id);
// Hailstone: where the cursor item goes when a combine batch puts it away: a stack of it with room in the general
// slots or their bags, else an empty general slot, else the first bag that takes it (game::bag_fit) with an empty
// place; never a worn slot (AutoInventoryCursorItem may equip it). Nullopt: nowhere. `merge` says a stack takes it.
struct PutAway {
    net::ItemSlot to;
    bool merge = false;
};
std::optional<PutAway> put_away_slot(const Inventory& inventory, const net::Item& held,
                                     const std::function<bool(int)>& slot_available);

// The requests' chat lines (colour 0x10e) and waits.
inline constexpr const char* kRequestingRecipes = "Requesting matching recipes...";
inline constexpr const char* kRequestingFavorites = "Requesting favorite recipes...";
inline constexpr std::int64_t kRecipeRequestWaitMs = 5000, kCombineWaitMs = 2100;
// Hailstone's combine batch: after the server's answer, the wait for its items (the result comes after it, on the
// cursor); between two put-away moves; how long after the answer (or the last put-away) a cursor item is taken for
// the combine's (one the player picks up later stops the batch instead); how long an answer may take.
inline constexpr std::int64_t kBatchSettleMs = 400, kBatchStepMs = 100, kBatchCollectMs = 1500,
                              kBatchAnswerTimeoutMs = 10000;

} // namespace hailstone::game
