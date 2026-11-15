// LoginClient against the mock server's login script (docs/protocol/login.md), and the hand-off from the login
// ticket to the world server: login, server list, play, then GameConnection to the character list.

#include <doctest/doctest.h>

#include "app/login_token_session.h"
#include "core/wipe.h"
#include "mock_server/harness.h"
#include "net/login.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::test;

namespace {

struct LoginRig {
    MockEqServer server;
    LoginClient client;

    explicit LoginRig(MockConfig config = MockConfig::standard()) : server(std::move(config)) {}
    LoginRig(MockConfig config, LoginClient::Options options) : server(std::move(config)), client(std::move(options)) {}
    // Logged in with the list shown, then Play on the mock's world.
    bool to_play() {
        start("tester", "secret");
        if (!until_state(LoginState::LoggedIn)) return false;
        client.request_server_list();
        if (!until_state(LoginState::ServerList)) return false;
        client.request_play(1);
        return true;
    }

    void step() {
        client.update();
        server.poll();
    }
    bool until(const std::function<bool()>& done, std::chrono::milliseconds limit = 8s) {
        return pump([&] { step(); }, done, limit);
    }
    bool until_state(LoginState s) {
        return until([&] { return client.state() == s || client.state() == LoginState::Failed; }) &&
               client.state() == s;
    }
    void start(const std::string& user, const std::string& password) {
        client.start("127.0.0.1", server.login.port(), user, password);
    }
};

} // namespace

TEST_CASE("mock login: login, server list and play hand over a world ticket") {
    LoginRig rig;
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    // The credentials went out DES-encrypted; the mock decrypted them.
    CHECK(rig.server.login_user == "tester");
    CHECK(rig.server.login_password == "secret");
    CHECK(rig.server.login_token.empty());
    CHECK_FALSE(rig.server.login_token_mode);
    CHECK(rig.client.login_result().account_id == 77);
    CHECK(rig.client.login_result().session_key == "ABCDEFGHIJ");
    CHECK(rig.client.sequence() == 3); // handshake 2, login 3 (docs/protocol/login.md, Sequence numbers)

    CHECK(rig.client.servers_serial() == 0);
    rig.client.request_server_list();
    REQUIRE(rig.until_state(LoginState::ServerList));
    CHECK(rig.client.sequence() == 4);
    CHECK(rig.client.servers_serial() != 0); // (the server select is built again when it changes)
    REQUIRE(rig.client.servers().size() == 1);
    CHECK(rig.client.servers()[0].name == "Mock EQEmu");
    CHECK(rig.client.servers()[0].address == "127.0.0.1");
    CHECK(rig.client.servers()[0].id == 1);
    CHECK(rig.client.servers()[0].status == 0);

    rig.client.request_play(1);
    REQUIRE(rig.until_state(LoginState::PlayAccepted));
    CHECK(rig.client.sequence() == 5);
    auto ticket = rig.client.ticket();
    REQUIRE(ticket);
    CHECK(ticket->account_id == 77);
    CHECK(ticket->session_key == "ABCDEFGHIJ");
    CHECK(ticket->world_host == "127.0.0.1");
    // The client always connects to world on 9000 (docs/protocol/login.md, Hand-off); the mock's world listens on
    // an ephemeral port, so the test points the ticket at it.
    CHECK(ticket->world_port == kWorldPort);
    CHECK(ticket->server_id == 1);
    CHECK(ticket->server_name == "Mock EQEmu");
    rig.client.close();
}

TEST_CASE("mock login: a launcher ticket authenticates through the world character list") {
    auto config = MockConfig::standard();
    config.login_token = "lt1_" + std::string(43, 'A'); // synthetic patcher-shaped ticket
    config.returned_login_token = "synthetic-next-login-token";
    const std::string token = config.login_token;
    LoginRig rig(std::move(config));
    rig.client.start_token("127.0.0.1", rig.server.login.port(), token);
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    CHECK(rig.server.login_requests == 1);
    CHECK(rig.server.login_user.empty());
    CHECK(rig.server.login_password.empty());
    CHECK(rig.server.login_token_mode);
    CHECK(rig.server.login_token == token);
    CHECK(rig.client.login_result().token == "synthetic-next-login-token");
    rig.client.request_server_list();
    REQUIRE(rig.until_state(LoginState::ServerList));
    rig.client.request_play(1);
    REQUIRE(rig.until_state(LoginState::PlayAccepted));
    REQUIRE(rig.client.ticket());
    WorldTicket ticket = *rig.client.ticket();
    CHECK(ticket.account_id == 77);
    CHECK(ticket.session_key == "ABCDEFGHIJ");
    ticket.world_port = rig.server.world.port();
    rig.client.close();
    CHECK(rig.client.state() == LoginState::Idle);
    CHECK(rig.client.login_result().token.empty());
    CHECK(rig.client.login_result().session_key.empty());
    CHECK_FALSE(rig.client.ticket());

    EmptyGameDir dir;
    GameDir game(dir.path());
    GameConnection conn(game);
    conn.start(ticket);
    REQUIRE(pump([&] { conn.update(); rig.server.poll(); },
                 [&] { return conn.state() == ConnectionState::CharacterSelect; }));
    REQUIRE(rig.server.login_infos.size() == 1);
    CHECK(rig.server.login_infos[0].account_id == "77");
    CHECK(rig.server.login_infos[0].session_key == "ABCDEFGHIJ");
    REQUIRE(conn.characters().size() == 1);
    CHECK(conn.characters()[0].name == "Gennro");
}

TEST_CASE("mock login: token authentication handles a fragmented maximum-size opaque ticket") {
    auto config = MockConfig::standard();
    config.login_token = std::string(login_codec::kMaxLoginTokenBytes, 'x');
    const auto token = config.login_token;
    LoginRig rig(std::move(config));
    rig.client.start_token("127.0.0.1", rig.server.login.port(), token);
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    CHECK(rig.server.login_token == token);
    CHECK(rig.server.login_requests == 1);
}

TEST_CASE("mock login: only the server's replacement token can authorize an explicit next sign-in") {
    auto config = MockConfig::standard();
    config.login_token = "synthetic-launcher-ticket";
    config.returned_login_token = "synthetic-server-next-token";
    LoginRig rig(std::move(config));
    app::LoginTokenSession session;
    rig.client.start_token("127.0.0.1", rig.server.login.port(), "synthetic-launcher-ticket");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    REQUIRE(session.remember("127.0.0.1", rig.server.login.port(), rig.client.login_result().token));
    rig.client.close();

    rig.server.config.login_token = "synthetic-server-next-token";
    rig.server.config.returned_login_token = "synthetic-server-rotated-token";
    std::string next;
    REQUIRE(session.take("127.0.0.1", rig.server.login.port(), next));
    CHECK_FALSE(session.available("127.0.0.1", rig.server.login.port()));
    rig.client.start_token("127.0.0.1", rig.server.login.port(), next);
    core::wipe(next);
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    CHECK(rig.server.login_requests == 2);
    CHECK(rig.server.login_token == "synthetic-server-next-token");
    CHECK(rig.server.login_user.empty());
    CHECK(rig.server.login_password.empty());
    REQUIRE(session.remember("127.0.0.1", rig.server.login.port(), rig.client.login_result().token));
    rig.client.request_server_list();
    REQUIRE(rig.until_state(LoginState::ServerList));
    rig.client.request_play(1);
    REQUIRE(rig.until_state(LoginState::PlayAccepted));
    REQUIRE(rig.client.ticket());
    CHECK(rig.client.ticket()->account_id == 77);
    CHECK(rig.client.ticket()->session_key == "ABCDEFGHIJ");
    rig.client.close();

    // The returned token is also consumed locally before sending. A server refusal does not make it replayable.
    rig.server.config.login_token = "another-current-token";
    REQUIRE(session.take("127.0.0.1", rig.server.login.port(), next));
    CHECK(next == "synthetic-server-rotated-token");
    rig.client.start_token("127.0.0.1", rig.server.login.port(), next);
    core::wipe(next);
    REQUIRE(rig.until_state(LoginState::Failed));
    CHECK(rig.server.login_requests == 3);
    CHECK(rig.client.error_id() == login_error::BadPassword);
    CHECK(rig.client.login_result().token.empty());
    CHECK_FALSE(session.take("127.0.0.1", rig.server.login.port(), next));
    CHECK(next.empty());
    rig.step();
    CHECK(rig.server.login_requests == 3);
}

TEST_CASE("mock login: invalid tokens fail locally before opening a session") {
    LoginRig rig;
    for (const auto& token : {std::string(), std::string("one\0two", 7),
                              std::string(login_codec::kMaxLoginTokenBytes + 1, 'x')}) {
        rig.client.start_token("127.0.0.1", rig.server.login.port(), token);
        CHECK(rig.client.state() == LoginState::Failed);
        CHECK(rig.client.error_id() == 0);
        CHECK(rig.client.error() == "The login ticket is empty, too long or contains a NUL byte.");
        rig.step();
    }
    CHECK(rig.server.login.sessions() == 0);
    CHECK(rig.server.login_requests == 0);
}

TEST_CASE("mock login: an empty host list refuses token authentication without a session") {
    LoginRig rig;
    rig.client.start_token(std::vector<LoginClient::Host>{}, "synthetic-ticket");
    CHECK(rig.client.state() == LoginState::Failed);
    CHECK(rig.client.error_id() == login_error::NoConnection);
    CHECK(rig.client.login_result().session_key.empty());
    rig.step();
    CHECK(rig.server.login.sessions() == 0);
    CHECK(rig.server.login_requests == 0);
}

TEST_CASE("mock login: a refused or unsupported token never falls back to password authentication") {
    auto config = MockConfig::standard();
    SUBCASE("the issuer refuses this ticket") { config.login_token = "accepted-ticket"; }
    SUBCASE("the server has token login disabled") {}
    LoginRig rig(std::move(config));
    // A successful earlier password login must not become a fallback for the launcher's ticket.
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    rig.client.start_token("127.0.0.1", rig.server.login.port(), "refused-ticket");
    REQUIRE(rig.until_state(LoginState::Failed));
    CHECK(rig.client.error_id() == login_error::BadPassword);
    CHECK(rig.client.login_result().session_key.empty());
    CHECK(rig.client.login_result().token.empty());
    CHECK(rig.server.login_requests == 2);
    CHECK(rig.server.login_token_mode);
    CHECK(rig.server.login_user.empty());
    CHECK(rig.server.login_password.empty());
    rig.step();
    CHECK(rig.server.login_requests == 2);
    CHECK(rig.client.state() == LoginState::Failed);
}

TEST_CASE("mock login: a token login timeout permits an explicit fresh password login") {
    auto config = MockConfig::standard();
    config.reply_to_login = false;
    ManualClock clock;
    LoginClient::Options options;
    options.session.clock = clock.source();
    options.login_timeout = 50ms;
    LoginRig rig(std::move(config), options);
    rig.client.start_token("127.0.0.1", rig.server.login.port(), "pending-ticket");
    REQUIRE(rig.until([&] { return rig.server.login_requests == 1; }));
    REQUIRE(rig.client.state() == LoginState::Authenticating);
    clock.hold();
    clock.advance(51ms);
    rig.step();
    CHECK(rig.client.state() == LoginState::Failed);
    CHECK(rig.client.error_id() == login_error::Timeout);
    CHECK(rig.server.login_requests == 1);
    CHECK(rig.server.login_token_mode);
    // The caller explicitly supplies new credentials; nothing is implicitly retried.
    rig.server.config.reply_to_login = true;
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    CHECK(rig.server.login_requests == 2);
    CHECK_FALSE(rig.server.login_token_mode);
    CHECK(rig.server.login_token.empty());
    CHECK(rig.server.login_password == "secret");
}

TEST_CASE("mock login: a token handshake timeout ends the attempt and needs an explicit fresh ticket") {
    auto config = MockConfig::standard();
    config.reply_to_handshake = false;
    ManualClock clock;
    clock.hold();
    LoginClient::Options options;
    options.session.clock = clock.source();
    options.connect_timeout = 50ms;
    LoginRig rig(std::move(config), options);
    rig.client.start_token("127.0.0.1", rig.server.login.port(), "discarded-pending-ticket");
    REQUIRE(rig.until([&] { return rig.server.handshake_requests == 1; }));
    REQUIRE(rig.client.state() == LoginState::Handshake);
    clock.advance(51ms);
    rig.step();
    CHECK(rig.client.state() == LoginState::Failed);
    CHECK(rig.client.error_id() == login_error::Timeout);
    CHECK(rig.server.login_requests == 0);
    rig.step();
    CHECK(rig.server.login_requests == 0); // no ticket replay or password fallback
    rig.server.config.reply_to_handshake = true;
    rig.server.config.login_token = "fresh-ticket";
    rig.client.start_token("127.0.0.1", rig.server.login.port(), "fresh-ticket");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    CHECK(rig.server.login_requests == 1);
    CHECK(rig.server.login_token == "fresh-ticket");
    CHECK(rig.server.login_user.empty());
    CHECK(rig.server.login_password.empty());
}

TEST_CASE("mock login: a password handshake timeout keeps the original pending behaviour") {
    auto config = MockConfig::standard();
    config.reply_to_handshake = false;
    ManualClock clock;
    clock.hold();
    LoginClient::Options options;
    options.session.clock = clock.source();
    options.connect_timeout = 50ms;
    LoginRig rig(std::move(config), options);
    rig.start("tester", "secret");
    REQUIRE(rig.until([&] { return rig.server.handshake_requests == 1; }));
    REQUIRE(rig.client.state() == LoginState::Handshake);
    clock.advance(51ms);
    rig.step();
    CHECK(rig.client.state() == LoginState::Handshake);
    CHECK(rig.server.login_requests == 0);
}

TEST_CASE("mock login: closing or replacing a pending token forgets the earlier credentials") {
    LoginRig rig;
    rig.client.start_token("127.0.0.1", rig.server.login.port(), "discarded-ticket");
    SUBCASE("cancel before the handshake") {
        rig.client.close();
        CHECK(rig.client.state() == LoginState::Idle);
        rig.start("tester", "secret");
        REQUIRE(rig.until_state(LoginState::LoggedIn));
        CHECK_FALSE(rig.server.login_token_mode);
        CHECK(rig.server.login_token.empty());
    }
    SUBCASE("replace with a fresh token before the handshake") {
        rig.server.config.login_token = "fresh-ticket";
        rig.client.start_token("127.0.0.1", rig.server.login.port(), "fresh-ticket");
        REQUIRE(rig.until_state(LoginState::LoggedIn));
        CHECK(rig.server.login_token_mode);
        CHECK(rig.server.login_token == "fresh-ticket");
        CHECK(rig.server.login_password.empty());
    }
    CHECK(rig.server.login_requests == 1);
}

TEST_CASE("mock login: a locked or down world is asked like any other, and the server decides") {
    // ServerSelectWnd::WndNotification (@0x10038310) checks only the locale: EQEmu's world admits a GM to a locked
    // world (Down is a locked one with no zone booted) and answers 326 to anyone else.
    bool admitted = true;
    SUBCASE("the world admits this account") {}
    SUBCASE("the world refuses this account") { admitted = false; }
    for (std::int32_t status : {4, 1, 5}) {
        CAPTURE(status);
        auto config = MockConfig::standard();
        config.server_status = status;
        if (!admitted) config.play_error = login_error::WorldUnavailable;
        LoginRig rig(std::move(config));
        rig.start("tester", "secret");
        REQUIRE(rig.until_state(LoginState::LoggedIn));
        rig.client.request_server_list();
        REQUIRE(rig.until_state(LoginState::ServerList));
        REQUIRE(rig.client.servers().size() == 1);
        const auto& world = rig.client.servers().front();
        CHECK(std::string(world.status_text()) == (status == 1 ? "Down" : "Locked"));
        const auto sequence = rig.client.sequence();
        rig.client.request_play(world.id);
        CHECK(rig.client.sequence() == sequence + 1); // the request went out
        if (admitted) {
            REQUIRE(rig.until_state(LoginState::PlayAccepted));
        } else {
            REQUIRE(rig.until_state(LoginState::Failed));
            CHECK(rig.client.error_id() == login_error::WorldUnavailable);
            CHECK_FALSE(rig.client.ticket());
        }
    }
}

TEST_CASE("mock login: the ticket gets a GameConnection through world authentication to the character list") {
    LoginRig rig;
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    rig.client.request_server_list();
    REQUIRE(rig.until_state(LoginState::ServerList));
    rig.client.request_play(1);
    REQUIRE(rig.until_state(LoginState::PlayAccepted));
    WorldTicket ticket = *rig.client.ticket();
    ticket.world_port = rig.server.world.port();
    rig.client.close();

    EmptyGameDir dir;
    GameDir game(dir.path());
    GameConnection conn(game);
    conn.start(ticket);
    REQUIRE(pump([&] { conn.update(); rig.server.poll(); },
                 [&] { return conn.state() == ConnectionState::CharacterSelect; }));
    // World checked the pair the login server issued (CheckAuth) and sent the list.
    REQUIRE(rig.server.login_infos.size() == 1);
    CHECK(rig.server.login_infos[0].account_id == "77");
    CHECK(rig.server.login_infos[0].session_key == "ABCDEFGHIJ");
    REQUIRE(conn.characters().size() == 1);
    CHECK(conn.characters()[0].name == "Gennro");
    // The character select is built again when this changes (each list, unique in the process).
    CHECK(conn.characters_serial() != 0);
    CHECK(conn.characters_serial() != rig.client.servers_serial());
}

TEST_CASE("mock login: a refused password keeps the server's error id") {
    LoginRig rig;
    rig.start("tester", "wrong");
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == login_error::BadPassword);
    CHECK(rig.server.login_password == "wrong");
}

TEST_CASE("mock login: a refused play request fails with the server's id") {
    MockConfig config = MockConfig::standard();
    config.play_error = 339; // server full
    LoginRig rig(std::move(config));
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    rig.client.request_server_list();
    REQUIRE(rig.until_state(LoginState::ServerList));
    rig.client.request_play(1);
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == 339);
    CHECK_FALSE(rig.client.ticket().has_value());
}

TEST_CASE("mock login: the server dropping the connection after login is error 112") {
    LoginRig rig;
    rig.start("tester", "secret");
    REQUIRE(rig.until_state(LoginState::LoggedIn));
    rig.server.login.send_disconnect();
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == login_error::Terminated);
}

TEST_CASE("mock login: 302 asks whether to remove the logged in character; No keeps the server list") {
    MockConfig config = MockConfig::standard();
    config.play_error = login_error::CharacterOnline;
    LoginRig rig(std::move(config));
    REQUIRE(rig.to_play());
    REQUIRE(rig.until_state(LoginState::KickOffered));
    CHECK(rig.client.play_result().offer_kick());
    CHECK(rig.client.play_result().error_id == 302);
    rig.client.answer_kick(false);
    CHECK(rig.client.state() == LoginState::ServerList);
    CHECK(rig.server.kick_requests == 0);
    // The login is still up: Play again asks again.
    rig.client.request_play(1);
    REQUIRE(rig.until_state(LoginState::KickOffered));
}

TEST_CASE("mock login: Yes sends the kick request (0x16); its successful reply (0x32) plays again") {
    MockConfig config = MockConfig::standard();
    config.play_error = login_error::OfflineTraderOnline; // 114
    config.kick_reply = login_error::NoError;
    LoginRig rig(std::move(config));
    REQUIRE(rig.to_play());
    REQUIRE(rig.until_state(LoginState::KickOffered));
    CHECK(rig.client.sequence() == 5);
    rig.client.answer_kick(true);
    CHECK(rig.client.state() == LoginState::Kicking);
    CHECK(rig.client.sequence() == 6); // the kick request takes the next number
    CHECK(rig.client.notice() == login_error_text(login_error::RemovingPlayer));
    REQUIRE(rig.until_state(LoginState::PlayAccepted));
    CHECK(rig.server.kick_requests == 1);
    CHECK(rig.client.sequence() == 7); // the play request again
    REQUIRE(rig.client.ticket());
    CHECK(rig.client.ticket()->server_id == 1);
}

TEST_CASE("mock login: a refused kick fails with the reply's error") {
    MockConfig config = MockConfig::standard();
    config.play_error = login_error::CharacterOnline;
    config.kick_reply = 102;
    LoginRig rig(std::move(config));
    REQUIRE(rig.to_play());
    REQUIRE(rig.until_state(LoginState::KickOffered));
    rig.client.answer_kick(true);
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == 102);
}

TEST_CASE("mock login: an unanswered kick request (EQEmu's login server) times out with 103") {
    MockConfig config = MockConfig::standard();
    config.play_error = login_error::CharacterOnline;
    LoginClient::Options options;
    options.kick_timeout = 200ms; // the original's 60 s
    LoginRig rig(std::move(config), options);
    REQUIRE(rig.to_play());
    REQUIRE(rig.until_state(LoginState::KickOffered));
    rig.client.answer_kick(true);
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.server.kick_requests == 1);
    CHECK(rig.client.error_id() == login_error::Timeout);
}

TEST_CASE("mock login: EQEmu's 111 (account already online) is a plain refusal, no question") {
    MockConfig config = MockConfig::standard();
    config.play_error = 111;
    LoginRig rig(std::move(config));
    REQUIRE(rig.to_play());
    REQUIRE(rig.until([&] { return rig.client.state() == LoginState::Failed; }));
    CHECK(rig.client.error_id() == 111);
    CHECK(rig.server.kick_requests == 0);
}
