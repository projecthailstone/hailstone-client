// The colours of the lines the client makes itself from stock RoF2 packets stay the original's: Hailstone's extended
// colours 355..408 (game/chat_windows.h) are only ever a server's.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/social_packets.h"
#include "net/protocol/rof2/packets/spell_packets.h"

using namespace hailstone;

TEST_CASE("Chat colours: environmental hits, considers, randoms and deaths keep the original's colours") {
    test::Rig rig;
    std::ofstream(rig.dir.path() / "eqstr_us.txt", std::ios::app)
        << "12187 YOU\n12118 FALL %1\n12130 DROWN %1\n12133 THORNS\n12134 THORNS %1\n"
        << "1086 RANDOM %1\n1087 ROLL %1 %2 %3\n12113 SLAIN %1\n12239 CON %1 %2 %3\n";
    rig.conn.reload_game_files();
    REQUIRE(rig.to_zone());
    rig.chat();

    net::CombatDamage hit;
    hit.target = 7; hit.type = 0xfc; hit.damage = 30;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 101; hit.type = 0xfb;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 7; hit.source = 101; hit.type = 0xf9; hit.damage = -10;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 101; hit.source = 7;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.source = 102;
    rig.server.zone.send_reliable(net::encode(hit));
    rig.server.zone.send_reliable(net::encode(net::Consider{7, 101, 5, 6, 0}));
    rig.server.zone.send_reliable(net::encode(net::RandomReply{1, 100, 20, "Gennro"}));
    rig.server.zone.send_reliable(net::encode(net::RandomReply{1, 100, 20, "Somebody"}));
    net::Death death;
    death.spawn_id = 101; death.killer_id = 7;
    rig.server.zone.send_reliable(net::encode(death));
    rig.server.add_spawn(test::make_spawn("Another", 103, 0, 1, -285, -148, 3.5f));
    death.spawn_id = 103;
    rig.server.zone.send_reliable(net::encode(death));
    rig.server.chat("Marker", 8, "stock-colour-marker");

    std::vector<net::ChatEvent> lines;
    REQUIRE(rig.until([&] {
        auto batch = rig.chat();
        for (auto& line : batch) lines.push_back(std::move(line));
        return std::any_of(lines.begin(), lines.end(), [](const auto& line) {
            return line.text.find("stock-colour-marker") != std::string::npos;
        });
    }));
    auto count = [&](std::uint32_t color) {
        return std::count_if(lines.begin(), lines.end(), [&](const auto& line) { return line.color == color; });
    };
    for (const auto& line : lines) {
        CAPTURE(line.text);
        CHECK(line.color < 355);
    }
    // ReportSuccessfulHit's environmental colours (game/combat.cpp environment_hit): 0x10a on us, 0x149 another's
    // 0xf3..0xfa, 0x117 another's fall, drowning or lava.
    CHECK(count(0x10a) == 2);
    CHECK(count(0x149) == 2);
    CHECK(count(0x117) == 1);
    CHECK(count(0x11f) == 4); // every roll's two lines, ours or another's
    CHECK(count(0x116) == 2); // others' deaths, an NPC's too
    // BuildConsiderMessage's line: the con colour as RGB, no user colour.
    auto con = std::find_if(lines.begin(), lines.end(), [](const auto& line) { return line.text.starts_with("CON"); });
    REQUIRE(con != lines.end());
    CHECK(con->color == 0);
    CHECK(con->rgb != 0);
}

TEST_CASE("Chat colours: spell landings, bard songs and interruptions keep the original's colours") {
    test::Rig rig;
    REQUIRE(rig.to_zone());
    rig.chat();
    auto spells = std::make_shared<game::SpellTable>();
    auto record = [](std::uint32_t id, std::uint8_t skill) {
        std::vector<std::string> fields(237, "0");
        fields[0] = std::to_string(id);
        fields[1] = "Fixture spell";
        fields[4] = "CAST SELF";
        fields[5] = " CAST OTHER";
        fields[6] = "LANDED SELF";
        fields[7] = " LANDED OTHER";
        fields[83] = "1";
        fields[100] = std::to_string(skill);
        std::string text;
        for (const auto& field : fields) {
            if (!text.empty()) text += '^';
            text += field;
        }
        return text + '\n';
    };
    REQUIRE(spells->parse(record(10, 41) + record(11, 24)) == 2);
    rig.conn.set_spell_table_for_test(spells);
    auto bard = test::make_spawn("Bard", 103, 0, 1, -285, -148, 3.5f);
    bard.class_ = 8;
    rig.server.add_spawn(bard);
    rig.server.zone.send_reliable(net::encode(net::BeginCast{10, 103, 200}));
    net::CombatDamage hit;
    hit.target = 7; hit.source = 103; hit.type = 231; hit.spell_id = 10;
    rig.server.zone.send_reliable(net::encode(hit));
    rig.server.zone.send_reliable(net::encode(net::InterruptCast{103, 123, {}}));
    rig.server.zone.send_reliable(net::encode(net::InterruptCast{7, 123, {}}));
    rig.server.zone.send_reliable(net::encode(net::InterruptCast{0, 123, {}}));
    hit.spell_id = 11; hit.damage = 20;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.damage = -20;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 101; hit.source = 7;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.source = 103;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.damage = 20; hit.source = 7;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.source = 103;
    rig.server.zone.send_reliable(net::encode(hit));
    hit.target = 7; hit.damage = 0;
    rig.server.zone.send_reliable(net::encode(hit));
    rig.server.chat("Marker", 8, "stock-spell-marker");
    std::vector<net::ChatEvent> lines;
    REQUIRE(rig.until([&] {
        auto batch = rig.chat();
        for (auto& line : batch) lines.push_back(std::move(line));
        return std::any_of(lines.begin(), lines.end(), [](const auto& line) {
            return line.text.find("stock-spell-marker") != std::string::npos;
        });
    }));
    auto count = [&](std::uint32_t color) {
        return std::count_if(lines.begin(), lines.end(), [&](const auto& line) { return line.color == color; });
    };
    for (const auto& line : lines) {
        CAPTURE(line.text);
        CHECK(line.color < 355);
    }
    CHECK(count(289) == 3); // every interruption, ours or another's: Spell Failures
    // The bard's cast line and the eight landings: no colour of their own (the spell channel's)
    CHECK(std::count_if(lines.begin(), lines.end(), [](const auto& line) {
              return line.color == 0 && (line.text.find("CAST") != std::string::npos ||
                                         line.text.find("LANDED") != std::string::npos);
          }) == 9);
}
