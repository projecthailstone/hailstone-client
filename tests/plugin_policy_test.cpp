// plugin/policy.h: the server's rules from its hailstone.cfg, and net/settings_packets.h.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "core/sha256.h"
#include "net/settings_packets.h"
#include "plugin/policy.h"

using namespace hailstone;
using plugin::FileKind;
using plugin::Policy;
namespace fs = std::filesystem;

TEST_CASE("hailstone.cfg: every setting, defaults for the rest, warnings for what it does not know") {
    std::vector<std::string> warnings;
    auto r = plugin::parse_server_cfg(R"(
; a comment
[scripts]
player = on
player_caps = read, ui     ; trailing comment
run = hud, autoloot

[Plugins]
Player = Off

[assets]
player = off

[socials]
mode = full
caps = read,target,actions,move
press_commands = 3
faces = no

[web]
allow = Example.org, wiki.example.net
)",
                                      &warnings);
    CHECK(warnings.empty());
    CHECK(r.configured);
    CHECK(r.player_script_caps == (plugin::kCapRead | plugin::kCapUi));
    CHECK(r.player_plugin_caps == 0);
    CHECK_FALSE(r.player_assets);
    CHECK(r.run_scripts == std::vector<std::string>{"hud", "autoloot"});
    CHECK(r.social_mode == plugin::ButtonMode::Full);
    CHECK(r.social_caps == (plugin::kCapRead | plugin::kCapTarget | plugin::kCapActions | plugin::kCapMove));
    CHECK(r.social_press_commands == 3);
    CHECK_FALSE(r.social_faces);
    CHECK(r.web_allow == std::vector<std::string>{"example.org", "wiki.example.net"});

    auto d = plugin::parse_server_cfg("");
    CHECK(d.configured);
    CHECK(d.player_script_caps == 0);
    CHECK(d.player_plugin_caps == 0);
    CHECK(d.player_assets);
    CHECK(d.social_mode == plugin::ButtonMode::Press);
    CHECK(d.web_allow.empty());

    // "player = on" alone: every capability.
    CHECK(plugin::parse_server_cfg("[plugins]\nplayer = yes\n").player_plugin_caps == plugin::kCapAll);

    warnings.clear();
    auto w = plugin::parse_server_cfg("[scripts]\nplayer = maybe\nplayer_caps = read,fly\ncolour = red\n[macros]\nx = 1\n"
                                      "[socials]\nmode = loud\npress_commands = -1\n",
                                      &warnings);
    CHECK(w.player_script_caps == 0);
    CHECK(warnings.size() == 7); // maybe, fly, colour, [macros], loud, -1, and player_caps without player = on
    // A section that is not the client's is kept for scripts (hailstone.server_rule).
    CHECK(w.script_sections.size() == 1);
    CHECK(w.script_sections["macros"]["x"] == "1");
    auto a = plugin::parse_server_cfg("[AutoCombat]\nHunter = off   ; no unattended hunting\nassist = on\n");
    CHECK(a.script_sections["autocombat"]["hunter"] == "off");
    CHECK(a.script_sections["autocombat"]["assist"] == "on");
    CHECK(r.script_sections.empty());
}

TEST_CASE("server_rules: none without hailstone.cfg; its own scripts and plugins listed; a changed cfg gives defaults") {
    fs::path dir = fs::temp_directory_path() / "hailstone_server_rules_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string cfg = "[scripts]\nplayer = on\n";
    std::ofstream(dir / "hailstone.cfg", std::ios::binary) << cfg;
    Sha256Digest script = sha256(std::string_view("print(1)"));
    std::vector<plugin::ListedFile> files{{"assets/spells_us.txt", sha256(std::string_view("s"))},
                                          {"scripts/hud.lua", script},
                                          {"plugins/MQ2Hud.so", sha256(std::string_view("p"))}};

    auto none = plugin::server_rules(dir, files);
    CHECK_FALSE(none.configured);
    CHECK(none.dir == dir);
    CHECK(none.server_files.size() == 2);

    files.push_back({"hailstone.cfg", sha256(cfg)});
    auto r = plugin::server_rules(dir, files);
    CHECK(r.configured);
    CHECK(r.player_script_caps == plugin::kCapAll);
    REQUIRE(r.server_files.size() == 2);
    CHECK(r.server_files[0].kind == FileKind::Script);
    CHECK(r.server_files[1].kind == FileKind::Plugin);

    std::ofstream(dir / "hailstone.cfg", std::ios::binary) << "[scripts]\nplayer = on\n[plugins]\nplayer = on\n";
    std::vector<std::string> warnings;
    auto changed = plugin::server_rules(dir, files, &warnings);
    CHECK(changed.configured);
    CHECK(changed.player_script_caps == 0);
    CHECK(changed.player_plugin_caps == 0);
    CHECK(warnings.size() == 1);
    fs::remove_all(dir);
}

TEST_CASE("Policy: nothing before the rules; everything without a cfg; the server's own, the player's as allowed") {
    Policy pol;
    Sha256Digest server = sha256(std::string_view("server's")), mine = sha256(std::string_view("mine"));
    CHECK(pol.caps(FileKind::Script, mine) == 0);
    CHECK(pol.button_rules().mode == plugin::ButtonMode::Off);

    pol.set(plugin::Rules{});
    CHECK(pol.caps(FileKind::Script, mine) == plugin::kCapAll);
    CHECK(pol.caps(FileKind::Plugin, mine) == plugin::kCapAll);
    CHECK(pol.button_rules().mode == plugin::ButtonMode::Full);
    CHECK(pol.button_rules().caps ==
          (plugin::kCapAll & ~(plugin::kCapPackets | plugin::kCapExtend | plugin::kCapLogin | plugin::kCapNet)));

    plugin::Rules r;
    r.configured = true;
    r.player_script_caps = plugin::kCapRead;
    r.server_files.push_back({FileKind::Plugin, "plugins/x.so", server});
    pol.set(r);
    CHECK(pol.caps(FileKind::Plugin, server) == plugin::kCapAll);
    CHECK(pol.server_file(FileKind::Plugin, server));
    CHECK_FALSE(pol.server_file(FileKind::Script, server));
    CHECK(pol.caps(FileKind::Plugin, mine) == 0);
    CHECK(pol.caps(FileKind::Script, mine) == plugin::kCapRead);
    CHECK(pol.refusal(FileKind::Plugin) == "this server runs only its own plugins (its hailstone.cfg)");
    pol.clear();
    CHECK(pol.caps(FileKind::Plugin, server) == 0);
}

TEST_CASE("Policy: provisional rules before the zone-in run only what has 'login'") {
    Policy pol;
    Sha256Digest server = sha256(std::string_view("server's")), mine = sha256(std::string_view("mine"));
    plugin::Rules r;
    r.provisional = true; // no hailstone.cfg known from the last login
    pol.set(r);
    CHECK(pol.caps(FileKind::Plugin, mine) == plugin::kCapAll);
    CHECK(pol.button_rules().mode == plugin::ButtonMode::Off); // socials from the zone-in

    r.configured = true;
    r.player_plugin_caps = plugin::kCapRead;
    r.server_files.push_back({FileKind::Plugin, "plugins/x.so", server});
    pol.set(r);
    CHECK(pol.caps(FileKind::Plugin, server) == plugin::kCapAll);
    CHECK(pol.caps(FileKind::Plugin, mine) == 0);
    CHECK(pol.refusal(FileKind::Plugin).find("'login'") != std::string::npos);
    r.player_plugin_caps = plugin::kCapRead | plugin::kCapLogin;
    pol.set(r);
    CHECK(pol.caps(FileKind::Plugin, mine) == (plugin::kCapRead | plugin::kCapLogin));

    // After the zone-in 'login' is not needed.
    r.provisional = false;
    r.player_plugin_caps = plugin::kCapRead;
    pol.set(r);
    CHECK(pol.caps(FileKind::Plugin, mine) == plugin::kCapRead);
    CHECK(plugin::parse_caps("read,login") == (plugin::kCapRead | plugin::kCapLogin));
    CHECK(plugin::caps_text(plugin::kCapLogin | plugin::kCapExtend) == "extend,login");
}

TEST_CASE("Policy: Lua social rules from [socials]") {
    Policy pol;
    plugin::Rules r = plugin::parse_server_cfg("[socials]\nmode = press\ncaps = all\npress_commands = 2\n");
    pol.set(r);
    auto b = pol.button_rules();
    CHECK(b.mode == plugin::ButtonMode::Press);
    CHECK(b.caps == (plugin::kCapAll & ~(plugin::kCapEvents | plugin::kCapUi | plugin::kCapWeb | plugin::kCapPackets |
                                     plugin::kCapExtend | plugin::kCapLogin | plugin::kCapNet)));
    CHECK(b.press_commands == 2);
    CHECK(b.faces);
    pol.set(plugin::parse_server_cfg("[socials]\nmode = full\ncaps = all\n"));
    CHECK(pol.button_rules().caps ==
          (plugin::kCapAll & ~(plugin::kCapPackets | plugin::kCapExtend | plugin::kCapLogin | plugin::kCapNet)));
    pol.set(plugin::parse_server_cfg("[socials]\nmode = off\n"));
    CHECK(pol.button_rules().mode == plugin::ButtonMode::Off);
    CHECK(pol.button_rules().caps == 0);
}

TEST_CASE("command_caps: EQ commands by their handler, abbreviations too; text is chat") {
    using namespace plugin;
    CHECK(command_caps("/target a_bat") == kCapTarget);
    CHECK(command_caps("/assist") == kCapTarget);
    CHECK(command_caps("/cast 1") == kCapActions);
    CHECK(command_caps("/disc Fortitude") == kCapActions);
    CHECK(command_caps("/useitem 22") == kCapActions);
    CHECK(command_caps("/attack on") == kCapActions);
    // Hailstone's own item commands (not in RoF2's table) need what using an item needs, by their whole word.
    CHECK(command_caps("/destroycursor 123") == kCapActions);
    CHECK(command_caps("/STOWCURSOR 123") == kCapActions);
    CHECK(command_caps("/destroy") == kCapActions);
    CHECK(command_caps("/Destroy") == kCapActions);
    CHECK(command_caps("/stowcursorx 123") == kCapCommands);
    CHECK(command_caps("/destro") == kCapCommands); // no prefix match: the client runs it as no command
    CHECK(command_caps("/finditem sword") == kCapCommands); // a window, not an item move
    CHECK(command_caps("/sit") == kCapMove);
    CHECK(command_caps("/follow") == kCapMove);
    CHECK(command_caps("/g hello") == kCapChat);
    CHECK(command_caps("/tell bob hi") == kCapChat);
    CHECK(command_caps("hello there") == kCapChat);
    CHECK(command_caps(";tell x hi") == kCapChat);
    CHECK(command_caps("/who all") == kCapCommands);
    CHECK(command_caps("/stick 10") == kCapCommands); // an MQ command: its handler checks movement itself
    CHECK(command_caps("/url https://example.org") == kCapWeb);
    CHECK(command_caps("/www example.org") == kCapWeb);
    CHECK(command_caps("") == 0);
}

TEST_CASE("caps text round trip") {
    using namespace plugin;
    CHECK(caps_text(kCapRead | kCapUi) == "read,ui");
    CHECK(caps_text(kCapWeb) == "web");
    CHECK(parse_caps("web") == kCapWeb);
    CHECK(caps_text(0) == "none");
    CHECK(parse_caps("read, ui,bogus") == (kCapRead | kCapUi));
    CHECK(parse_caps("all") == kCapAll);
    CHECK(parse_caps(caps_text(kCapAll)) == kCapAll);
}

TEST_CASE("settings packets: request layout, answer round trip, later fields ignored") {
    auto req = net::encode_hailstone_settings_request();
    CHECK(req.opcode == 0x1432);
    REQUIRE(req.data.size() == 6);
    CHECK(req.data[0] == std::byte{'N'});
    CHECK(req.data[4] == std::byte{1});

    net::HailstoneSettings v;
    v.flags = 0x5; // no flag is defined now; any value goes through
    auto p = net::encode(v);
    CHECK(p.opcode == 0x1433);
    REQUIRE(p.data.size() == 10);
    auto d = net::decode_hailstone_settings(p);
    REQUIRE(d);
    CHECK(d->flags == 0x5u);

    auto longer = p;
    longer.data.push_back(std::byte{7}); // a later version's field
    REQUIRE(net::decode_hailstone_settings(longer));
    auto bad = p;
    bad.data.resize(9);
    CHECK_FALSE(net::decode_hailstone_settings(bad));
    bad = p;
    bad.data[4] = std::byte{2};
    CHECK_FALSE(net::decode_hailstone_settings(bad));
    bad = p;
    bad.opcode = 0x1421; // another Hailstone opcode
    CHECK_FALSE(net::decode_hailstone_settings(bad));
}

TEST_CASE("policy: an add-on's caps are a ceiling on what its files get") {
    using namespace hailstone::plugin;
    CHECK(parse_caps_strict("read, ui") == (kCapRead | kCapUi));
    CHECK(parse_caps_strict("all") == kCapAll);
    CHECK(parse_caps_strict("none") == 0u);
    CHECK_FALSE(parse_caps_strict("read,flying"));
    CHECK(caps_described(kCapRead | kCapChat).size() == 2);
    CHECK(caps_described(kCapAll).size() == 13);

    const Sha256Digest addon = sha256(std::string_view("addon.lua")), own = sha256(std::string_view("mine.lua"));
    const Sha256Digest server = sha256(std::string_view("server.lua"));
    Policy p;
    p.set_ceilings({{FileKind::Script, addon, kCapRead | kCapUi}});

    // A server without a hailstone.cfg: the player's own get everything, the add-on its ceiling.
    Rules open;
    open.server_files = {{FileKind::Script, "scripts/server.lua", server}};
    p.set(open);
    CHECK(p.caps(FileKind::Script, own) == kCapAll);
    CHECK(p.caps(FileKind::Script, addon) == (kCapRead | kCapUi));
    CHECK(p.caps(FileKind::Script, server) == kCapAll);
    CHECK(p.caps(FileKind::Plugin, addon) == kCapAll); // a ceiling is for its kind
    CHECK(p.ceiling(FileKind::Script, addon) == (kCapRead | kCapUi));
    CHECK_FALSE(p.ceiling(FileKind::Script, own));

    // A server that allows less: the less of the two.
    Rules strict = parse_server_cfg("[scripts]\nplayer = on\nplayer_caps = read,actions\n");
    p.set(strict);
    CHECK(p.caps(FileKind::Script, own) == (kCapRead | kCapActions));
    CHECK(p.caps(FileKind::Script, addon) == kCapRead);
    // The ceilings stay over a change of server and a disconnect.
    p.clear();
    p.set(open);
    CHECK(p.caps(FileKind::Script, addon) == (kCapRead | kCapUi));

    // Before the zone-in only what has 'login'; the ceiling takes it away too.
    Rules early = open;
    early.provisional = true;
    p.set(early);
    CHECK(p.caps(FileKind::Script, own) == kCapAll);
    CHECK(p.caps(FileKind::Script, addon) == 0u);

    // A file both of two add-ons ship gets what both allow.
    p.set(open);
    p.set_ceilings({{FileKind::Script, addon, kCapRead | kCapUi}, {FileKind::Script, addon, kCapRead | kCapChat}});
    CHECK(p.caps(FileKind::Script, addon) == kCapRead);
    // The server's own file with the same bytes as an add-on's is the server's.
    p.set_ceilings({{FileKind::Script, server, kCapRead}});
    CHECK(p.caps(FileKind::Script, server) == kCapAll);
}
