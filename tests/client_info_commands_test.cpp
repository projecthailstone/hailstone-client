// The original's client, help and report commands (net/connection/game_connection_client.cpp, app/client_app_info.cpp,
// game/output_file.h): the packets, /outputfile's files, and against the mock EQEmu /report, /guidehelp, /uptime,
// /veteranReward, /acompare, /help and /autojoin.

#include <doctest/doctest.h>

#include <map>

#include "game/eq_strings.h"
#include "game/inventory.h"
#include "game/output_file.h"
#include "mock_server/harness.h"
#include "net/protocol/rof2/packets/player_packets.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

TEST_CASE("OP_Feedback, OP_Report and /uptime's answer as the original lays them out") {
    Feedback f;
    f.name = "Gennro";
    f.y = 1.5f;
    f.x = -2.0f;
    f.z = 3.25f;
    f.heading = 128;
    f.text = "nice zone";
    AppPacket p = encode(f);
    CHECK(rof2::from_wire(p.opcode) == rof2::Op::Feedback);
    REQUIRE(p.data.size() == kFeedbackSize);
    CHECK(static_cast<char>(p.data[0]) == 'G');
    CHECK(static_cast<char>(p.data[0x78]) == 'n'); // the text at +0x78
    auto back = decode_feedback(p);
    REQUIRE(back);
    CHECK(back->name == "Gennro");
    CHECK(back->y == 1.5f);
    CHECK(back->x == -2.0f);
    CHECK(back->z == 3.25f);
    CHECK(back->heading == 128);
    CHECK(back->text == "nice zone");

    // The report: the text and its NUL, cut at 0x2004 characters.
    AppPacket r = encode_report(std::string(0x3000, 'a'));
    CHECK(rof2::from_wire(r.opcode) == rof2::Op::Report);
    CHECK(r.data.size() == 0x2005);

    // "%02d:%02d:%02d:%02d": days, hours, minutes, seconds.
    CHECK(format_uptime(0) == "00:00:00:00");
    CHECK(format_uptime(90061) == "01:01:01:01");
    CHECK(format_uptime(3 * 86400 + 23 * 3600 + 59 * 60 + 59) == "03:23:59:59");
    auto u = decode_uptime(encode(Uptime{100, 61}));
    REQUIRE(u);
    CHECK(u->world_seconds == 100);
    CHECK(u->zone_seconds == 61);
    CHECK(encode_veteran_reward_claim().data.size() == 0x48);
    CHECK(encode_achievement_compare(7, 9).data.size() == 8);
}

TEST_CASE("/outputfile: the writers' words and files") {
    using game::OutputFileKind;
    CHECK(game::output_file_kind("Inventory") == OutputFileKind::Inventory);
    CHECK(game::output_file_kind("GUILDBANK") == OutputFileKind::GuildBank);
    CHECK(game::output_file_kind("guildhall") == OutputFileKind::GuildHall);
    CHECK(game::output_file_kind("inv") == OutputFileKind::None); // stricmp: whole words only
    std::tm t{};
    t.tm_year = 126;
    t.tm_mon = 9;
    t.tm_mday = 5;
    t.tm_hour = 7;
    t.tm_min = 8;
    t.tm_sec = 9;
    CHECK(game::output_file_date(t) == "20261005-070809");

    // InventoryDump_WriteItem: an empty slot, a stack, a bag with its slots, an item's visible sockets only.
    game::EqStrings strings;
    std::string out;
    game::dump_inventory_item(out, nullptr, "Charm");
    CHECK(out == "Charm\tEmpty\t0\t0\t0\n");
    Item bag;
    bag.name = "Backpack";
    bag.id = 1001;
    bag.item_class = 1;
    bag.bag_slots = 2;
    Item arrows;
    arrows.name = "Arrow";
    arrows.id = 8005;
    arrows.max_stack = 100;
    arrows.stack_count = 40;
    bag.contents.push_back({1, arrows});
    Item sword;
    sword.name = "Sword";
    sword.id = 5;
    sword.aug_sockets[0] = {7, 1, 0};
    sword.aug_sockets[1] = {7, 0, 0}; // not visible: left out
    Item aug;
    aug.name = "Gem";
    aug.id = 77;
    sword.contents.push_back({0, aug});
    game::Inventory inv;
    REQUIRE(inv.put(ItemSlot::possessions(kSlotGeneral1), bag));
    REQUIRE(inv.put(ItemSlot::possessions(kSlotPrimary), sword));
    std::string file = game::inventory_file(inv, strings);
    CHECK(file.rfind("Location\tName\tID\tCount\tSlots\n", 0) == 0);
    CHECK(file.find("[3488]\tBackpack\t1001\t1\t2\n[3488]-Slot1\tEmpty\t0\t0\t0\n[3488]-Slot2\tArrow\t8005\t40\t6\n") !=
          std::string::npos);
    CHECK(file.find("[569]\tSword\t5\t1\t6\n[569]-Slot1\tGem\t77\t1\t6\n[570]\tEmpty") != std::string::npos);
    CHECK(file.find("[3496]24\tEmpty\t0\t0\t0\n[3497]1\tEmpty\t0\t0\t0\n[3497]2\tEmpty\t0\t0\t0\n") != std::string::npos);

    // The spell book: valid ids the table knows, "<level>\t<name>".
    game::SpellData heal;
    heal.id = 12;
    heal.name = "Healing";
    std::map<std::uint32_t, const game::SpellData*> table{{12, &heal}};
    auto spell = [&](std::uint32_t id) -> const game::SpellData* {
        auto it = table.find(id);
        return it == table.end() ? nullptr : it->second;
    };
    const std::vector<std::uint32_t> book{12, 0xffffffff, 99, 12};
    CHECK(game::spellbook_file(book, spell, [](const game::SpellData&) { return std::uint8_t{4}; }) ==
          "4\tHealing\n4\tHealing\n");

    // The raid roster: "empty" rows skipped; the unassigned with group 0.
    std::string raid = game::raid_roster_file({{"1", "Gennro", "60", "Cleric", "", "Raid Leader"}, {"1", "empty"}},
                                              {{"", "Loner", "10", "Warrior", "", ""}}, "empty",
                                              [](const std::string& n) { return n == "Loner" ? "note" : ""; });
    CHECK(raid == "1\tGennro\t60\tCleric\tRaid Leader\t\n0\tLoner\t10\tWarrior\t\tnote\n");

    // The guild roster: the member list's columns but the last, then the tribute and notes lists' from column 1.
    game::DumpList members{{{"Gennro", "60", "Cleric", "Leader", "", "10/25/26", "Lobby", "hi", "", ""}}, 10};
    game::DumpList tribute{{}, 5};
    game::DumpList notes{{{"Gennro", "hi", ""}}, 3};
    CHECK(game::guild_roster_file(members, tribute, notes) ==
          "Gennro\t60\tCleric\tLeader\t\t10/25/26\tLobby\thi\t\t\t\t\t\thi\t\n");

    // The guild bank: the permission's name, a single member's name for permission 1.
    std::vector<std::optional<GuildBankItem>> main(200), deposit(40);
    GuildBankItem a;
    a.name = "Shield";
    a.item_id = 9;
    a.quantity = 1;
    a.permission = GuildBankPermission::SingleMember;
    a.who_for = "Gennro";
    main[2] = a;
    a.permission = GuildBankPermission::Public;
    deposit[0] = a;
    CHECK(game::guild_bank_file(main, deposit, strings) ==
          "Location\tName\tID\tCount\tPermission\nBank-003\tShield\t9\t1\tGennro\nDeposit-01\tShield\t9\t1\t[6106]\n");
}

TEST_CASE("mock server: /report once per zone-in, /guidehelp, /uptime, /veteranReward, /acompare, /help, /autojoin") {
    Rig rig;
    auto& conn = rig.conn;
    auto& server = rig.server;
    std::map<std::string, std::string> ini;
    conn.set_ui_ini(
        [&](std::string_view s, std::string_view k) -> std::optional<std::string> {
            auto it = ini.find(std::string(s) + "/" + std::string(k));
            return it == ini.end() ? std::nullopt : std::optional<std::string>(it->second);
        },
        [&](std::string_view s, std::string_view k, const std::string& v) { ini[std::string(s) + "/" + std::string(k)] = v; });
    conn.set_visible_chat_text([] { return std::string("line one\n\n===================================\n"); });
    REQUIRE(rig.to_zone());
    rig.chat();

    // /report: no player: 12943; the report; a second one: 12945.
    conn.send_command("/report");
    conn.send_command("/report Badguy");
    REQUIRE(rig.until([&] { return MockEqServer::count(server.zone_log, rof2::Op::Report) == 1; }));
    for (const auto& p : server.zone_log)
        if (rof2::from_wire(p.opcode) == rof2::Op::Report) {
            std::string text(reinterpret_cast<const char*>(p.data.data()), p.data.size());
            CHECK(text == std::string("Badguy|Gennro|line one\n\n===================================\n") + '\0');
        }
    conn.send_command("/report Badguy");
    auto lines = rig.chat();
    CHECK(has_line(lines, "[12943]"));
    CHECK(has_line(lines, "[12944] Badguy"));
    CHECK(has_line(lines, "[12945]"));

    // /guidehelp: OP_Petition with the text and 3560.
    conn.send_command("/guidehelp");
    conn.send_command("/guidehelp a quest is stuck");
    REQUIRE(rig.until([&] { return MockEqServer::count(server.zone_log, rof2::Op::Petition) == 1; }));
    lines = rig.chat();
    CHECK(has_line(lines, "[3555]"));
    CHECK(has_line(lines, "[3560] a quest is stuck"));

    // /uptime: opcode 0x344f alone; the answer's two lines.
    conn.send_command("/uptime");
    REQUIRE(rig.until([&] {
        for (const auto& p : server.zone_log)
            if (p.opcode == kOpUptime && p.data.empty()) return true;
        return false;
    }));
    server.zone.send_reliable(encode(Uptime{90061, 61}));
    REQUIRE(rig.until([&] { return has_line(rig.chat(), "[4103] 00:00:01:01"); }));

    // /veteranReward: dialog 0x7e; yes: 3600 and 0x7d89.
    conn.send_command("/veteranReward");
    REQUIRE(conn.confirmation());
    CHECK(conn.confirmation()->id == kConfirmVeteranReward);
    CHECK(conn.confirmation()->timeout_ms == 60000);
    conn.answer_confirmation(true);
    REQUIRE(rig.until([&] {
        for (const auto& p : server.zone_log)
            if (p.opcode == kOpVeteranRewardClaim && p.data.size() == 0x48) return true;
        return false;
    }));
    CHECK(has_line(rig.chat(), "[3600]"));

    // /acompare without a target: 5203.
    conn.send_command("/acompare");
    CHECK(has_line(rig.chat(), "[5203]"));

    // /help: the help text; a class (its eqstr name) lists the table's commands of that category.
    conn.send_command("/help");
    lines = rig.chat();
    CHECK(has_line(lines, "[13125]"));
    CHECK(has_line(lines, "[13130]"));
    conn.send_command("/help [13122]"); // guild (13122 "guild" without the string table)
    lines = rig.chat();
    CHECK(has_line(lines, "[13131]"));
    CHECK(has_line(lines, "  /guildinvite"));
    CHECK_FALSE(has_line(lines, "  /who"));

    // /autojoin: the UI ini's ChatManager / ChannelAutoJoin.
    conn.send_command("/autojoin");
    CHECK(has_line(rig.chat(), "Usage: /autojoin <channel name>"));
    conn.send_command("/autojoin general,trade");
    CHECK(ini["ChatManager/ChannelAutoJoin"] == "general,trade");
    CHECK(has_line(rig.chat(), "Set channels to be joined at login."));

    // /lfguild and /feedback open their windows; /paperdoll and /vreport do nothing.
    conn.send_command("/lfguild");
    CHECK(conn.lfguild().open);
    conn.send_command("/feedback");
    CHECK(conn.feedback_open());
    conn.send_feedback("hello");
    CHECK_FALSE(conn.feedback_open());
    REQUIRE(rig.until([&] { return MockEqServer::count(server.zone_log, rof2::Op::Feedback) == 1; }));
    conn.send_command("/paperdoll");
    conn.send_command("/vreport");
    CHECK(rig.chat().empty());
}
