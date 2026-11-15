#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/byte_io.h"
#include "core/log.h"
#include "mq_test_world.h"
#include "net/opcodes.h"
#include "plugin/hooks.h"

using namespace hailstone;

namespace {
using Access = net::GameConnectionTestAccess;

net::AppPacket formatted(std::uint32_t id, const std::vector<std::string>& args, std::uint32_t color) {
    ByteWriter writer;
    writer.le(std::uint32_t{0});
    writer.le(id);
    writer.le(color);
    for (const auto& arg : args) writer.fixed_string(arg, arg.size() + 1);
    writer.le(std::uint8_t{0});
    return {net::to_wire(net::Op::FormattedMessage), writer.take()};
}

std::string read_hit_log(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void enable_log(net::GameConnection& conn) {
    Access::ticket(conn).server_name = "Test Server";
    Access::client_options(conn).set(game::ClientOption::Log, 1);
    Access::open_chat_log(conn, "hit-modes");
    REQUIRE(conn.chat_logging());
}
} // namespace

TEST_CASE("Spell hit modes: decoded packets change display while logs and hooks keep full text") {
    plugin::Hooks hooks;
    net::ConnectionOptions options;
    options.hooks = &hooks;
    mq_test::World world("hailstone_spell_hit_modes", options);
    auto& conn = *world.conn;
    std::ofstream(world.dir / "eqstr_us.txt")
        << "EQST0002\n9073 DD %1 -> %2: %3\n9072 DOT %1: %2 via %3\n"
        << "9068 HEAL %1: %2\n12976 HOT %1 via %2\n428 CRIT %1\n"
        << "1039 HEAL CRIT %1: %2\n12205 CAST %1\n";
    world.game->rescan();
    conn.reload_game_files();
    enable_log(conn);
    std::vector<std::string> incoming;
    hooks.on_chat(plugin::Hooks::kClient, [&](plugin::ChatLine& line) {
        incoming.push_back(line.text);
        return plugin::Flow::Continue;
    });

    struct Case {
        std::uint32_t id;
        std::vector<std::string> args;
        game::SpellHitReport kind;
        std::string full;
        std::string amount;
    };
    const std::vector<Case> cases{
        {9073, {"Tester", "a rat", "1234"}, game::SpellHitReport::DirectDamage, "DD Tester -> a rat: 1234", "1234"},
        {9072, {"a rat", "27", "Fixture Plague"}, game::SpellHitReport::DamageOverTime,
         "DOT a rat: 27 via Fixture Plague", "27"},
        {9068, {"Groupie", "45"}, game::SpellHitReport::Healing, "HEAL Groupie: 45", "45"},
        {12976, {"67", "Fixture Renewal"}, game::SpellHitReport::HealingOverTime,
         "HOT 67 via Fixture Renewal", "67"},
        {428, {"89"}, game::SpellHitReport::DirectDamage, "CRIT 89", "89"},
        {1039, {"Groupie", "91"}, game::SpellHitReport::Healing, "HEAL CRIT Groupie: 91", "91"},
    };
    for (std::int32_t mode : {0, 1, 2}) {
        for (const auto& sample : cases) {
            CAPTURE(mode);
            CAPTURE(sample.id);
            game::SpellHitModes modes{};
            modes[static_cast<std::size_t>(sample.kind)] = mode;
            conn.set_spell_hit_modes(modes);
            Access::handle_zone(conn, formatted(sample.id, sample.args, 321));
            auto event = conn.poll_chat();
            REQUIRE(event);
            REQUIRE_FALSE(conn.poll_chat());
            CHECK(event->color == 321);
            CHECK(event->kind == net::ChatEvent::Kind::System);
            if (mode == 0) CHECK(event->text == sample.full);
            else if (mode == 2) CHECK(event->text == sample.amount);
            else {
                CHECK(event->text != sample.full);
                CHECK(event->text.find(sample.amount) != std::string::npos);
            }
            REQUIRE_FALSE(incoming.empty());
            CHECK(incoming.back() == sample.full);
            CHECK(read_hit_log(conn.chat_log_path()).ends_with("] " + sample.full + "\n"));
            char recent[4096]{};
            log::copy_recent_lines(recent, sizeof recent);
            CHECK(std::string(recent).find("chat: " + sample.full) != std::string::npos);
        }
    }
    game::SpellHitModes modes;
    modes.fill(2);
    conn.set_spell_hit_modes(modes);
    for (const auto& packet : {formatted(12205, {"Fixture Plague"}, 289),
                               formatted(9073, {"Tester", "a rat", "invalid"}, 321)}) {
        Access::handle_zone(conn, packet);
        auto event = conn.poll_chat();
        REQUIRE(event);
        CHECK(event->text == incoming.back());
        CHECK(read_hit_log(conn.chat_log_path()).ends_with("] " + event->text + "\n"));
    }
}

TEST_CASE("Spell hit modes: numeric OP_Damage landings retain full logs and nonnumeric landings") {
    mq_test::World world("hailstone_spell_landing_modes");
    auto& conn = *world.conn;
    std::ofstream(world.dir / "eqstr_us.txt") << "EQST0002\n12203 %1 DAMAGE %2\n4600 %1 HEAL %2\n";
    world.game->rescan();
    conn.reload_game_files();
    auto spells = std::make_shared<game::SpellTable>();
    REQUIRE(spells->parse(mq_test::spell_line({{0, "100"}, {1, "Fixture"}, {6, "LANDED"},
                                               {7, " OTHER LANDED"}, {83, "1"}})) == 1);
    conn.set_spell_table_for_test(spells);
    enable_log(conn);
    for (std::int32_t mode : {0, 1, 2}) {
        game::SpellHitModes modes;
        modes.fill(mode);
        conn.set_spell_hit_modes(modes);
        for (std::int32_t damage : {32, -64, 0}) {
            CAPTURE(mode);
            CAPTURE(damage);
            net::CombatDamage hit;
            hit.target = 1;
            hit.source = 12;
            hit.type = 231;
            hit.spell_id = 100;
            hit.damage = damage;
            Access::handle_zone(conn, net::encode(hit));
            auto event = conn.poll_chat();
            REQUIRE(event);
            const auto* spell = spells->get(100);
            REQUIRE(spell);
            const auto full = game::spell_hit_message(conn.strings(), conn.world(), *spell, 1, damage);
            REQUIRE(full);
            CHECK(read_hit_log(conn.chat_log_path()).ends_with("] " + *full + "\n"));
            if (mode == 0 || damage == 0) CHECK(event->text == *full);
            else if (mode == 2) CHECK(event->text == (damage > 0 ? "32" : "64"));
            else CHECK(event->text != *full);
        }
    }
    net::CombatDamage other;
    other.target = 12;
    other.source = 1;
    other.type = 231;
    other.spell_id = 100;
    other.damage = 99;
    Access::handle_zone(conn, net::encode(other));
    auto event = conn.poll_chat();
    REQUIRE(event);
    CHECK(event->text == "Groupie OTHER LANDED");
}

TEST_CASE("Hit modes: existing melee modes preserve complete log lines") {
    mq_test::World world("hailstone_melee_hit_mode_logs");
    auto& conn = *world.conn;
    std::ofstream(world.dir / "eqstr_us.txt")
        << "EQST0002\n12185 You\n12187 YOU\n12162 %1 %2 %3 %4\n12160 for %1 damage.\n"
        << "12161 for %1 damage.\n12191 crush\n12192 crushes\n2130 %1 %2 for %3\n"
        << "12158 %1 try to %2 %3, but %4!\n12156 miss\n15039 missed %1\n";
    world.game->rescan();
    conn.reload_game_files();
    enable_log(conn);
    for (std::int32_t mode : {0, 1, 2}) {
        game::HitModes modes;
        modes.fill(mode);
        conn.set_hit_modes(modes);
        net::CombatDamage hit;
        hit.target = 10;
        hit.source = 1;
        hit.type = 0;
        hit.damage = 123;
        const auto full = game::damage_report(conn.strings(), conn.world(), hit.target, hit.source,
                                               hit.type, hit.damage, hit.special);
        REQUIRE(full);
        Access::handle_zone(conn, net::encode(hit));
        auto event = conn.poll_chat();
        REQUIRE(event);
        CHECK(event->text == game::report_hit_line(conn.strings(), *full, mode));
        CHECK(read_hit_log(conn.chat_log_path()).ends_with("] " + full->text + "\n"));
        hit.damage = 0;
        const auto miss = game::damage_report(conn.strings(), conn.world(), hit.target, hit.source,
                                                hit.type, hit.damage, hit.special);
        REQUIRE(miss);
        Access::handle_zone(conn, net::encode(hit));
        event = conn.poll_chat();
        REQUIRE(event);
        CHECK(event->text == game::report_hit_line(conn.strings(), *miss, mode));
        CHECK(read_hit_log(conn.chat_log_path()).ends_with("] " + miss->text + "\n"));
    }
}

TEST_CASE("Spell hit modes: chat hook rewrites remain authoritative") {
    plugin::Hooks hooks;
    net::ConnectionOptions options;
    options.hooks = &hooks;
    mq_test::World world("hailstone_spell_hit_mode_rewrite", options);
    auto& conn = *world.conn;
    std::ofstream(world.dir / "eqstr_us.txt") << "EQST0002\n428 CRIT %1\n";
    world.game->rescan();
    conn.reload_game_files();
    game::SpellHitModes modes;
    modes.fill(2);
    conn.set_spell_hit_modes(modes);
    enable_log(conn);
    hooks.on_chat(plugin::Hooks::kClient, [](plugin::ChatLine& line) {
        CHECK(line.text == "CRIT 123");
        line.text = "replacement full line";
        return plugin::Flow::Continue;
    });
    Access::handle_zone(conn, formatted(428, {"123"}, 321));
    auto event = conn.poll_chat();
    REQUIRE(event);
    CHECK(event->text == "replacement full line");
    CHECK(read_hit_log(conn.chat_log_path()).ends_with("] replacement full line\n"));
}
