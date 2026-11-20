#include "game/zoning.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/msvc_atoi.h"

namespace hailstone::game {

namespace {

// __ftol2: truncation; NaN and out-of-range values give 0x80000000.
std::int32_t ftol(double v) {
    if (!(v > -2147483649.0 && v < 2147483648.0)) return static_cast<std::int32_t>(0x80000000u);
    return static_cast<std::int32_t>(v);
}

// The original copies a field into a NUL-terminated buffer and runs atol / atoi / atof on it.
std::string field(std::string_view text, std::size_t at, std::size_t n) {
    return at < text.size() ? std::string(text.substr(at, n)) : std::string();
}

} // namespace

bool repop_clears_coin(std::int32_t pvp_rules, std::int32_t level, bool virtual_0c, bool flag) {
    std::int32_t threshold = virtual_0c && !flag ? 150 : 5;
    if (pvp_rules == 6) threshold = 0;
    return threshold < level || flag;
}

std::string atp_teleport_text(std::string_view region_name) {
    std::string rest = region_name.size() > 4 ? std::string(region_name.substr(4)) : std::string();
    char text[64];
    std::snprintf(text, sizeof(text), "DRNTP00255%05d000000000000000", core::msvc_atoi(rest));
    return text;
}

std::uint32_t through_fpu(std::uint32_t bits) {
    bool nan = (bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0;
    return nan ? bits | 0x00400000u : bits;
}

ResolvedTeleport resolve_teleport_index(std::string_view text, std::int32_t index, std::int32_t current_zone,
                                        float location[3], std::span<const ZonePointRow> zone_points) {
    ResolvedTeleport r;
    const std::size_t len = text.size();
    std::int32_t zone = 0;
    if (index < 0) {
        if (len > 9) {
            zone = static_cast<std::int32_t>(std::atol(field(text, 5, 5).c_str()));
            if (len > 15) {
                std::string f = field(text, 10, 6);
                r.x = static_cast<float>(std::strtod(f.c_str(), nullptr));
                r.zone_point = core::msvc_atoi(f);
                if (len > 21) {
                    r.y = static_cast<float>(std::strtod(field(text, 16, 6).c_str(), nullptr));
                    if (len > 27) {
                        r.z = static_cast<float>(std::strtod(field(text, 22, 6).c_str(), nullptr));
                        if (len > 30) r.heading = static_cast<float>(std::strtod(field(text, 28, 3).c_str(), nullptr));
                    }
                }
            }
        }
    } else {
        r.zone_point = index;
        zone = 0xff;
    }
    if (zone == 0xff) {
        // 0x513420: the first row with that number gives the position, the zone and the request dword;
        // without one, the position is zeroed and the zone is -1, and the teleport fails.
        const ZonePointRow* row = nullptr;
        for (const auto& z : zone_points)
            if (z.number == r.zone_point) {
                row = &z;
                break;
            }
        if (!row) {
            r.x = r.y = r.z = r.heading = 0.0f;
            return r;
        }
        r.x = row->x;
        r.y = row->y;
        r.z = row->z;
        r.heading = row->heading;
        zone = row->zone;
        r.index_request = row->index_request;
    }
    r.ok = true;
    r.found = zone;
    r.camera_flag = true;
    r.same_zone = zone == current_zone;
    r.destination = zone;
    if (current_zone == 0x22) {
        if (zone == 10) r.y = location[1] = location[1] - 1044.0f;
    } else if (current_zone == 10) {
        if (zone == 0x22) r.y = location[1] = location[1] + 1044.0f;
    } else if (current_zone == 3) {
        if (zone == 4) r.z = location[2] = location[2] + 2.0f;
    } else if (current_zone == 2 && zone == 4) {
        r.x = r.x + 10.0f;
    }
    return r;
}

bool do_teleport(DoTeleportInput& in, const DoTeleportHooks& h) {
    if (!in.is_local_player) {
        if (in.virtual_6c && in.virtual_90) h.remove_affect(h.get_spell_affect(0x43), 0);
        return false;
    }
    bool has_description = false;
    ResolvedTeleport t = h.resolve(in.index, has_description);
    auto release = [&] {
        if (has_description) h.free_description();
    };
    if (!t.ok || t.destination == 0) {
        release();
        return false;
    }
    h.position_sample(4, in.point);
    std::int32_t request = t.index_request;
    if (in.waiting_for_port != 0) {
        in.waiting_for_port = 0;
        request = 0;
    } else if (request != 0 && t.zone_point != -1) {
        h.send_index_request(t.zone_point);
        release();
        return true;
    }
    if (h.teleport_b(t.found, through_fpu(std::bit_cast<std::uint32_t>(t.x)), through_fpu(std::bit_cast<std::uint32_t>(t.y)),
                     through_fpu(std::bit_cast<std::uint32_t>(t.z)), through_fpu(std::bit_cast<std::uint32_t>(t.heading)))) {
        release();
        return true;
    }
    h.position_sample(5, in.point);
    release();
    return false;
}

bool do_teleport_b(TeleportBPlayer& p, const TeleportBArgs& a, std::int32_t game_state, const TeleportBHooks& h) {
    if (p.waiting_for_port) p.waiting_for_port = false;
    if (p.stand_state == 0x66 || p.linkdead) return false;
    if (a.reason == 0) {
        // The current position, packed as an OP_ClientUpdate; position sample 2 before it goes.
        h.position_sample(2);
        h.send_client_update();
    }
    std::int32_t arg9 = (a.reason == 0xf || a.reason == 0x11 || a.reason == 0x13) ? 2 : -1;
    h.save_physics();
    if (a.x < 999999.0f) p.x = a.x;
    if (a.y < 999999.0f) p.y = a.y;
    if (a.z < 999999.0f) p.z = a.z;
    if (a.heading < 513.0f) p.heading = a.heading;
    if (game_state != 1 && a.zone != p.zone) {
        if (p.race == 8 || p.race == 11 || p.race == 12) p.z = p.z + 1.0f;
        MoveToZoneCall m;
        m.zone = a.zone;
        m.save_flag = 0;
        m.reason = a.reason;
        m.x = p.x;
        m.y = p.y;
        m.z = p.z;
        m.heading = ftol(p.heading);
        m.arg8 = a.arg8;
        m.arg9 = arg9;
        m.arg10 = a.arg9;
        h.move_to_zone(m);
        h.rewind_reset();
        return true;
    }
    if (h.move_object) h.move_object(p.x, p.y, p.z);
    h.position_sample(3);
    float above = p.z + 1.0f;
    double floor = h.floor_height(p.x, p.y, above, 1);
    if (floor != static_cast<double>(kNoFloor)) p.floor_height = static_cast<float>(floor);
    h.update_actor();
    p.colliding_type = 0;
    p.standing_on = false;
    p.touching_switch = false;
    h.find_person_request();
    h.rewind_reset();
    return true;
}


void zone_player_to_bind(std::size_t size, const ZoneToBindPacket& p, std::uint32_t current_zone,
                         bool have_respawn_window, bool have_cast_spell_window, const ZoneToBindHooks& h) {
    if (size <= 20) return;
    if (have_respawn_window) h.deactivate_respawn_window();
    h.activate_main_ui();
    h.set_keep_items(p.keep_items);
    h.chat(539, p.zone_name, 13);
    h.prepare_for_repop();
    h.set_vitals(p.hp, p.mana, p.endurance);
    h.fill_air();
    if (have_cast_spell_window) h.update_spell_gems();
    if (p.zone == current_zone) {
        h.stop_death_music();
        h.rewind_reset();
        h.send_position_list();
    } else {
        MoveToZoneCall m;
        m.zone = static_cast<std::int32_t>(p.zone);
        m.save_flag = 1;
        m.reason = 10;
        m.x = p.x;
        m.y = p.y;
        m.z = p.z;
        m.heading = ftol(p.heading);
        m.arg8 = -1;
        m.arg9 = -1;
        m.arg10 = 0;
        h.move_to_zone(m);
        h.local_view_setup();
    }
    h.restore_camera();
    h.set_keep_items(0);
    h.finish();
}

} // namespace hailstone::game
