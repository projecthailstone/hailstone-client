#include "app/stats_ui.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "core/game_dir.h"
#include "core/log.h"
#include "game/eq_names.h"
#include "game/mechanics/character.h"
#include "game/mechanics/sheet_builder.h"
#include "game/mechanics/tables.h"
#include "net/connection/game_connection.h"

namespace hailstone::app {

using namespace game::mechanics;

StatsUi::StatsUi(const GameDir& game) : tables_(std::make_unique<GameTables>()) {
    if (!tables_->load(game)) log::warn("stats: Resources/BaseData.txt or SkillCaps.txt missing; values will be 0");
}

StatsUi::~StatsUi() = default;

void share_own_hp_items(net::GameConnection& conn, const Character* ch) {
    if (!ch) {
        conn.set_own_hp_items(0, 0);
        return;
    }
    const std::int64_t items = ch->stats().hp_bonus;
    conn.set_own_hp_items(ch->sheet().vitals.cur_hp ? 0 : items, ch->sheet().vitals.max_hp ? 0 : items);
}

void StatsUi::forget() {
    character_.reset();
    sheet_.reset();
    class_sheet_.reset();
}

namespace {

void row(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

std::string pct_haste(std::int32_t v) {
    if (v == 0) return "0%";
    return std::to_string(v - 100) + "%";
}

} // namespace

void StatsUi::draw(net::GameConnection& conn) {
    const auto& profile = conn.profile();
    // The camp countdown is still in the game (GameConnection::in_zone): the player stays and is rebuilt, or every
    // window that reads it (the inventory's stats, movement, the hot buttons) would show zero until the camp ends.
    if (!profile || !conn.in_zone()) {
        forget();
        share_own_hp_items(conn, nullptr); // out of the zone the items' HP goes
        return;
    }

    auto now = std::chrono::steady_clock::now();
    // The same live state the connection's regen ticks build the character from (its time sitting included).
    LiveState live = conn.live_state();
    // ProcessAllStats' first loop: the worn items' stats-disabled bytes (0x7b28a0) before the sums read them.
    conn.refresh_worn_stats_disabled();
    // Rebuilt every frame (movement, the hot buttons and the windows read this player), into the last frame's sheet:
    // its items, buffs and abilities reuse their storage, so an unchanged inventory is copied without allocating.
    character_.reset();
    if (!sheet_) sheet_ = std::make_unique<CharacterSheet>();
    // The owned AA ranks' effects (CacheAAEffects).
    build_sheet_into(*sheet_, *profile, conn.inventory(), conn.spell_state(), live,
                     [&conn](std::vector<SheetAbility>& out) { conn.aa_sheet_abilities_into(out); },
                     conn.zone_info() ? &*conn.zone_info() : nullptr, &conn.world());
    sheet_->prestige_limit = conn.game_feature(0x13); // GetGameFeature(UseItem): the membership's (-1 none)
    const CharacterSheet& sheet = *sheet_;
    character_ = std::make_unique<Character>(sheet, *tables_, spell_lookup(conn.spell_table()));
    Character& ch = *character_;
    ch.process_all_stats();
    share_own_hp_items(conn, &ch);
    const DerivedStats& d = ch.stats();
    std::int32_t max_hp = ch.max_hp();
    std::int32_t max_mana = ch.max_mana();
    std::int32_t max_end = ch.max_endurance();

    // The server may take the best of several classes' formulas (server_ext().max_vitals_classes; NMS multiclass
    // servers: the unlocked classes, zone/client_mods.cpp CalcBaseHP, CalcBaseMana, CalcBaseEndurance); the same
    // formulas with each. A server's client patch may answer Max_HP / Max_Mana / Max_Endurance itself (the NMS
    // patch: the server's values, sheet.vitals), so the comparison runs the stock formulas on a copy without them.
    const bool patched = sheet.vitals.any();
    std::int32_t best_hp = max_hp, best_mana = max_mana, best_end = max_end;
    const std::vector<std::uint8_t> vitals_classes = conn.server_ext().max_vitals_classes(conn, sheet.class_id);
    const bool several = vitals_classes.size() != 1 || vitals_classes[0] != sheet.class_id;
    if ((open_ || now >= next_check_) && (several || patched)) {
        best_hp = best_mana = best_end = 0;
        if (!class_sheet_) class_sheet_ = std::make_unique<CharacterSheet>();
        for (std::uint8_t c : vitals_classes) {
            CharacterSheet& as = *class_sheet_;
            as = sheet; // copy-assigned: the last copy's storage is reused
            as.class_id = c;
            as.vitals = {};
            Character cc(as, *tables_, spell_lookup(conn.spell_table()));
            cc.process_all_stats();
            best_hp = std::max(best_hp, cc.max_hp());
            best_mana = std::max(best_mana, cc.max_mana());
            best_end = std::max(best_end, cc.max_endurance());
        }
    }

    // Compare with the server's maxima (OP_HPUpdate, OP_ManaUpdate) at most once a second. A mismatch is a warning
    // when the client uses the stock formulas; under a patch that answers with the server's values nothing in the
    // client reads the formulas, so it is only information (what a client without the patch would show).
    const auto& ps = conn.player_status();
    // The server's HP with the items' HP its own HP update leaves out (net::GameConnection::own_hp), as Max_HP has it.
    const net::GameConnection::OwnHp own = conn.own_hp();
    if (now >= next_check_) {
        next_check_ = now + std::chrono::seconds(1);
        auto report = [patched](std::string_view what, std::int64_t computed, std::int64_t server) {
            if (patched)
                log::info("stats: max {} by the stock formula {}, server says {} (the server's client patch uses the "
                          "server's)", what, computed, server);
            else
                log::warn("stats: max {} computed {}, server says {}", what, computed, server);
        };
        if (ps.max_hp > 0 && own.max != best_hp && (warned_hp_[0] != best_hp || warned_hp_[1] != own.max)) {
            warned_hp_[0] = best_hp;
            warned_hp_[1] = own.max;
            report("HP", best_hp, own.max);
        }
        if (ps.max_mana > 0 && static_cast<std::int64_t>(ps.max_mana) != best_mana &&
            (warned_mana_[0] != best_mana || warned_mana_[1] != ps.max_mana)) {
            warned_mana_[0] = best_mana;
            warned_mana_[1] = ps.max_mana;
            report("mana", best_mana, ps.max_mana);
        }
        if (ps.max_endurance > 0 && static_cast<std::int64_t>(ps.max_endurance) != best_end &&
            (warned_end_[0] != best_end || warned_end_[1] != ps.max_endurance)) {
            warned_end_[0] = best_end;
            warned_end_[1] = ps.max_endurance;
            report("endurance", best_end, ps.max_endurance);
        }
    }
    if (!open_) return;

    ImGui::SetNextWindowSize(ImVec2(440, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 80), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Character", &open_)) {
        ImGui::End();
        return;
    }
    ImGui::Text("%s  level %d %s %s", profile->name.c_str(), sheet.level, game::race_name(sheet.race).c_str(),
                game::class_name(sheet.class_id).c_str());
    if (sheet.class_mask) {
        std::string classes;
        for (std::uint32_t c = 1; c <= 16; ++c)
            if (sheet.class_mask & (1u << (c - 1))) classes += (classes.empty() ? "" : " / ") + game::class_name(c);
        ImGui::TextDisabled("classes: %s (the client's formulas use the profile class)", classes.c_str());
    }
    ImGui::TextDisabled("x87 at 24-bit precision, as in game (Direct3D 9)");

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("vitals", 2, kFlags)) {
        auto vs = [](std::int64_t cur, std::int64_t computed, std::int64_t server) {
            std::string s = std::to_string(cur) + " / " + std::to_string(computed);
            if (server > 0) s += server == computed ? "  (server: same)" : "  (server: " + std::to_string(server) + ")";
            return s;
        };
        row("HP", vs(own.cur, max_hp, ps.max_hp > 0 ? own.max : 0));
        row("Mana", vs(ps.mana, max_mana, ps.max_mana));
        row("Endurance", vs(ps.endurance, max_end, ps.max_endurance));
        if (several || patched) {
            row(several ? "Unlocked classes" : "Stock formulas",
                (several ? "best max HP " : "max HP ") + std::to_string(best_hp) + ", mana " + std::to_string(best_mana) +
                    ", endurance " + std::to_string(best_end) + (patched ? " (the patch answers the server's)" : ""));
        }
        row("AC", std::to_string(d.ac));
        row("Item ATK", std::to_string(d.attack));
        row("Haste", pct_haste(ch.total_effect(11)) + "  (items " + pct_haste(d.attack_speed) + ")");
        row("Regen / tick", "HP " + std::to_string(ch.hp_regen()) + ", mana " + std::to_string(ch.mana_regen()) +
                                ", endurance " + std::to_string(ch.endurance_regen()));
        row("Weight", std::to_string(d.weight) + " / " + std::to_string(d.stats.str));
        row("Defense", std::to_string(ch.compute_defense()));
        row("Air supply", std::to_string(ch.max_air_supply()));
        row("Food / drink", std::to_string(sheet.food) + " / " + std::to_string(sheet.drink));
        ImGui::EndTable();
    }
    if (ImGui::BeginTable("stats", 4, kFlags)) {
        ImGui::TableSetupColumn("Stat");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Base");
        ImGui::TableSetupColumn("Cap");
        ImGui::TableHeadersRow();
        struct S {
            const char* name;
            std::int32_t value, base, heroic;
            int cap;
        };
        const S rows[] = {
            {"STR", d.stats.str, sheet.base_str, ch.heroic(&DerivedStats::heroic_str), 0},
            {"STA", d.stats.sta, sheet.base_sta, ch.heroic(&DerivedStats::heroic_sta), 1},
            {"AGI", d.stats.agi, sheet.base_agi, ch.heroic(&DerivedStats::heroic_agi), 2},
            {"DEX", d.stats.dex, sheet.base_dex, ch.heroic(&DerivedStats::heroic_dex), 3},
            {"WIS", d.stats.wis, sheet.base_wis, ch.heroic(&DerivedStats::heroic_wis), 4},
            {"INT", d.stats.int_, sheet.base_int, ch.heroic(&DerivedStats::heroic_int), 5},
            {"CHA", d.stats.cha, sheet.base_cha, ch.heroic(&DerivedStats::heroic_cha), 6},
            {"Magic", d.stats.sv_magic, ch.basesave_magic(), ch.heroic(&DerivedStats::heroic_sv_magic), 7},
            {"Fire", d.stats.sv_fire, ch.basesave_fire(), ch.heroic(&DerivedStats::heroic_sv_fire), 9},
            {"Cold", d.stats.sv_cold, ch.basesave_cold(), ch.heroic(&DerivedStats::heroic_sv_cold), 8},
            {"Disease", d.stats.sv_disease, ch.basesave_disease(), ch.heroic(&DerivedStats::heroic_sv_disease), 11},
            {"Poison", d.stats.sv_poison, ch.basesave_poison(), ch.heroic(&DerivedStats::heroic_sv_poison), 10},
            {"Corruption", d.stats.sv_corruption, sheet.base_save_corruption,
             ch.heroic(&DerivedStats::heroic_sv_corruption), 13},
            {"Physical", d.stats.sv_physical, sheet.base_save_physical, 0, -1},
        };
        for (const S& s : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(s.name);
            ImGui::TableSetColumnIndex(1);
            if (s.heroic) ImGui::Text("%d (+%d heroic)", s.value, s.heroic);
            else ImGui::Text("%d", s.value);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", s.base);
            ImGui::TableSetColumnIndex(3);
            if (s.cap >= 0) ImGui::Text("%d", ch.stat_cap(s.cap));
        }
        ImGui::EndTable();
    }
    if (ImGui::CollapsingHeader("Skills")) {
        if (ImGui::BeginTable("skills", 3, kFlags)) {
            ImGui::TableSetupColumn("Skill");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Cap");
            ImGui::TableHeadersRow();
            for (std::uint32_t k = 0; k < 78; ++k) {
                std::int32_t value = ch.adjusted_skill(k);
                std::int32_t cap = ch.skill_cap(sheet.level, sheet.class_id, static_cast<std::int32_t>(k));
                if (value == 0 && cap == 0) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                std::string name = conn.strings().format(skill_name_eqstr(k), {});
                ImGui::TextUnformatted(name.empty() ? std::to_string(k).c_str() : name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", value);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", cap);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace hailstone::app
