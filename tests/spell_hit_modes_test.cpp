#include <doctest/doctest.h>

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/log.h"
#include "mq_test_world.h"
#include "net/messages/zone.h"
#include "plugin/hooks.h"

using namespace hailstone;

namespace {
using Access = net::GameConnectionTestAccess;

net::messages::GameMessage formatted(std::uint32_t id, std::vector<std::string> args, std::uint32_t color) {
    net::FormattedMessage message;
    message.string_id = id;
    message.type = color;
    message.args = std::move(args);
    return {net::messages::GameOpcode::FormattedMessage, std::move(message)};
}

net::messages::GameMessage special(std::string_view text, std::uint32_t color, std::uint8_t speak = 0,
                                   std::uint8_t journal = 0, std::uint8_t language = 0,
                                   std::string_view sayer = {}) {
    net::SpecialMessage message;
    message.speak_mode = speak;
    message.journal_mode = journal;
    message.language = language;
    message.type = color;
    message.sayer = std::string(sayer);
    message.message = std::string(text);
    return {net::messages::GameOpcode::SpecialMesg, std::move(message)};
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

TEST_CASE("Spell hit modes: semantic messages change display while logs and hooks keep full text") {
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
            conn.receive_zone_message_for_test(formatted(sample.id, sample.args, 321));
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
        conn.receive_zone_message_for_test(packet);
        auto event = conn.poll_chat();
        REQUIRE(event);
        CHECK(event->text == incoming.back());
        CHECK(read_hit_log(conn.chat_log_path()).ends_with("] " + event->text + "\n"));
    }
}

TEST_CASE("Spell hit modes: special and formatted combat messages shorten display and preserve full logs and hooks") {
    plugin::Hooks hooks;
    net::ConnectionOptions options;
    options.hooks = &hooks;
    mq_test::World world("hailstone_spell_text_modes", options);
    auto& conn = *world.conn;
    std::ofstream(world.dir / "eqstr_us.txt") << "EQST0002\n6688 %1\n";
    world.game->rescan();
    conn.reload_game_files();
    enable_log(conn);
    std::vector<std::string> incoming;
    hooks.on_chat(plugin::Hooks::kClient, [&](plugin::ChatLine& line) {
        incoming.push_back(line.text);
        return plugin::Flow::Continue;
    });
    struct Fixture {
        std::string full, compact, amount;
        std::uint32_t color;
        game::SpellHitReport kind;
    };
    const std::array fixtures{
        Fixture{"Diddles hit a Broken Skull guardian for 486 points of non-melee damage. (Time Rend)",
                "Diddles -> a Broken Skull guardian: 486 damage (Time Rend)", "486", 283,
                game::SpellHitReport::DirectDamage},
        Fixture{"Diddles has healed herself for 315 points of damage. (Killing Spree)",
                "Diddles -> herself: 315 heal (Killing Spree)", "315", 375, game::SpellHitReport::Healing},
        Fixture{"Diddles hit a Broken Skull guardian for 1,234 points of non-melee damage. (Venom)",
                "Diddles -> a Broken Skull guardian: 1,234 DoT (Venom)", "1,234", 376,
                game::SpellHitReport::DamageOverTime},
    };
    for (bool generic : {false, true}) {
        for (std::int32_t mode : {0, 1, 2}) {
            for (const auto& fixture : fixtures) {
                CAPTURE(generic);
                CAPTURE(mode);
                CAPTURE(fixture.color);
                game::SpellHitModes modes;
                modes.fill(2);
                modes[static_cast<std::size_t>(fixture.kind)] = mode;
                conn.set_spell_hit_modes(modes);
                const auto before = read_hit_log(conn.chat_log_path());
                conn.receive_zone_message_for_test(generic ? formatted(6688, {fixture.full}, fixture.color)
                                                        : special(fixture.full, fixture.color));
                auto event = conn.poll_chat();
                REQUIRE(event);
                REQUIRE_FALSE(conn.poll_chat());
                CHECK(event->kind == net::ChatEvent::Kind::System);
                CHECK(event->color == fixture.color);
                CHECK(event->text == (mode == 0 ? fixture.full : mode == 1 ? fixture.compact : fixture.amount));
                REQUIRE_FALSE(incoming.empty());
                CHECK(incoming.back() == fixture.full);
                const auto added = read_hit_log(conn.chat_log_path()).substr(before.size());
                CHECK(added.ends_with("] " + fixture.full + "\n"));
                const auto first = added.find(fixture.full);
                REQUIRE(first != std::string::npos);
                CHECK(added.find(fixture.full, first + fixture.full.size()) == std::string::npos);
                char recent[4096]{};
                log::copy_recent_lines(recent, sizeof recent);
                CHECK(std::string(recent).find("chat: " + fixture.full) != std::string::npos);
            }
        }
    }
}

TEST_CASE("Spell hit modes: generic heals follow Healing independently of Healing Over Time") {
    mq_test::World world("hailstone_raw_heal_category");
    auto& conn = *world.conn;
    const std::string full = "Diddles has healed herself for 315 points of damage. (Killing Spree)";
    game::SpellHitModes modes{};
    modes[static_cast<std::size_t>(game::SpellHitReport::HealingOverTime)] = 2;
    conn.set_spell_hit_modes(modes);
    conn.receive_zone_message_for_test(special(full, 375));
    auto event = conn.poll_chat();
    REQUIRE(event);
    CHECK(event->text == full);
    modes[static_cast<std::size_t>(game::SpellHitReport::Healing)] = 1;
    conn.set_spell_hit_modes(modes);
    conn.receive_zone_message_for_test(special(full, 375));
    event = conn.poll_chat();
    REQUIRE(event);
    CHECK(event->text == "Diddles -> herself: 315 heal (Killing Spree)");
}

TEST_CASE("Spell hit modes: unrelated ids, unmatched combat text and dialogue keep complete text") {
    mq_test::World world("hailstone_spell_text_exclusions");
    auto& conn = *world.conn;
    std::ofstream(world.dir / "eqstr_us.txt")
        << "EQST0002\n6688 %1\n99999 %1\n12205 %1\n1035 %2\n1032 %1 says, '%2'\n1410 %1 says, '%3'\n";
    world.game->rescan();
    conn.reload_game_files();
    Access::profile(conn)->languages = std::vector<std::uint8_t>{100, 100};
    enable_log(conn);
    game::SpellHitModes modes;
    modes.fill(2);
    conn.set_spell_hit_modes(modes);
    const std::string full = "Diddles hit a Broken Skull guardian for 486 points of non-melee damage. (Time Rend)";
    const std::string malformed = "Diddles hit a Broken Skull guardian for 48,6 points of non-melee damage. (Time Rend)";
    struct Fixture {
        net::messages::GameMessage message;
        std::string full;
        std::uint32_t color;
    };
    const std::vector<Fixture> fixtures{
        {formatted(6688, {full, "extra argument"}, 283), full, 283},
        {formatted(99999, {full}, 283), full, 283},
        {formatted(12205, {full}, 283), full, 283},
        {formatted(6688, {full}, 256), full, 256},
        {formatted(6688, {malformed}, 283), malformed, 283},
        {special(full, 256), full, 256},
        {special(malformed, 283), malformed, 283},
        {special(full, 283, 3), full, 283},
        {special(full, 283, 0, 1), full, 283},
        {special(full, 283, 0, 0, 1), full, 283},
        {special(full, 283, 0, 0, 255), full, 283},
        {special(full, 283, 0, 0, 0, "Guard"), full, 283},
        {special(full, 283, 1, 0, 0, "Guard"), "Guard says, '" + full + "'", 283},
    };
    for (const auto& fixture : fixtures) {
        CAPTURE(static_cast<std::uint16_t>(fixture.message.opcode));
        CAPTURE(fixture.full);
        conn.receive_zone_message_for_test(fixture.message);
        auto event = conn.poll_chat();
        REQUIRE(event);
        REQUIRE_FALSE(conn.poll_chat());
        CHECK(event->text == fixture.full);
        CHECK(event->color == fixture.color);
        CHECK(read_hit_log(conn.chat_log_path()).ends_with("] " + fixture.full + "\n"));
    }
    net::ChannelMessage say;
    say.sender = "Groupie";
    say.channel = 8;
    say.language_skill = 100;
    say.message = full;
    conn.receive_zone_message_for_test({net::messages::GameOpcode::ChannelMessage, say});
    auto event = conn.poll_chat();
    REQUIRE(event);
    CHECK(event->text == "Groupie says, '" + full + "'");
}

TEST_CASE("Spell hit modes: special and formatted combat text respects hook replacements and drops") {
    plugin::Hooks hooks;
    net::ConnectionOptions options;
    options.hooks = &hooks;
    mq_test::World world("hailstone_spell_text_hooks", options);
    auto& conn = *world.conn;
    std::ofstream(world.dir / "eqstr_us.txt") << "EQST0002\n6688 %1\n";
    world.game->rescan();
    conn.reload_game_files();
    enable_log(conn);
    game::SpellHitModes modes;
    modes.fill(2);
    conn.set_spell_hit_modes(modes);
    const std::string full = "Diddles has healed herself for 315 points of damage. (Killing Spree)";
    bool drop = false;
    hooks.on_chat(plugin::Hooks::kClient, [&](plugin::ChatLine& line) {
        CHECK(line.text == full);
        if (drop) return plugin::Flow::Drop;
        line.text = "replacement full line";
        return plugin::Flow::Continue;
    });
    for (const auto& packet : {special(full, 375), formatted(6688, {full}, 375)}) {
        conn.receive_zone_message_for_test(packet);
        auto event = conn.poll_chat();
        REQUIRE(event);
        CHECK(event->text == "replacement full line");
        CHECK(read_hit_log(conn.chat_log_path()).ends_with("] replacement full line\n"));
    }
    drop = true;
    const auto before = read_hit_log(conn.chat_log_path());
    for (const auto& packet : {special(full, 375), formatted(6688, {full}, 375)}) {
        conn.receive_zone_message_for_test(packet);
        CHECK_FALSE(conn.poll_chat());
    }
    CHECK(read_hit_log(conn.chat_log_path()) == before);
}

TEST_CASE("Spell hit modes: stock EQEmu's caster-less heal message follows Healing") {
    mq_test::World world("hailstone_spell_stock_heal");
    auto& conn = *world.conn;
    enable_log(conn);
    // Client::Message: OP_SpecialMesg, speak mode Raw, journal None, language 0, no sayer, Chat::NonMelee.
    const std::string full = "You have been healed for 42 points of damage.";
    for (std::int32_t mode : {0, 1, 2}) {
        CAPTURE(mode);
        game::SpellHitModes modes;
        modes.fill(0);
        modes[static_cast<std::size_t>(game::SpellHitReport::Healing)] = mode;
        conn.set_spell_hit_modes(modes);
        conn.receive_zone_message_for_test(special(full, 283));
        auto event = conn.poll_chat();
        REQUIRE(event);
        CHECK(event->color == 283);
        CHECK(event->text == (mode == 0 ? full : mode == 1 ? std::string("You: 42 heal") : std::string("42")));
        CHECK(read_hit_log(conn.chat_log_path()).ends_with("] " + full + "\n"));
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
            conn.receive_zone_message_for_test({net::messages::GameOpcode::Damage, hit});
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
    conn.receive_zone_message_for_test({net::messages::GameOpcode::Damage, other});
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
        conn.receive_zone_message_for_test({net::messages::GameOpcode::Damage, hit});
        auto event = conn.poll_chat();
        REQUIRE(event);
        CHECK(event->text == game::report_hit_line(conn.strings(), *full, mode));
        CHECK(read_hit_log(conn.chat_log_path()).ends_with("] " + full->text + "\n"));
        hit.damage = 0;
        const auto miss = game::damage_report(conn.strings(), conn.world(), hit.target, hit.source,
                                                hit.type, hit.damage, hit.special);
        REQUIRE(miss);
        conn.receive_zone_message_for_test({net::messages::GameOpcode::Damage, hit});
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
    conn.receive_zone_message_for_test(formatted(428, {"123"}, 321));
    auto event = conn.poll_chat();
    REQUIRE(event);
    CHECK(event->text == "replacement full line");
    CHECK(read_hit_log(conn.chat_log_path()).ends_with("] replacement full line\n"));
}
