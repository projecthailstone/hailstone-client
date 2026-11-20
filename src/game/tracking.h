#pragma once

// The tracking window's list (CTrackingWnd): follows the original's list functions
// and the OP_Track reply parser SetTrackingList @0x77a930 with the std::sort it runs, checked against
// TrackSort.jsonl, CTrackingWnd_GetTrackColor.jsonl, CTrackingWnd_OnProcessFrame.jsonl and
// CTrackingWnd_SetTrackingList.jsonl.
//
// OP_Track (0x17e5) has no payload; the client sends it when the window shows, every 6 s with auto update on, and
// from UseSkill(53). The reply (HandleWorldMessage @0x4c623c; a first dword of 0xff8b344f is a timer, not a list):
// u16 count, then per entry u32 spawn id, f32 distance, u8 level, u8 is_npc (0: a player), the name with its NUL,
// u8 is_pet, u8 is_merc. The window keeps 1000 entries of 0x54 bytes; the list shows the trimmed name only, with
// ':' before pets and mercenaries; there is no distance column and no direction. Track sends 0x695e {spawn id}.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hailstone::game {

inline constexpr std::uint32_t kTrackPlayer = 0x100, kTrackPet = 0x200, kTrackMerc = 0x400, kTrackHidden = 0x80;
// The con colour bits DoTrackFilter sets (and the ranger's filter mask uses).
inline constexpr std::uint32_t kTrackGreen = 0x01, kTrackBlue = 0x02, kTrackWhite = 0x04, kTrackYellow = 0x08,
                               kTrackRed = 0x10, kTrackLightBlue = 0x20, kTrackGrey = 0x40;
inline constexpr int kTrackEntries = 1000;
inline constexpr std::uint32_t kTrackReplyTimer = 0xff8b344fu;
inline constexpr std::uint32_t kTrackRefreshMs = 6000, kTrackControlsMs = 500;

// One window entry (+0 spawn id, +4 name, +0x44 level, +0x48 distance, +0x4c reply position, +0x50 flags).
struct TrackEntry {
    std::int32_t spawn_id = 0;
    std::string name;            // as received, at most 63 characters
    std::string sort_name;       // CEverQuest::trimName(name): what the name orders compare
    std::uint8_t level = 0;      // kept from the previous reply when the entry is reset
    float distance = -1.0f;
    std::int32_t position = -1;  // the entry's index in the reply; -1 unused
    std::uint32_t flags = 0;
};
using TrackList = std::array<TrackEntry, kTrackEntries>;

// The seven sort predicates (std::sort's less; 0x779080..00779190): 0 reply order; 1 names A..Z; 2 nearest
// first; 3 players first, then level high to low; 4 names Z..A; 5 farthest first; 6 players first, then level low
// to high (a player `a` is always less, even against a player). Names compare as _stricmp.
bool track_less(int order, const TrackEntry& a, const TrackEntry& b);
// std::sort as eqgame.exe's (MSVC) runs it over the 1000 entries: introsort (insertion sort up to 32, the median
// of nine above 40, heap sort when the depth runs out), so entries the predicate does not order land where the
// original puts them.
void track_sort(std::span<TrackEntry> entries, int order);

// RoF2 tracking record decoding lives in protocol/rof2/packets/tracking_packets.*.
// This module retains protocol-neutral sort, con-colour and filtering rules.
// CTrackingWnd::GetTrackColor: the chat colour of an entry: a player 12 in our group, else 7; otherwise by the
// first con bit of 0x01 (2), 0x02 (4), 0x20 (18), 0x40 (6), 0x04 (10), then 15 with 0x08, else 13.
std::uint32_t track_color(const TrackEntry& e, std::span<const std::int32_t> group_spawn_ids);

// CTrackingWnd::OnProcessFrame: while the window is active the controls refresh every 500 ms and, with auto update
// on, OP_Track goes out every 6000 ms (unsigned clock compares, as CDisplay +0x154).
struct TrackFrame {
    bool refresh_controls = false;
    bool send_track = false;
};
TrackFrame tracking_frame(bool active, std::uint32_t now, std::uint32_t& next_controls, std::uint32_t& next_track,
                          bool auto_update);

// DoTrackFilter @0x77a170's settings.
struct TrackFilter {
    std::uint32_t con_mask = 0;   // 0xddfcbc: the con colours a ranger hides (/trackfilter)
    std::int32_t players = 1;     // 0xddfcb0 (/trackplayers): 0 off, 1 all, 2 group
    bool pets = true;             // 0xddfcb5 (/trackpets)
    bool mercs = true;            // 0xddfcb4 (/trackmercs)
};
// The con bit of 0x7be930's colour (1 grey .. 7 red; else none).
std::uint32_t track_con_bit(std::uint32_t standard_color);
// DoTrackFilter for one used entry: the con bit replaces the low 7 bits, 0x80 when the filters hide it. `in_group`
// answers whether a spawn id is a group member's; `pet_owner` gives a pet's owner id (0 when unknown).
void track_filter(TrackEntry& e, std::uint32_t standard_color, bool ranger, const TrackFilter& f,
                  bool (*in_group)(const void* ctx, std::int32_t id), std::int32_t pet_owner, const void* ctx);

// The list line: the trimmed name, ':' before a pet's or mercenary's.
std::string track_line(const TrackEntry& e);

// The skill check before tracking (UseSkill(53) and the window): whether the character has Tracking. A function
// pointer so a server's rules can replace it (NMS: Ranger, Druid or Bard in the class mask); stock: the skill.
using TrackingAllowedFn = bool (*)(std::uint8_t class_id, std::uint32_t class_mask, std::uint32_t tracking_skill);
bool stock_tracking_allowed(std::uint8_t class_id, std::uint32_t class_mask, std::uint32_t tracking_skill);
TrackingAllowedFn& tracking_allowed();

} // namespace hailstone::game
