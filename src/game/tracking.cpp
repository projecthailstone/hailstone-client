#include "game/tracking.h"

#include <cstring>
#include <utility>

namespace hailstone::game {

namespace {

// MSVC's _stricmp (C locale): lower-cased unsigned bytes.
int stricmp_c(const std::string& a, const std::string& b) {
    std::size_t i = 0;
    for (;; ++i) {
        unsigned f = i < a.size() ? static_cast<unsigned char>(a[i]) : 0u;
        unsigned l = i < b.size() ? static_cast<unsigned char>(b[i]) : 0u;
        if (f >= 'A' && f <= 'Z') f += 'a' - 'A';
        if (l >= 'A' && l <= 'Z') l += 'a' - 'A';
        if (f == 0 || f != l) return static_cast<int>(f) - static_cast<int>(l);
    }
}

bool players_first(const TrackEntry& a, const TrackEntry& b, bool ascending) {
    if (a.flags & kTrackPlayer) return true;
    if (b.flags & kTrackPlayer) return false;
    return ascending ? a.level < b.level : b.level < a.level;
}

// MSVC <algorithm> (the version eqgame.exe was built with: std_Sort_TrackingEntry @0x77b3f0 and its helpers).
struct Sorter {
    std::span<TrackEntry> v;
    int order;
    bool less(std::size_t a, std::size_t b) const { return track_less(order, v[a], v[b]); }
    bool less(const TrackEntry& a, std::size_t b) const { return track_less(order, a, v[b]); }
    bool less(std::size_t a, const TrackEntry& b) const { return track_less(order, v[a], b); }
    void swap(std::size_t a, std::size_t b) { std::swap(v[a], v[b]); }

    void med3(std::size_t first, std::size_t mid, std::size_t last) {
        if (less(mid, first)) swap(mid, first);
        if (less(last, mid)) swap(last, mid);
        if (less(mid, first)) swap(mid, first);
    }
    void median(std::size_t first, std::size_t mid, std::size_t last) {
        std::ptrdiff_t count = static_cast<std::ptrdiff_t>(last - first);
        if (count > 40) {
            std::size_t step = static_cast<std::size_t>((count + 1) / 8);
            med3(first, first + step, first + 2 * step);
            med3(mid - step, mid, mid + step);
            med3(last - 2 * step, last - step, last);
            med3(first + step, mid, last - step);
        } else {
            med3(first, mid, last);
        }
    }
    std::pair<std::size_t, std::size_t> partition(std::size_t first, std::size_t last) {
        std::size_t mid = first + (last - first) / 2;
        median(first, mid, last - 1);
        std::size_t pfirst = mid, plast = pfirst + 1;
        while (first < pfirst && !less(pfirst - 1, pfirst) && !less(pfirst, pfirst - 1)) --pfirst;
        while (plast < last && !less(plast, pfirst) && !less(pfirst, plast)) ++plast;
        std::size_t gfirst = plast, glast = pfirst;
        for (;;) {
            for (; gfirst < last; ++gfirst) {
                if (less(pfirst, gfirst)) continue;
                if (less(gfirst, pfirst)) break;
                swap(plast++, gfirst);
            }
            for (; first < glast; --glast) {
                if (less(glast - 1, pfirst)) continue;
                if (less(pfirst, glast - 1)) break;
                swap(--pfirst, glast - 1);
            }
            if (glast == first && gfirst == last) return {pfirst, plast};
            if (glast == first) {
                if (plast != gfirst) swap(pfirst, plast);
                ++plast;
                swap(pfirst++, gfirst++);
            } else if (gfirst == last) {
                if (--glast != --pfirst) swap(glast, pfirst);
                swap(pfirst, --plast);
            } else {
                swap(gfirst++, --glast);
            }
        }
    }
    void insertion(std::size_t first, std::size_t last) {
        if (first == last) return;
        for (std::size_t next = first + 1; next != last; ++next) {
            TrackEntry val = std::move(v[next]);
            if (less(val, first)) {
                for (std::size_t k = next; k > first; --k) v[k] = std::move(v[k - 1]);
                v[first] = std::move(val);
            } else {
                std::size_t hole = next;
                while (less(val, hole - 1)) {
                    v[hole] = std::move(v[hole - 1]);
                    --hole;
                }
                v[hole] = std::move(val);
            }
        }
    }
    // Indices relative to `first`.
    void push_heap(std::size_t first, std::ptrdiff_t hole, std::ptrdiff_t top, TrackEntry&& val) {
        for (std::ptrdiff_t idx = (hole - 1) / 2; top < hole && less(first + static_cast<std::size_t>(idx), val);
             idx = (hole - 1) / 2) {
            v[first + static_cast<std::size_t>(hole)] = std::move(v[first + static_cast<std::size_t>(idx)]);
            hole = idx;
        }
        v[first + static_cast<std::size_t>(hole)] = std::move(val);
    }
    void adjust_heap(std::size_t first, std::ptrdiff_t hole, std::ptrdiff_t bottom, TrackEntry&& val) {
        const std::ptrdiff_t top = hole;
        std::ptrdiff_t idx = 2 * hole + 2;
        auto at = [&](std::ptrdiff_t i) { return first + static_cast<std::size_t>(i); };
        for (; idx < bottom; idx = 2 * idx + 2) {
            if (less(at(idx), at(idx - 1))) --idx;
            v[at(hole)] = std::move(v[at(idx)]);
            hole = idx;
        }
        if (idx == bottom) {
            v[at(hole)] = std::move(v[at(bottom - 1)]);
            hole = bottom - 1;
        }
        push_heap(first, hole, top, std::move(val));
    }
    void heap_sort(std::size_t first, std::size_t last) {
        std::ptrdiff_t bottom = static_cast<std::ptrdiff_t>(last - first);
        for (std::ptrdiff_t hole = bottom / 2; hole > 0;) {
            --hole;
            TrackEntry val = std::move(v[first + static_cast<std::size_t>(hole)]);
            adjust_heap(first, hole, bottom, std::move(val));
        }
        for (; last - first > 1; --last) {
            TrackEntry val = std::move(v[last - 1]);
            v[last - 1] = std::move(v[first]);
            adjust_heap(first, 0, static_cast<std::ptrdiff_t>(last - 1 - first), std::move(val));
        }
    }
    void sort(std::size_t first, std::size_t last, std::ptrdiff_t ideal) {
        std::ptrdiff_t count;
        for (; (count = static_cast<std::ptrdiff_t>(last - first)) > 32 && ideal > 0;) {
            auto mid = partition(first, last);
            ideal /= 2;
            ideal += ideal / 2;
            if (mid.first - first < last - mid.second) {
                sort(first, mid.first, ideal);
                first = mid.second;
            } else {
                sort(mid.second, last, ideal);
                last = mid.first;
            }
        }
        if (count > 32) heap_sort(first, last);
        else if (count > 1) insertion(first, last);
    }
};

} // namespace

bool track_less(int order, const TrackEntry& a, const TrackEntry& b) {
    switch (order) {
    case 0: return a.position < b.position;
    case 1: return stricmp_c(a.sort_name, b.sort_name) < 0;
    case 2: return b.distance > a.distance;
    case 3: return players_first(a, b, false);
    case 4: return stricmp_c(a.sort_name, b.sort_name) > 0;
    case 5: return b.distance < a.distance;
    case 6: return players_first(a, b, true);
    default: return false;
    }
}

void track_sort(std::span<TrackEntry> entries, int order) {
    Sorter s{entries, order};
    s.sort(0, entries.size(), static_cast<std::ptrdiff_t>(entries.size()));
}

std::uint32_t track_color(const TrackEntry& e, std::span<const std::int32_t> group_spawn_ids) {
    const std::uint32_t f = e.flags;
    if (f & kTrackPlayer) {
        for (std::int32_t id : group_spawn_ids)
            if (id == e.spawn_id) return 12;
        return 7;
    }
    if (f & kTrackGreen) return 2;
    if (f & kTrackBlue) return 4;
    if (f & kTrackLightBlue) return 18;
    if (f & kTrackGrey) return 6;
    if (f & kTrackWhite) return 10;
    return (f & kTrackYellow) ? 15 : 13;
}

TrackFrame tracking_frame(bool active, std::uint32_t now, std::uint32_t& next_controls, std::uint32_t& next_track,
                          bool auto_update) {
    TrackFrame out;
    if (!active) return out;
    if (now >= next_controls) {
        next_controls = now + kTrackControlsMs;
        out.refresh_controls = true;
    }
    if (auto_update && now >= next_track) {
        next_track = now + kTrackRefreshMs;
        out.send_track = true;
    }
    return out;
}

std::uint32_t track_con_bit(std::uint32_t standard_color) {
    switch (standard_color) {
    case 1: return kTrackGrey;
    case 2: return kTrackGreen;
    case 3: return kTrackLightBlue;
    case 4: return kTrackBlue;
    case 5: return kTrackWhite;
    case 6: return kTrackYellow;
    case 7: return kTrackRed;
    default: return 0;
    }
}

void track_filter(TrackEntry& e, std::uint32_t standard_color, bool ranger, const TrackFilter& f,
                  bool (*in_group)(const void* ctx, std::int32_t id), std::int32_t pet_owner, const void* ctx) {
    if (e.spawn_id == 0) return;
    // The low byte cleared (0x80 too), then the con bit.
    e.flags &= ~0xffu;
    e.flags |= track_con_bit(standard_color);
    bool hidden = ranger && !(e.flags & kTrackPlayer) && (f.con_mask & e.flags) != 0;
    auto grouped = [&](std::int32_t id) { return in_group && in_group(ctx, id); };
    if (e.flags & kTrackPlayer) {
        if (f.players == 0) hidden = true;
        else if (f.players == 1) hidden = false;
        else if (f.players == 2 && !grouped(e.spawn_id)) hidden = true;
    }
    if (e.flags & kTrackPet) {
        hidden = !f.pets;
        if (f.players == 0) hidden = true;
        else if (f.players == 2 && !(pet_owner != 0 && grouped(pet_owner))) hidden = true;
    }
    if (e.flags & kTrackMerc) {
        hidden = hidden || !f.mercs;
        if (f.players == 0) hidden = true;
        else if (f.players == 2 && !grouped(e.spawn_id)) hidden = true;
    }
    if (hidden) e.flags |= kTrackHidden;
}

std::string track_line(const TrackEntry& e) {
    std::string name = e.sort_name.empty() ? e.name : e.sort_name;
    return (e.flags & (kTrackPet | kTrackMerc)) ? ":" + name : name;
}

bool stock_tracking_allowed(std::uint8_t, std::uint32_t, std::uint32_t tracking_skill) { return tracking_skill > 0; }

TrackingAllowedFn& tracking_allowed() {
    static TrackingAllowedFn fn = &stock_tracking_allowed;
    return fn;
}

} // namespace hailstone::game
