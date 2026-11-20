#pragma once

// A scripted EQEmu (login, world and zone servers and a chat server socket) on loopback, for the offline
// integration tests of LoginClient and GameConnection. Each server is a SoeEndpoint (soe_endpoint.h) plus the
// packet sequence EQEmu sends, in the order docs/protocol/login.md and docs/protocol/rof2-packets.md (sections 1
// and 4) give. Packets are made with the client's own encoders where it has them ("server direction, for tests");
// the rest are built here from the documented layouts (profile_builder.h, the world and zone notes below).
//
// The mock does not decide anything beyond the script: it answers what the client sends and exposes gameplay
// triggers (chat, spawns, HP, items, zone changes) for the test to fire. The test drives it with poll().
//
// Not scripted, because the layout is not documented or the client does not use it (docs/unknowns.md): the
// membership packets, tributes, OP_XTargetResponse, doors and objects, the NMS character sets, character creation,
// and the chat server's own protocol (the UCS socket only records what arrives).

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mock_server/soe_endpoint.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/connection/login_client.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/wire/app_packet.h"
#include "net/protocol/rof2/packets/world_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"

namespace hailstone::test {

// A spawn record the client accepts: a PC or NPC at a position (server coordinates: y first in the packet, the
// client swaps to world x/y). `npc` 0 PC, 1 NPC, 2 NPC corpse, 3 PC corpse.
net::Spawn make_spawn(const std::string& name, std::uint32_t id, std::uint8_t npc, std::uint32_t race, float x, float y,
                      float z, float heading = 0.0f);

// A common item in a possessions slot (net/protocol/rof2/packets/item_packets.h).
net::Item make_item(std::uint32_t item_id, const std::string& name, std::int16_t slot, std::uint32_t stack = 1);

struct MockCharacter {
    std::string name = "Gennro";
    std::uint8_t class_ = 1;
    std::uint32_t race = 1;
    std::uint8_t level = 50;
    std::uint8_t gender = 0;
    std::uint16_t zone_id = 2;
    std::uint32_t hp = 4321, max_hp = 5000, mana = 1234, endurance = 777;
    std::vector<net::Item> inventory; // sent as OP_CharInventory
};

struct MockZone {
    std::uint16_t id = 2;
    std::string short_name = "qeynos2";
    std::string long_name = "North Qeynos";
    float start_x = -285.0f, start_y = -148.0f, start_z = 3.5f, start_heading = 128.0f; // server coordinates
    std::vector<net::Spawn> spawns;          // everyone else; sent during zone-in
    std::vector<net::ZonePoint> zone_points; // OP_SendZonepoints
};

struct MockConfig {
    // login server
    std::string account = "tester", password = "secret";
    std::string login_token; // non-empty: this opaque ticket is also accepted, with empty user/password fields
    std::string returned_login_token; // OP_LoginAccepted's optional next-login token
    bool reply_to_handshake = true;
    bool reply_to_login = true; // false: leave the authentication pending for timeout tests
    std::uint32_t account_id = 77;
    std::string session_key = "ABCDEFGHIJ"; // 10 characters, as EQEmu
    std::uint32_t server_id = 1;
    std::string server_name = "Mock EQEmu";
    std::int32_t play_error = 0; // non-zero: refuse the play request with this eqlsstr id
    // The answer to the kick request (0x16) after a 302 / 114 play error: none (EQEmu's login server has no handler),
    // or the 0x32 reply with this error id; 101 (success) also clears play_error, as the character has left.
    std::optional<std::int32_t> kick_reply;
    // world server
    std::vector<MockCharacter> characters;
    std::string motd = "Welcome to the mock server.";
    bool zone_unavailable = false; // answer OP_EnterWorld with OP_ZoneUnavail
    bool send_chat_server = false; // OP_SetChatServer and OP_SetChatServer2 point at the `ucs` socket
    std::uint32_t log_server_spell_scribe = 0; // OP_LogServer +0x18 (EQEmu sends 0)
    // zone server
    std::map<std::uint16_t, MockZone> zones;
    soe::SessionParams zone_params = SoeEndpoint::default_params();
    std::uint32_t own_spawn_id = 7;
    std::int32_t server_status = 0; // login-list flags: 1 down, 4 locked; admission still uses play_error

    // One character (Gennro, a human warrior in qeynos2 with two NPCs, a guard and a zone line into qeynos) and
    // two zones.
    static MockConfig standard();
};

class MockEqServer {
public:
    explicit MockEqServer(MockConfig config = MockConfig::standard());

    SoeEndpoint login, world, zone, ucs;
    MockConfig config;

    // Receives on every socket and runs the scripts. Call it from the test's loop, next to the client's update().
    void poll();

    // The ticket a LoginClient would produce, for tests that start at the world server.
    net::WorldTicket ticket() const;

    // ---- what the client did ----
    std::string login_user, login_password, login_token; // as decrypted from OP_Login
    bool login_token_mode = false;
    int handshake_requests = 0;
    int login_requests = 0;
    std::vector<net::AppPacket> world_log;  // every application packet the world server received, in order
    std::vector<net::AppPacket> zone_log;
    std::vector<net::AppPacket> ucs_log;
    std::vector<net::LoginInfo> login_infos; // OP_SendLoginInfo of each world session
    std::vector<net::ClientPositionUpdate> client_updates; // decoded OP_ClientUpdate, in order
    int zone_entries = 0;                    // OP_ZoneEntry received
    int camp_requests = 0;                   // OP_Camp received
    int logouts = 0;                         // OP_Logout received
    int kick_requests = 0;                   // the login server's kick request (0x16) received
    int own_delete_spawns = 0;               // OP_DeleteSpawn received (the zone then drops the link)
    bool drop_on_logout = true;              // EQEmu drops the link on OP_Logout
    // The names of the opcodes in a log, for assertions on order.
    static std::vector<net::rof2::Op> ops(const std::vector<net::AppPacket>& log);
    // How many times a log holds the opcode.
    static int count(const std::vector<net::AppPacket>& log, net::rof2::Op op);

    // ---- scripted behaviour a test can change ----
    // Echo each OP_ClientUpdate back as the server's update for this spawn (an NPC that mirrors the player).
    std::optional<std::uint16_t> echo_client_updates_as;
    // Answers to OP_ZoneChange requests, one per request; success 1 when the queue is empty. -1 is "zone not ready".
    std::deque<std::int32_t> zone_change_answers;
    // The zone the next OP_ZoneServerInfo sends the client to (the character's zone at first; a successful
    // OP_ZoneChange sets it to the target).
    std::uint16_t current_zone() const { return current_zone_; }

    // ---- gameplay triggers (zone server) ----
    void chat(const std::string& sender, std::uint32_t channel, const std::string& text, const std::string& target = {});
    void add_spawn(const net::Spawn& spawn);
    void remove_spawn(std::uint32_t id);
    // The server's OP_ClientUpdate for a spawn: unreliable, as EQEmu sends them.
    void move_spawn(std::uint32_t id, const net::Motion& motion);
    void hp_update(std::int16_t spawn_id, std::uint32_t current, std::int32_t max);
    void item_packet(std::uint32_t type, const net::Item& item);
    void request_zone_change(std::uint16_t zone_id, float x, float y, float z);
    // Sends the zone's packets for the character as OP_ZoneEntry's answer; used by the script, public for tests that
    // want to replay a zone-in.
    void send_zone_in(const MockCharacter& character, const MockZone& zone);

    const MockCharacter* character(const std::string& name) const;
    const MockZone& current_zone_def() const;
    MockZone& zone_def(std::uint16_t id) { return config.zones.at(id); }

    // The packet builders, public so tests can assert against what the mock sent.
    static net::AppPacket build_new_zone(const MockZone& zone, const std::string& character);
    static net::AppPacket build_zone_points(const std::vector<net::ZonePoint>& points);

private:
    void on_login_packet(const net::AppPacket& p);
    void on_world_packet(const net::AppPacket& p);
    void on_zone_packet(const net::AppPacket& p);
    void send_login_reply(std::uint16_t opcode, std::vector<std::byte> data);
    void send_world_hello(bool zoning);
    void send_char_list();
    void enter_world(const std::string& name);

    std::uint16_t current_zone_ = 0;
    std::string entering_;      // the character the world is sending into a zone
    std::string zone_char_;     // the character in the zone session
    bool zoning_ = false;
};

} // namespace hailstone::test
