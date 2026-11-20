#pragma once

// The real estate manager as RealEstateManagerClient keeps it (0x5b2500, RealEstateManagerClient_Process @0x5b0a10 and its resolvers 0x5affc0 / 0x5b00e0 /
// 0x5b0140 / 0x4b2f40 / 0x4b30f0 / 0x4d6790 / 0x5b45b0): the definition catalog and its cost sets, the estate
// records, their access tables, the item rows the server feeds per estate, the local character's owned-estate lists,
// the areas, the placed objects, and which estate the player stands in (occupancy). No network and no windows here:
// net/connection/game_connection_real_estate.cpp feeds it and the UI reads it.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "net/messages/real_estate.h"

namespace hailstone::game {

inline constexpr std::uint32_t kReNone = 0xffffffffu;

// TypeToBitmask @0x7f5e30: 1 << (type - 1) (0 for type 0).
constexpr std::uint32_t re_type_bit(net::ReType t) {
    auto v = static_cast<std::uint32_t>(t);
    return v == 0 || v > 31 ? 0 : 1u << (v - 1);
}
inline constexpr std::uint32_t kReOwnerMask = 0x88; // EmptyFunction_007f62b0: plots and guild plots
inline constexpr std::uint32_t kReCrateMask = 0x40; // 0x7f62c0
// RealEstate::IsOwnerType @0x7f6030 (4, 8); 0x7f60f0 houses (2, 3); 0x7f61b0 crates (7); 0x7f6280 guild plots (8);
// 0x7f5f70's group test, true only for neighborhoods (5).
constexpr bool re_is_owner_type(net::ReType t) { return t == net::ReType::Plot || t == net::ReType::GuildPlot; }
constexpr bool re_is_house_type(net::ReType t) { return t == net::ReType::GuildHall || t == net::ReType::House; }
constexpr bool re_is_crate_type(net::ReType t) { return t == net::ReType::Crate; }
constexpr bool re_is_neighborhood_type(net::ReType t) { return t == net::ReType::Neighborhood; }

// The item rows the server sent for one estate (map B, 0x087f / 0x1cef / 0x2cf7).
struct ReItemSummary {
    std::string name;
    std::vector<net::ReItemRow> rows;
};

// Which estate the player is in (mgr +0x8c .. +0xa0).
struct ReOccupancy {
    std::uint32_t root = kReNone;      // +0x8c, only 0x6fd0 sets it
    net::ReType root_type = net::ReType::Unknown; // +0x90
    std::uint32_t current = kReNone;   // +0x94: from the areas, else the root
    std::uint32_t yard = kReNone;      // +0x98: the nearest plot or guild plot at or above current
    std::uint32_t house = kReNone;     // +0x9c
    std::uint32_t crate = kReNone;     // +0xa0
};

class RealEstateManager {
public:
    // 0x2698: the catalog replaced (definitions and cost sets).
    void set_catalog(net::ReCatalog catalog);
    const net::ReDefinition* definition(std::uint32_t id) const;
    // DefinitionTypeOf @0x7f4fa0: Unknown (10) for a missing definition.
    net::ReType definition_type(std::uint32_t definition) const;
    const net::ReCostSet* cost_set(std::uint32_t id) const;
    const std::vector<net::ReDefinition>& definitions() const { return catalog_.definitions; }

    // 0x63fd / 0x6fd0: add or replace (mgr vt+0x24); 0x6fd0 also sets the root and its type first.
    void add_record(net::ReRecord record);
    void set_current(net::ReRecord record);
    // 0x4f09: forget it (mgr vt+0x28).
    void remove_record(std::uint32_t id);
    // 0x2691 form B: a known id only gets its owner and guild; an unknown one is made from the fields.
    void merge_search_record(const net::ReRecord& record);
    const net::ReRecord* record(std::uint32_t id) const;
    const std::unordered_map<std::uint32_t, net::ReRecord>& records() const { return records_; }
    net::ReType record_type(std::uint32_t id) const;
    // 0x5b41d0: a plot is owned when its owner is set, a guild plot when its guild is.
    bool already_owned(std::uint32_t id) const;

    // 0x3968 / 0x485d.
    void set_access(std::uint32_t re, net::ReAccess access) { access_[re] = std::move(access); }
    const net::ReAccess* access(std::uint32_t re) const;

    // Map B.
    void add_item_rows(const net::ReItemList& list);
    void rename_summary(std::uint32_t re, std::string name);
    void remove_item(std::uint32_t re, std::uint32_t item_id);
    void forget_summary(std::uint32_t re) { summaries_.erase(re); }
    const ReItemSummary* summary(std::uint32_t re) const;
    const std::map<std::uint32_t, ReItemSummary>& summaries() const { return summaries_; }

    // The local character's owned-estate lists (PcBase +0x29ac / +0x29bc / +0x29cc): replaced by 0x2aa7 / 0x3e00 /
    // 0x66df, A and B toggled by 0x76c6 / 0x0c13.
    enum class OwnedList { A = 0, B = 1, C = 2 };
    void set_owned(OwnedList list, const std::vector<std::uint32_t>& ids);
    void toggle_owned(OwnedList list, std::uint32_t id, bool add);
    const std::vector<std::uint32_t>& owned(OwnedList list) const { return owned_[static_cast<int>(list)]; }

    // 0x49df: the areas replaced (AreaManager::Clear then DeserializeAll).
    void set_areas(std::vector<net::ReArea> areas) { areas_ = std::move(areas); }
    const std::vector<net::ReArea>& areas() const { return areas_; }
    // EQAreaBase::PointInVolume @0x505f60: the point in the inverse of the area's matrix (row vectors) is within
    // [-1, 1] on each axis.
    static bool point_in_area(const net::ReArea& area, const glm::vec3& point);

    // Placed objects (EQPlacedItemManager): keyed by {re, item}.
    // 0x1045: a new key adds one (true), a known one takes the new data (false: the original keeps its actor).
    bool add_placed(const net::RePlacedItem& item);
    void remove_placed(std::uint32_t re, std::uint32_t item_id);
    const net::RePlacedItem* placed(std::uint32_t re, std::uint32_t item_id) const;
    const std::vector<net::RePlacedItem>& placed_items() const { return placed_; }

    // RealEstateManagerClient_Process: every second (when records exist) the current estate from the areas at
    // `server_pos` (none: the root), then the house, yard and crate. Without records all four follow the root at
    // once. `has_player`: false gives the root (GetCurrentParentId with no player).
    void process(std::uint32_t now_ms, bool has_player, const glm::vec3& server_pos);
    const ReOccupancy& occupancy() const { return occ_; }

    // The request-pending latch (+0xa4 / +0xa8): set by a request, released by its answer or after 5 s.
    void set_pending(std::uint32_t now_ms) { pending_ = true, pending_at_ = now_ms; }
    void clear_pending() { pending_ = false; }
    bool pending() const { return pending_; }

    // Everything forgotten (a zone change keeps nothing: the server sends it again on arrival).
    void reset();

    // The record or the nearest parent whose type is in `mask`, within 10 steps (so a parent cycle ends); or null.
    const net::ReRecord* find_up(const net::ReRecord* r, std::uint32_t mask) const;

private:
    std::uint32_t area_estate(std::uint32_t area_id, std::uint32_t root) const;
    bool descends_from(std::uint32_t id, std::uint32_t ancestor) const;
    const net::ReRecord* find_down(const net::ReRecord* r, std::uint32_t mask, int skip, int depth) const;

    net::ReCatalog catalog_;
    std::unordered_map<std::uint32_t, net::ReRecord> records_;
    std::unordered_map<std::uint32_t, net::ReAccess> access_;
    std::map<std::uint32_t, ReItemSummary> summaries_;
    std::vector<std::uint32_t> owned_[3];
    std::vector<net::ReArea> areas_;
    std::vector<net::RePlacedItem> placed_;
    ReOccupancy occ_;
    std::uint32_t last_tick_ = 0;
    bool ticked_ = false;
    bool pending_ = false;
    std::uint32_t pending_at_ = 0;
};

// RealEstate::ShowResultCode @0x5ae1f0: the eqstr id for a 0x1228 code, 0 for none (success, -1 and codes it does not
// list print nothing).
std::uint32_t re_result_message(std::int32_t code);
// 0x5ad930 (0x66bf, sell / move out) and 0x5adad0 (0x68ba, transfer): 0 for success.
std::uint32_t re_sell_message(std::int32_t code);
std::uint32_t re_transfer_message(std::int32_t code);

// CRealEstateManageWnd_RefreshUpkeepList @0x742cd0 and RealEstate_FormatTimeRemaining @0x740a50: the days the escrow
// lasts, the least over the currencies of balance / daily cost (the record's upkeep set within the definition's upkeep
// group). Negative: unlimited (no cost); 0: expired.
double re_upkeep_days(const RealEstateManager& m, const net::ReRecord& r);
// RealEstate_FormatTimeRemaining @0x740a50 ("Unlimited", "Expired", "1w 1d", "3d", "<1d").
std::string re_format_time_left(double days);

} // namespace hailstone::game
