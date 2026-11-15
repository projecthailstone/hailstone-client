#include <doctest/doctest.h>

#include <stdexcept>

#include "app/login_launch.h"

using namespace hailstone;

namespace {

std::optional<std::string> no_env(const std::string&) { return std::nullopt; }

void parse(app::LoginLaunch& launch, std::initializer_list<std::string_view> args, const app::TakeEnv& env = no_env) {
    const std::span<const std::string_view> all(args.begin(), args.size());
    for (std::size_t at = 0; at < all.size(); ++at)
        if (!app::consume_login_launch_arg(launch, all, at, env))
            throw std::runtime_error("Unsupported test launcher argument.");
}

void resolve(const app::LoginLaunch& launch, bool viewer = false) {
    std::string user, password, account;
    app::resolve_login_launch(launch, user, password, account, viewer);
}

ServerProfile profile(std::string name, std::string host, int port) {
    ServerProfile result;
    result.id = name;
    result.name = name;
    result.host = host;
    result.port = port;
    return result;
}

} // namespace

TEST_CASE("launcher login: patcher's native ticket args; /server: is RoF2's world host, not a world choice") {
    app::LoginLaunch launch;
    parse(launch, {"patchme", "/TiCkEt:synthetic-ticket", "/server:world.example"});
    CHECK(launch.token_requested);
    CHECK(launch.ticket == "synthetic-ticket");
    CHECK(launch.world.empty()); // taken and ignored: the play reply names the world's address
    CHECK_NOTHROW(resolve(launch));
}

TEST_CASE("launcher login: /ticket: loses one trailing quote as in RoF2 (InitGameInstance @005fe56a)") {
    app::LoginLaunch quoted;
    parse(quoted, {"/ticket:synthetic\""});
    CHECK(quoted.ticket == "synthetic");
    app::LoginLaunch two;
    parse(two, {"/ticket:synthetic\"\""});
    CHECK(two.ticket == "synthetic\"");
    // 63 bytes and the quote: RoF2's strncpy of 63 keeps the same 63.
    const std::string sixty_three(63, 'b');
    app::LoginLaunch full;
    const std::string full_arg = "/ticket:" + sixty_three + "\"";
    parse(full, {full_arg});
    CHECK(full.ticket == sixty_three);
    app::LoginLaunch only_quote;
    CHECK_THROWS(parse(only_quote, {"/ticket:\""}));
    app::LoginLaunch explicit_ticket; // --ticket keeps its bytes as given
    parse(explicit_ticket, {"--ticket", "synthetic\""});
    CHECK(explicit_ticket.ticket == "synthetic\"");
}

TEST_CASE("launcher login: RoF2's /name: is --character") {
    app::LoginLaunch launch;
    parse(launch, {"patchme", "/ticket:synthetic-ticket", "/NAME:Somechar"});
    CHECK(launch.character == "Somechar");
    CHECK(launch.world.empty());
    app::LoginLaunch twice;
    CHECK_THROWS(parse(twice, {"/name:One", "/name:Two"}));
    app::LoginLaunch empty;
    CHECK_THROWS(parse(empty, {"/name:"}));
    const std::string long_name = "/name:" + std::string(64, 'c');
    app::LoginLaunch too_long;
    CHECK_THROWS(parse(too_long, {long_name}));
    app::LoginLaunch copy;
    copy = launch;
    CHECK(copy.character == "Somechar");
}

TEST_CASE("launcher login: unknown arguments are not named while a ticket is in use") {
    app::LoginLaunch none;
    const std::vector<std::string> unknown{"--bogus", "split-ticket-part"};
    const auto plain = app::unknown_argument_warnings(none, unknown);
    REQUIRE(plain.size() == 2);
    CHECK(plain[0] == "unknown argument: --bogus");
    app::LoginLaunch launch;
    parse(launch, {"/ticket:first-half"});
    const auto hidden = app::unknown_argument_warnings(launch, unknown);
    REQUIRE(hidden.size() == 1);
    CHECK(hidden[0].find("split-ticket-part") == std::string::npos);
    CHECK(hidden[0].find("--bogus") == std::string::npos);
    CHECK(hidden[0].starts_with("2 unknown arguments ignored"));
    CHECK(app::unknown_argument_warnings(launch, {}).empty());
}

TEST_CASE("launcher login: a ticket launch is not restarted by the updater at start, other runs are") {
    app::LoginLaunch none;
    CHECK(app::update_at_start_allowed(none));
    app::LoginLaunch launch;
    parse(launch, {"--ticket", "synthetic-ticket"});
    CHECK_FALSE(app::update_at_start_allowed(launch));
}

TEST_CASE("launcher login: explicit ticket and environment transports") {
    app::LoginLaunch direct;
    parse(direct, {"--ticket", "synthetic-ticket", "--world", "42"});
    CHECK(direct.ticket == "synthetic-ticket");
    CHECK(direct.world == "42");
    app::LoginLaunch environment;
    parse(environment, {"--ticket-env", "TEST_LAUNCH_TICKET"}, [](const std::string& name) -> std::optional<std::string> {
        if (name == "TEST_LAUNCH_TICKET") return std::string("environment-ticket");
        return std::nullopt;
    });
    CHECK(environment.ticket == "environment-ticket");
    const std::string long_ticket(64, 'a');
    app::LoginLaunch extended;
    CHECK_NOTHROW(parse(extended, {"--ticket", long_ticket}));
    app::LoginLaunch native;
    const std::string native_arg = "/ticket:" + long_ticket;
    CHECK_THROWS(parse(native, {native_arg}));
}

TEST_CASE("launcher login: malformed tickets are rejected without echoing secrets") {
    for (const auto& args : {std::vector<std::string_view>{"--ticket"}, {"/ticket:"},
                            {"--ticket-env", "UNSET"}, {"--ticket", "--world"},
                            {"--ticket", "one", "/ticket:secret-two"}}) {
        app::LoginLaunch launch;
        std::size_t at = 0;
        bool rejected = false;
        try {
            while (at < args.size()) {
                if (!app::consume_login_launch_arg(launch, args, at, no_env))
                    throw std::runtime_error("Unsupported test launcher argument.");
                ++at;
            }
        } catch (const std::runtime_error& e) {
            rejected = true;
            CHECK(std::string_view(e.what()).find("secret-two") == std::string_view::npos);
        }
        CHECK(rejected);
    }
    app::LoginLaunch launch;
    parse(launch, {"--ticket", "dummy"});
    CHECK_THROWS(resolve(launch, true));
    const std::string too_long(net::login_codec::kMaxLoginTokenBytes + 1, 'a');
    app::LoginLaunch overlong;
    CHECK_THROWS(parse(overlong, {"--ticket", too_long}));
    const std::string with_nul("a\0b", 3);
    app::LoginLaunch nul;
    CHECK_THROWS(parse(nul, {"--ticket", with_nul}));
}

TEST_CASE("launcher login: a ticket overrides legacy username password and saved-account arguments") {
    app::LoginLaunch launch;
    parse(launch, {"patchme", "/ticket:synthetic-ticket"});
    std::string user = "legacy-user", password = "synthetic-legacy-password", account = "saved-account";
    CHECK_NOTHROW(app::resolve_login_launch(launch, user, password, account, false));
    CHECK(user.empty());
    CHECK(password.empty());
    CHECK(account.empty());
    CHECK(launch.ticket == "synthetic-ticket");
    CHECK(launch.token_requested);
    app::LoginLaunch regular;
    user = "ordinary-user";
    password = "ordinary-password";
    account = "ordinary-account";
    CHECK_NOTHROW(app::resolve_login_launch(regular, user, password, account, false));
    CHECK(user == "ordinary-user");
    CHECK(password == "ordinary-password");
    CHECK(account == "ordinary-account");
}

TEST_CASE("launcher login: unrelated arguments are left for the client") {
    app::LoginLaunch launch;
    const std::vector<std::string_view> args{"--game-dir", "somewhere"};
    std::size_t at = 0;
    CHECK_FALSE(app::consume_login_launch_arg(launch, args, at, no_env));
    CHECK(at == 0);
    CHECK_FALSE(launch.token_requested);
    at = args.size();
    CHECK_FALSE(app::consume_login_launch_arg(launch, args, at, no_env));
}

TEST_CASE("launcher login: endpoint comes from explicit selection or game eqhost, never the remembered server") {
    const auto remembered = profile("remembered", "other.example", 5997);
    const auto game = profile_from_eqhost("[LoginServer]\nHost=game.example:5999\n");
    const auto matching = profile("game-profile", "game.example", 5999);
    const std::vector<ServerProfile> saved{remembered, matching};
    CHECK_THROWS(app::token_login_profile({}, nullptr, std::nullopt, true, saved, {}, 0));
    CHECK_THROWS(app::token_login_profile("missing", nullptr, game, true, saved, {}, 0));
    const auto from_game = app::token_login_profile({}, nullptr, game, true, saved, {}, 0);
    CHECK(from_game.id == "game-profile");
    CHECK(from_game.host == "game.example");
    CHECK(from_game.port == 5999);
    const auto explicit_profile = app::token_login_profile("remembered", &remembered, game, true, saved, {}, 0);
    CHECK(explicit_profile.id == "remembered");
    CHECK(explicit_profile.host == "other.example");
    const auto explicit_host = app::token_login_profile({}, nullptr, game, true, saved, "explicit.example", 6000);
    CHECK(explicit_host.id.empty());
    CHECK(explicit_host.host == "explicit.example");
    CHECK(explicit_host.port == 6000);
    CHECK_THROWS(app::token_login_profile({}, nullptr, game, true, saved, {}, 65536));
}

TEST_CASE("launcher login: eqhost.txt only from a game folder named for this run (--game-dir, EQ_GAME_DIR)") {
    const auto remembered = profile("remembered", "other.example", 5997);
    const auto game = profile_from_eqhost("[LoginServer]\nHost=game.example:5999\n");
    const std::vector<ServerProfile> saved{remembered};
    // The asset folder came from hailstone.ini: its eqhost.txt may be another server's, so nothing is sent.
    bool refused = false;
    try {
        (void)app::token_login_profile({}, nullptr, game, false, saved, {}, 0);
    } catch (const std::runtime_error& e) {
        refused = true;
        const std::string_view why = e.what();
        CHECK(why.find("--server") != std::string_view::npos);
        CHECK(why.find("--host") != std::string_view::npos);
        CHECK(why.find("--game-dir") != std::string_view::npos);
    }
    CHECK(refused);
    CHECK_THROWS(app::token_login_profile({}, nullptr, std::nullopt, false, saved, {}, 0));
    // Named for this run: its eqhost.txt is used.
    CHECK(app::token_login_profile({}, nullptr, game, true, saved, {}, 0).host == "game.example");
    // An explicit server or host needs no game folder.
    CHECK(app::token_login_profile("remembered", &remembered, game, false, saved, {}, 0).host == "other.example");
    CHECK(app::token_login_profile({}, nullptr, game, false, saved, "explicit.example", 5999).host == "explicit.example");
}

TEST_CASE("launcher login: world selection is exact, unique and includes locked worlds for server admission") {
    std::vector<net::ServerInfo> servers(2);
    servers[0].id = 7;
    servers[0].name = "First World";
    servers[1].id = 42;
    servers[1].name = "Chosen World";
    servers[1].status = 4;
    CHECK(app::launcher_world(servers, "42") == &servers[1]);
    CHECK(app::launcher_world(servers, "chosen world") == &servers[1]);
    CHECK(app::launcher_world(servers, "Chosen") == nullptr);
    CHECK(app::launcher_world(servers, "999") == nullptr);
    CHECK(app::launcher_world(servers, "") == nullptr);
    servers[0].name = "Chosen World";
    CHECK(app::launcher_world(servers, "Chosen World") == nullptr);
    servers[0].id = 42;
    CHECK(app::launcher_world(servers, "42") == nullptr);
}
