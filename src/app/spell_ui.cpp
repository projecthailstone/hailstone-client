#include "app/spell_ui.h"
#include "ui/lua/lua_host.h"

#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <format>

#include "core/log.h"
#include "game/spell_description.h"
#include "game/spells.h"
#include "net/game_connection.h"

namespace hailstone::app {

namespace {

const char* target_type_name(std::uint8_t t) {
    switch (t) {
    case 1: return "Line of sight";
    case 3: return "Group teleport";
    case 4: return "Area around caster";
    case 5: return "Single target";
    case 6: return "Self";
    case 8: return "Area around target";
    case 9: return "Animal";
    case 10: return "Undead";
    case 11: return "Summoned";
    case 13: return "Lifetap";
    case 14: return "Pet";
    case 15: return "Corpse";
    case 41: return "Group";
    default: return "Other";
    }
}

std::string seconds(std::uint32_t ms) {
    return ms % 1000 == 0 ? std::format("{} s", ms / 1000) : std::format("{:.1f} s", static_cast<double>(ms) / 1000.0);
}

ui::BuffView buff_view(const game::SpellTable& table, const game::EqStrings& strings, const DescriptionPlayer& player,
                       int slot, const game::BuffSlot& b, bool timers_on_hold, bool zone_suspends_buffs,
                       std::int64_t now) {
    ui::BuffView v;
    v.slot = slot;
    v.spell = b.spell;
    const auto* s = table.get(b.spell);
    v.name = s ? s->name : std::format("Spell {}", b.spell);
    v.icon = s ? s->spell_icon : -1;
    v.beneficial = !s || s->beneficial();
    v.time_left = game::buff_is_permanent(b.tics, s) ? "permanent"
                                                      : game::format_duration(game::buff_ms_left(
                                                            b, s, timers_on_hold, zone_suspends_buffs, now));
    v.tooltip = v.name;
    if (!b.caster_name.empty()) v.tooltip += "\nCast by " + b.caster_name;
    if (s) {
        std::string d = spell_description(*s, strings, player);
        if (!d.empty()) v.tooltip += "\n" + d;
    }
    if (v.beneficial) v.tooltip += "\n(right-click to remove)";
    return v;
}

} // namespace

SpellWindowsDrawn spell_windows_drawn(bool book_open) {
    SpellWindowsDrawn d;
    d.bar = !ui::lua::replaces("CastSpellWnd");
    d.cast = !ui::lua::replaces("CastingWindow");
    d.book = book_open && !ui::lua::replaces("SpellBookWnd");
    d.buffs = !ui::lua::replaces("BuffWindow");
    d.songs = !ui::lua::replaces("ShortDurationBuffWindow");
    d.target = !ui::lua::replaces("TargetWindow");
    return d;
}

DescriptionPlayer description_player(const net::GameConnection& conn, const game::mechanics::Character* character) {
    DescriptionPlayer p;
    p.character = character;
    const auto& st = conn.player_status();
    p.level = st.level ? st.level : (conn.profile() ? conn.profile()->level : 1);
    p.class_id = conn.profile() ? conn.profile()->class_ : 0;
    return p;
}

std::string spell_description(const game::SpellData& spell, const game::EqStrings& strings,
                              const DescriptionPlayer& player) {
    // GetSpellDescriptionText @0x4fc170: dbstr kind 6 of the spell's DescriptionIndex.
    std::string t = strings.db_string(static_cast<std::uint32_t>(spell.description_id), 6);
    return game::spell_description_text(spell, t, player.character, player.level, player.class_id);
}

std::string spell_tooltip(const game::SpellData& s, const game::EqStrings& strings, int level_needed,
                          const DescriptionPlayer& player) {
    std::string t = s.name;
    if (level_needed > 0 && level_needed < 255) t += std::format("\nLevel {}", level_needed);
    t += std::format("\nMana {}", s.mana);
    if (s.endurance_cost) t += std::format(", endurance {}", s.endurance_cost);
    t += "\nCast " + seconds(s.cast_time) + ", recovery " + seconds(s.recovery_time) + ", recast " + seconds(s.recast_time);
    t += std::format("\n{}{}", target_type_name(s.target_type), s.range > 0 ? std::format(", range {:.0f}", s.range) : "");
    std::string d = spell_description(s, strings, player);
    if (!d.empty()) t += "\n\n" + d;
    return t;
}

SpellUi::SpellUi(render::VkContext& vk, const GameDir& game) {
    try {
        icons_ = std::make_unique<ui::SpellIconCache>(vk, game);
    } catch (const std::exception& e) {
        log::warn("spell icons unavailable: {}", e.what());
    }
}

SpellUi::~SpellUi() = default;

void SpellUi::draw(net::GameConnection& conn, const game::mechanics::Character* character) {
    const DescriptionPlayer player = description_player(conn, character);
    const auto& st = conn.spell_state();
    const auto& table = conn.spell_table();
    const auto& strings = conn.strings();
    const auto now = net::GameConnection::now_ms();
    const std::uint8_t class_id = conn.profile() ? conn.profile()->class_ : 0;
    const int usable = conn.usable_gems();

    // CSpellBookWnd's rules are the connection's (game/spell_book.h): opening
    // may be refused, it sits the player down, and standing up, moving or
    // zoning close it.
    if (toggle_) {
        toggle_ = false;
        if (conn.spell_book_open()) conn.close_spell_book();
        else conn.open_spell_book();
    }
    book_.open = conn.spell_book_open();
    const SpellWindowsDrawn drawn = spell_windows_drawn(book_.open);

    // Gems.
    ui::SpellBarView bar;
    for (int g = 0; drawn.bar && g < game::kSpellGems; ++g) {
        const auto& gem = st.gems[static_cast<std::size_t>(g)];
        ui::SpellGemView v;
        v.usable = g < usable;
        if (const auto* s = gem.spell != game::kNoSpell ? table.get(gem.spell) : nullptr) {
            v.spell = gem.spell;
            v.name = s->name;
            v.icon = s->spell_icon;
            v.tooltip = spell_tooltip(*s, strings, conn.spell_level_needed(*s, class_id), player);
            // Time left on the gem timer, the recovery lock and the linked timer (Unix seconds).
            const std::uint32_t eq_now = net::GameConnection::eq_time();
            auto left = [eq_now](std::uint32_t eta) {
                return eq_now < eta ? static_cast<std::int64_t>(eta - eq_now) : std::int64_t{0};
            };
            std::int64_t wait = std::max(left(gem.ready), left(st.recovery_ready));
            if (s->reuse_timer_index > 0 && s->reuse_timer_index < 20) {
                auto unix_now = static_cast<std::int64_t>(std::time(nullptr));
                std::int64_t end = st.linked_ready[static_cast<std::size_t>(s->reuse_timer_index)];
                wait = std::max(wait, std::max<std::int64_t>(0, end - unix_now) * 1000);
            }
            if (wait > 0) {
                // The gem fills back up over its recast (or the 10 s lock while the server decides).
                std::int64_t span = std::max<std::int64_t>({static_cast<std::int64_t>(s->recast_time), s->recovery_time, 1000});
                if (st.casting.casting() || st.pending > 0) span = std::max<std::int64_t>(span, game::kGemLockMs);
                v.refresh = std::clamp(static_cast<float>(wait) / static_cast<float>(span), 0.05f, 1.0f);
            }
            v.casting = st.casting.casting() && st.casting.slot == g;
        }
        if (st.memorizing && st.memorizing->gem == g)
            v.memorize = 1.0f - static_cast<float>(st.memorizing->ticks_left) / static_cast<float>(game::kMemorizeTicks);
        bar.gems.push_back(std::move(v));
    }

    // Cast bar: GetCastingGaugeValue closes it once the ETA has passed.
    ui::CastBarView cast;
    if (st.casting.casting() && st.casting.eta_ms > now && st.casting.cast_time > 0) {
        cast.visible = true;
        const auto* s = table.get(st.casting.spell);
        cast.spell = s ? s->name : "Casting";
        cast.seconds_left = static_cast<float>(st.casting.eta_ms - now) / 1000.0f;
        cast.fraction = 1.0f - static_cast<float>(st.casting.eta_ms - now) / static_cast<float>(st.casting.cast_time);
    }
    if (st.memorizing) {
        const auto* s = table.get(st.memorizing->spell);
        cast.memorizing = "Memorizing " + (s ? s->name : std::string("a spell"));
        cast.memorize_fraction =
            std::clamp(1.0f - static_cast<float>(st.memorizing->ticks_left) / static_cast<float>(game::kMemorizeTicks), 0.0f, 1.0f);
    }

    // Book: up to 720 entries, each with a full tooltip, so only while the C++ book shows them.
    book_.entries.clear();
    book_.usable_gems = usable;
    for (int slot = 0; drawn.book && slot < game::kSpellBookSize; ++slot) {
        std::uint32_t id = st.book[static_cast<std::size_t>(slot)];
        if (id == game::kNoSpell || id == 0) continue;
        const auto* s = table.get(id);
        ui::SpellBookEntry e;
        e.slot = slot;
        e.spell = id;
        e.name = s ? s->name : std::format("Spell {}", id);
        e.icon = s ? s->spell_icon : -1;
        e.level = s ? conn.spell_level_needed(*s, class_id) : 0;
        e.tooltip = s ? spell_tooltip(*s, strings, e.level, player) : e.name;
        for (const auto& g : st.gems) e.memorized |= g.spell == id;
        book_.entries.push_back(std::move(e));
    }
    book_.status = st.memorizing ? cast.memorizing + "..." : std::string();
    book_.cursor_scroll = conn.cursor_scroll_spell() != 0;

    // Buffs.
    ui::BuffWindowView buffs;
    for (int slot = 0; (drawn.buffs || drawn.songs) && slot < game::kBuffSlots; ++slot) {
        const auto& b = st.buffs[static_cast<std::size_t>(slot)];
        if (b.empty()) continue;
        const bool song = slot >= game::kLongBuffSlots && slot < game::kLongBuffSlots + game::kShortBuffSlots;
        if (!(song ? drawn.songs : drawn.buffs)) continue;
        (song ? buffs.songs : buffs.buffs).push_back(
            buff_view(table, strings, player, slot, b, st.buff_timers_on_hold, st.zone_suspends_buffs, now));
    }
    const auto& world = conn.world();
    if (drawn.target && world.target_id && st.target_buffs_entity == world.target_id &&
        world.target_id != world.player_id) {
        if (auto it = world.spawns.find(world.target_id); it != world.spawns.end()) {
            buffs.target_name = game::display_name(it->second.name);
            for (const auto& [slot, b] : st.target_buffs)
                buffs.target.push_back(buff_view(table, strings, player, static_cast<int>(slot), b,
                                                 st.buff_timers_on_hold && conn.target_buffs_are_own(),
                                                 st.zone_suspends_buffs, now));
        }
    }

    ui::SpellActions actions;
    // Each off while its Lua window (ui/<pack>) stands in for it (spell_windows_drawn); the buff lists of a replaced
    // window were left empty above.
    if (drawn.bar) ui::spell_bar_window(bar, actions, icons_.get());
    if (drawn.cast) ui::cast_bar_window(cast);
    if (drawn.book) ui::spell_book_window(book_, actions, icons_.get());
    ui::buff_windows(buffs, actions, icons_.get());

    if (!book_.open && conn.spell_book_open()) conn.close_spell_book(); // the window's close button
    if (actions.toggle_book) {
        if (conn.spell_book_open()) conn.close_spell_book();
        else conn.open_spell_book();
    }
    if (actions.cast_gem) conn.cast_gem(*actions.cast_gem);
    if (actions.forget_gem) conn.forget_gem(*actions.forget_gem);
    if (actions.remove_buff) conn.remove_buff(*actions.remove_buff);
    if (actions.stop_cast) conn.stop_cast();
    if (actions.scribe) conn.scribe_cursor(*actions.scribe);
    if (actions.memorize) {
        int gem = actions.memorize->second;
        if (gem < 0) {
            // The first empty usable gem; with none, the last one (the original
            // memorizes into the gem picked on the cursor instead).
            gem = usable - 1;
            for (int g = 0; g < usable; ++g)
                if (st.gems[static_cast<std::size_t>(g)].spell == game::kNoSpell) { gem = g; break; }
        }
        conn.memorize(actions.memorize->first, gem);
    }
}

} // namespace hailstone::app
