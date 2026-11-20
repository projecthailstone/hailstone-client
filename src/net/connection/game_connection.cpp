#include "net/connection/game_connection.h"
#include "net/messages/task.h"
#include "net/messages/notice.h"
#include "net/messages/pet.h"
#include "net/messages/progression.h"
#include "net/messages/group.h"
#include "net/messages/message.h"


#include <algorithm>
#include <fstream>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <iterator>
#include <random>

#include "core/ascii_case.h"
#include "core/game_dir.h"
#include "core/byte_io.h"
#include "core/ini_file.h"
#include "core/server_profile.h"
#include "core/zone_name.h"
#include "core/log.h"
#include "core/process_serial.h"
#include "game/appearance.h"
#include "game/eye_control.h"
#include "game/boats.h"
#include "game/race_models.h"
#include "game/combat.h"
#include "game/sound_rules.h"
#include "game/coords.h"
#include "game/zone_names.h"
#include "game/mechanics/tables.h"
#include "game/mechanics/vehicle.h"
#include "net/connection/chat_client.h"
#include "plugin/hooks.h"
#include "plugin/packet_bridge.h"
#include "game/attack_check.h"

namespace hailstone::net {

using messages::GameOpcode;

namespace {

using Clock = std::chrono::steady_clock;

// The selected codec reports unknown wire opcodes, but that does not prove
// they are part of the stock wire family. A server extension may have sent
// them. Keep the raw opcode and application-payload length for later mapping;
// never log packet contents (they may include private account/game data).
std::string codec_diagnostic(std::string_view service, std::string_view selected_codec,
                             const messages::CodecError& issue) {
    std::string text = std::format("{} codec [{}]: {}", service, selected_codec, issue.detail);
    if (issue.wire_opcode) {
        text += std::format(" (wire 0x{:04x}", static_cast<unsigned>(*issue.wire_opcode));
        if (issue.payload_size) text += std::format(", payload {} bytes", *issue.payload_size);
        text += ')';
    }
    return text;
}

// The original client sends its position at most every 280 ms while it can
// move (CEverQuest::DoMainLoop @0x53e0c1).
constexpr auto kPositionInterval = std::chrono::milliseconds(280);
// RequestZoneChangeStatus @0x5203c0 waits up to 180 s for the answer to one OP_ZoneChange (@0x52050e).
// RequestZoneChangeStatusBlocking @0x52cee0 sends it again 500 ms after a "not ready" answer (-1) while
// MoveToZone's limit, 7 s from the first request (@0x541806), lasts.
constexpr auto kZoneChangeAnswerWait = std::chrono::seconds(180);
constexpr auto kZoneChangeRetryLimit = std::chrono::milliseconds(7000);
constexpr auto kZoneChangeRetryDelay = std::chrono::milliseconds(500);
// The hand-offs (docs/protocol/session.md): after OP_WorldComplete world has 30 s to
// drop the link (NetMgr_WaitForDisconnect @0x4caca3); after a zone change's OP_DeleteSpawn the zone has 120 s
// (WaitForWorldDisconnect @0x51a4d0); after a camp's OP_Logout 3 s (@0x45ecf3), then OP_DeleteSpawn and the 120 s.
constexpr auto kWorldDropWait = std::chrono::seconds(30);
constexpr auto kWorldConnectWait = std::chrono::seconds(30);
constexpr auto kWorldAuthWait = std::chrono::seconds(60);
constexpr auto kWorldHandoffWait = std::chrono::seconds(60);
constexpr auto kZoneAttachWait = std::chrono::seconds(30);
constexpr auto kZoneInitialWait = std::chrono::seconds(180);
constexpr auto kZoneDropWait = std::chrono::seconds(120);
constexpr auto kLogoutDropWait = std::chrono::seconds(3);

// PlayerZoneClient::ChangePosition @0x5a0bc0 stops a spawn (StopMoving: SpeedRun +0x7c and the turn rate +0x8c 0)
// that sits, loots or lies down.
bool stops_motion(std::uint8_t state) {
    return state == kStandSitting || state == kStandLooting || state == kStandLying;
}

} // namespace

const char* to_string(ConnectionState s) {
    switch (s) {
    case ConnectionState::Idle: return "idle";
    case ConnectionState::WorldConnecting: return "connecting to world";
    case ConnectionState::WorldAuth: return "authenticating with world";
    case ConnectionState::CharacterSelect: return "character select";
    case ConnectionState::Creating: return "creating character";
    case ConnectionState::EnteringWorld: return "entering world";
    case ConnectionState::ZoneConnecting: return "connecting to zone";
    case ConnectionState::ZoneEntering: return "entering zone";
    case ConnectionState::InZone: return "in zone";
    case ConnectionState::Zoning: return "zoning";
    case ConnectionState::Camping: return "camping";
    case ConnectionState::Disconnected: return "disconnected";
    }
    return "?";
}

GameConnection::GameConnection(const GameDir& game, ConnectionOptions options) : game_(game), options_(std::move(options)) {
    if (options_.plugins) ext_chain_.add(options_.plugins);
    load_chat_filters();
    load_client_options();
    reload_game_files();
}

namespace {

// SetSpawnHP @0x4bd0e0: cur * 100.0 / max on the x87 (truncated), so nothing wraps; the
// 64-bit integer quotient is the same for every int32 pair. A spawn still alive (cur >= 1) shows at least 1%; at or
// below 0 it keeps that value (here 0, the percentage being unsigned).
std::uint8_t spawn_hp_percent(std::int64_t cur, std::int64_t max) {
    std::int64_t pct = cur < 1 ? 0 : std::max<std::int64_t>(1, cur * 100 / max);
    return static_cast<std::uint8_t>(std::min<std::int64_t>(pct, 100));
}

} // namespace

void GameConnection::set_own_spawn_hp() {
    const OwnHp hp = own_hp();
    if (hp.max <= 0) return;
    if (auto it = world_.spawns.find(world_.player_id); it != world_.spawns.end())
        { it->second.hp_percent = spawn_hp_percent(hp.cur, hp.max); it->second.hp_known = true; }
}

void GameConnection::set_own_hp_items(std::int64_t cur, std::int64_t max) {
    if (own_hp_items_.cur == cur && own_hp_items_.max == max) return;
    own_hp_items_ = {cur, max};
    if (pstatus_.max_hp > 0) set_own_spawn_hp();
}

void GameConnection::reload_game_files() {
    strings_.load(game_);
    tables_.reset();
    auto table = std::make_shared<game::SpellTable>();
    // No installation has been selected yet in the pre-game client. An empty
    // GameDir is intentional, not a broken installation to warn about.
    if (!table->load(game_) && game_.has_base())
        log::warn("spells_us.txt not found; spell names and rules are unavailable");
    spell_table_ = std::move(table);
    // SetHeights' data: racedata.txt's default heights and moddat.ini's ROffsets (game/spawn_heights.h).
    auto text = [&](std::string_view file) {
        auto bytes = game_.read(file);
        return bytes ? std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()) : std::string();
    };
    std::string racedata = text("racedata.txt");
    if (racedata.empty() && game_.has_base())
        log::warn("racedata.txt not found; spawn heights use the defaults");
    world_.height_tables =
        std::make_shared<const game::HeightTables>(game::parse_height_tables(racedata, text("Resources/moddat.ini")));
    // CDisplay's UseLuclinElementals (game/world_state.h luclin_elementals), read once as the display starts.
    IniFile ini;
    if (auto path = game_.find("eqclient.ini")) ini.load(*path);
    world_.luclin_elementals = game::eqclient_ini_bool(ini.get("Defaults", "UseLuclinElementals"), true);
}

GameConnection::~GameConnection() { disconnect(); }

// Retire identities scoped to the previous zone as soon as the zone server
// accepts the transfer. The world handoff calls this too for the first login.
// No old-zone spawn, request, or donation may own a new-zone reply.
void GameConnection::retire_zone_observations() {
    // Re-clear on world handoff even if the outgoing zone already retired its
    // identities: late old-zone packets must not populate the new zone.
    const bool first_retirement = !zone_observations_retired_;
    zone_observations_retired_ = true;
    invalidate_interaction_context();
    world_.spawns.clear();
    parcel_icon_status_.reset();
    if (first_retirement) ++world_.spawn_set_serial;
    findable_npcs_.clear();
    quest_npcs_.clear();
    quest_npcs_asked_ = false;
    world_.player_id = 0;
    world_.controlled_id = 0;
    world_.eye_id = world_.view_id = 0;
    eye_motion_dirty_ = false;
    last_eye_position_send_ = {};
    world_.target_id = target_of_target_ = 0;
    pending_guild_permissions_.reset();
    reset_tribute_donations();
    reset_tribute_bonuses();
    find_requests_.clear();
    quiet_path_.reset();
    npc_find_identity_.reset();
    shown_find_lease_ = {};
    if (first_retirement) ++shown_find_generation_;
    path_.stop();
    retire_tracking();
    tracking_request_.reset();
    tracking_data_.reset();
    tracking_actors_.clear();
    tracking_request_retired_ = false;
}

void GameConnection::clear_session_pending() {
    zone_initial_ = {};
    parcel_icon_status_.reset();
    zone_ready_ = false;
    client_spawn_requested_ = false;
    zone_change_pending_ = false;
    zone_change_retry_at_ = {};
    zone_observations_retired_ = false;
    world_drop_pending_ = false;
    world_drop_deadline_ = {};
    world_handoff_deadline_ = {};
    zone_attach_deadline_ = {};
    zone_initial_deadline_ = {};
    world_phase_deadline_ = {};
    pending_create_.reset();
    pending_create_name_.clear();
    pending_creation_submitted_ = false;
    pending_delete_name_.clear();
    pending_enter_name_.clear();
    zone_exit_ = ZoneExit::None;
    waiting_for_port_ = false;
    camp_started_ = {};
    respawn_selection_sending_ = false;
    position_tracker_.flush();
    if (zone_endpoint_) zone_endpoint_->invalidate_transient_requests();
    discard_questions();
}

void GameConnection::fail(std::string message) {
    close_evolving_transfer();
    reset_fellowship();
    reset_achievements();
    reset_rewards();
    log::warn("connection: {}", message);
    capture_bank();
    error_ = std::move(message);
    state_ = ConnectionState::Disconnected;
    respawn_.reset();
    clear_hover_rez();
    respawn_spawn_pending_ = false;
    respawn_placement_.reset();
    discard_questions([](const Confirmation& c) { return c.id == kConfirmRezz; });
    reset_tribute_donations();
    reset_tribute_bonuses();
    reset_guild_permissions(kTributeNone);
    world_drop_pending_ = false;
    clear_session_pending();
    if (zone_endpoint_) zone_endpoint_->disconnect();
    if (world_endpoint_) world_endpoint_->disconnect();
}

void GameConnection::chat(ChatEvent::Kind kind, std::string text, std::uint32_t rgb, std::uint32_t color,
                          std::string peer, std::optional<std::string> display_text) {
    if (options_.hooks) { // OnIncomingChat, OnFilterChat (a server's client may drop lines in dsp_chat)
        // A deliberate difference from MQ, whose hook hangs off dsp_chat and sees a hit line as displayed: hooks get
        // the full sentence whatever the hit mode (docs/plugins.md), so a parser matches one form.
        plugin::ChatLine line{text, rgb, static_cast<int>(kind)};
        line.from_code = !plugin::caller_name().empty();
        line.origin_caps = plugin::caller_caps();
        if (options_.hooks->chat(line) == plugin::Flow::Drop) return;
        // A hook's replacement is authoritative; a precomputed abbreviation no longer describes it.
        if (line.text != text) display_text.reset();
        text = std::move(line.text);
        rgb = line.color;
    }
    // Hit modes change presentation only: logs and incoming-chat parsers retain the complete sentence.
    log::info("chat: {}", text);
    if (chat_logging()) write_chat_log(text); // dsp_chat's OutputTextToLog (Log on)
    chat_.push_back({kind, display_text ? std::move(*display_text) : std::move(text), rgb, color, std::move(peer)});
    if (chat_.size() > 500) chat_.pop_front();
}

void GameConnection::start(const WorldTicket& ticket) {
    close_evolving_transfer();
    reset_fellowship();
    reset_achievements();
    reset_rewards();
    reset_tribute_donations();
    reset_tribute_bonuses();
    reset_guild_permissions(kTributeNone);
    parcel_icon_status_.reset();
    ticket_ = ticket;
    max_characters_ = 0;
    pending_create_.reset();
    pending_create_name_.clear();
    pending_creation_submitted_ = false;
    pending_delete_name_.clear();
    pending_enter_name_.clear();
    protocol_diagnostics_.clear();
    // Protocol selection belongs to ConnectionOptions, supplied by the login
    // that opened this world. A saved profile may have changed on disk since
    // login (or may not exist for a transient session), so do not override it.
    camped_out_.reset();
    link_death_.reset();
    zoning_ = false;
    connect_world(false);
}

void GameConnection::connect_world(bool zoning) {
    zoning_ = zoning;
    world_handoff_deadline_ = {};
    world_drop_deadline_ = {};
    world_drop_pending_ = false;
    pending_enter_name_.clear();
    world_login_sent_ = false;
    world_phase_deadline_ = Clock::now() + kWorldConnectWait;
    auto world_created = ProtocolEndpoint::create(options_.world_protocol, codec_registry_);
    if (!world_created) { fail(world_created.error); return; }
    world_endpoint_ = std::move(world_created.endpoint);
    if (options_.hooks) world_endpoint_->set_wire_interceptor(std::make_shared<plugin::PacketBridge>(options_.hooks));
    world_endpoint_->connect(ticket_.world_host, ticket_.world_port);
    state_ = zoning ? ConnectionState::Zoning : ConnectionState::WorldConnecting;
    status_ = "Connecting to " + ticket_.server_name;
}

bool GameConnection::handle_required_codec_error(const messages::CodecError& issue,
                                                messages::ServiceScope scope) {
    if (issue.kind == messages::CodecErrorKind::UnsupportedOpcode || !issue.opcode) return false;
    bool required = false;
    if (scope == messages::ServiceScope::World) {
        required = (state_ == ConnectionState::WorldAuth && *issue.opcode == GameOpcode::SendCharInfo) ||
                   (state_ == ConnectionState::EnteringWorld && *issue.opcode == GameOpcode::ZoneServerInfo);
    } else if (scope == messages::ServiceScope::Zone && state_ == ConnectionState::ZoneEntering) {
        // Both OP_ZoneEntry and OP_NewSpawn can carry the initial local player.
        // Later NPC spawns are optional: their malformed payloads must not
        // tear down an otherwise initialized zone.
        const bool missing_own_spawn = !zone_initial_.own_spawn;
        required = *issue.opcode == GameOpcode::NewZone || *issue.opcode == GameOpcode::PlayerProfile ||
                   *issue.opcode == GameOpcode::WorldObjectsSent ||
                   (missing_own_spawn && (*issue.opcode == GameOpcode::ZoneEntry ||
                                          *issue.opcode == GameOpcode::NewSpawn));
    }
    if (!required) return false;
    fail("required protocol message could not be decoded: " + issue.detail);
    return true;
}

void GameConnection::update() {
    if (world_endpoint_) {
        world_endpoint_->update();
        while (auto issue = world_endpoint_->receive_error()) {
            const std::string text =
                codec_diagnostic("world", world_endpoint_->profile().game_codec, *issue);
            if (protocol_diagnostics_.size() == 128) protocol_diagnostics_.pop_front();
            protocol_diagnostics_.push_back(text);
            log::warn("{}", text);
            if (handle_required_codec_error(*issue, messages::ServiceScope::World)) return;
        }
        if (world_endpoint_->state() == SessionState::Connected && !world_login_sent_ &&
            (state_ == ConnectionState::WorldConnecting || state_ == ConnectionState::Zoning)) {
            LoginInfo info;
            info.account_id = std::to_string(ticket_.account_id);
            info.session_key = ticket_.session_key;
            info.zoning = zoning_;
            auto sent = send_world(messages::outgoing(GameOpcode::SendLoginInfo, std::move(info)));
            if (!sent) {
                fail("world login rejected by selected codec: " + sent.error()->detail);
            } else {
                world_login_sent_ = true;
                world_phase_deadline_ = Clock::now() + kWorldAuthWait;
                if (state_ == ConnectionState::WorldConnecting) state_ = ConnectionState::WorldAuth;
                status_ = "Logging in to " + ticket_.server_name;
            }
            // Zoning keeps state_ == Zoning; the world's OP_EnterWorld reply moves it on.
        }
        // The endpoint has already decoded all RoF2 wire opcodes and payloads.
        while (auto message = world_endpoint_ ? world_endpoint_->receive() : std::nullopt) {
            handle_world(*message);
            if (state_ == ConnectionState::Disconnected) return;
        }
        if (world_endpoint_ && world_phase_deadline_ != Clock::time_point{} &&
            Clock::now() >= world_phase_deadline_ &&
            (state_ == ConnectionState::WorldConnecting || state_ == ConnectionState::WorldAuth ||
             state_ == ConnectionState::Zoning)) {
            fail(state_ == ConnectionState::WorldConnecting ? "world connection timed out" :
                 state_ == ConnectionState::Zoning ? "world zoning authentication timed out" :
                                                      "world authentication or character list timed out");
            return;
        }
        if (world_endpoint_ && state_ == ConnectionState::EnteringWorld &&
            world_handoff_deadline_ != Clock::time_point{} && Clock::now() >= world_handoff_deadline_) {
            fail("world did not provide the requested zone handoff");
            return;
        }
        if (world_endpoint_ && world_endpoint_->state() == SessionState::Disconnected) {
            bool expected = state_ == ConnectionState::ZoneConnecting || state_ == ConnectionState::ZoneEntering ||
                            state_ == ConnectionState::InZone || state_ == ConnectionState::Disconnected;
            if (!expected) {
                fail("lost the connection to the world server" +
                     (world_endpoint_->error().empty() ? std::string() : ": " + world_endpoint_->error()));
                // HandleWorldDisconnect(…, 1): to the front end. Not when it never connected (Net_ConnectToServer's
                // own failure).
                if (world_login_sent_) link_death_ = LinkDeath{};
            }
            world_endpoint_.reset();
        }
    }
    if (world_drop_pending_) {
        if (!world_endpoint_) {
            // "ZONING": Net_ConnectToServer with the address from OP_ZoneServerInfo.
            world_drop_pending_ = false;
            world_drop_deadline_ = {};
            if (state_ != ConnectionState::ZoneConnecting || zone_endpoint_) {
                fail("invalid world-to-zone handoff state");
                return;
            }
            auto zone_created = ProtocolEndpoint::create(options_.zone_protocol, codec_registry_);
            if (!zone_created) { fail("cannot initialize selected zone protocol: " + zone_created.error); return; }
            zone_endpoint_ = std::move(zone_created.endpoint);
            zone_attach_deadline_ = Clock::now() + kZoneAttachWait;
            zone_initial_deadline_ = {};
            zone_initial_ = {};
            // A failed/incomplete transfer from an earlier zone must not block
            // the next attempt. Completed installed files remain session scoped.
            if (server_files_ && !server_files_->finished()) server_files_.reset();
            hailstone_script_ = false; // capability is scoped to a single zone session
            guild_world_invites_ = false;
            auto bridge = std::make_shared<plugin::PacketBridge>(options_.hooks, options_.zone_packet_tap);
            zone_endpoint_->set_wire_interceptor(bridge);
            zone_endpoint_->set_observer(bridge);
            zone_endpoint_->connect(zone_host_, zone_port_);
        } else if (Clock::now() >= world_drop_deadline_) {
            world_drop_pending_ = false;
            world_drop_deadline_ = {};
            fail("World did not drop the connection");
        }
    }

    if (zone_endpoint_) {
        zone_endpoint_->update();
        if (state_ == ConnectionState::ZoneConnecting && zone_attach_deadline_ != Clock::time_point{} &&
            Clock::now() >= zone_attach_deadline_) {
            fail("zone session did not connect before its deadline");
            return;
        }
        if (state_ == ConnectionState::ZoneEntering && zone_initial_deadline_ != Clock::time_point{} &&
            Clock::now() >= zone_initial_deadline_ && (!server_files_ || server_files_->finished())) {
            fail("zone initialization did not complete before its deadline");
            return;
        }
        while (auto issue = zone_endpoint_->receive_error()) {
            const std::string text =
                codec_diagnostic("zone", zone_endpoint_->profile().game_codec, *issue);
            if (protocol_diagnostics_.size() == 128) protocol_diagnostics_.pop_front();
            protocol_diagnostics_.push_back(text);
            log::warn("{}", text);
            if (handle_required_codec_error(*issue, messages::ServiceScope::Zone)) return;
        }
        if (zone_endpoint_->state() == SessionState::Connected && state_ == ConnectionState::ZoneConnecting) {
            ZoneEntryRequest entry;
            entry.name = character_;
            // GetZoneInfoFromNetwork @0x539480: the spell files' checksums (ClientSpellManager +0x2bf40/+0x2bf44).
            entry.spell_crc = spell_table_->file_crc();
            entry.spell_assoc_crc = spell_table_->assoc_file_crc();
            log::info("zone entry: spell file crc {:#010x}, assoc crc {:#010x}", entry.spell_crc, entry.spell_assoc_crc);
            auto sent = send_zone(messages::outgoing(GameOpcode::ZoneEntry, std::move(entry)));
            if (!sent) { fail("zone entry rejected by selected codec: " + sent.error()->detail); return; }
            zone_attach_deadline_ = {};
            zone_initial_deadline_ = Clock::now() + kZoneInitialWait;
            state_ = ConnectionState::ZoneEntering;
            status_ = "Entering the zone";
            // The first zone-in of the login brings the server's client files first (game/server_file_sync.h): the zone
            // knows the client once it has OP_ZoneEntry, and keeps it connecting while the files come.
            if (!options_.server_files_dir.empty() && !server_files_ &&
                zone_endpoint_->has_extension("hailstone")) {
                server_files_ = std::make_unique<ServerFileSync>(
                    options_.server_files_dir, [this](ServerFilesRequest request) {
                        auto admitted = send_zone(messages::outgoing(messages::GameOpcode::HsFilesRequest, request));
                        if (!admitted || *admitted.value() == 0)
                            fail("zone server-file request was rejected by the selected endpoint");
                    },
                    options_.server_files_limits);
                zone_endpoint_->set_no_hold(true); // the pieces asked and acknowledged without the 150 ms hold
                server_files_->start(ServerFileSync::Clock::now());
                if (state_ == ConnectionState::Disconnected) return;
            }
        }
        if (server_files_ && !server_files_->finished()) {
            server_files_->update(ServerFileSync::Clock::now());
            if (state_ == ConnectionState::Disconnected) return;
            if (server_files_->finished()) finish_server_files();
        }
        while (auto message = zone_endpoint_ ? zone_endpoint_->receive() : std::nullopt) {
            handle_zone(*message);
            if (state_ == ConnectionState::Disconnected) return;
        }
        // CTaskWnd::OnProcessFrame: the task compass mark looked for again once marked (0x76ff70).
        if (task_compass_dirty_ && state_ == ConnectionState::InZone) update_task_compass();
        if (zone_endpoint_ && zone_exit_ != ZoneExit::None && zone_endpoint_->state() != SessionState::Disconnected &&
            Clock::now() >= zone_exit_deadline_) {
            if (zone_exit_ == ZoneExit::CampLogout) {
                // The zone kept the link 3 s after OP_Logout: DoMainLoop's exit (exit reason 0x1e6) records the
                // position, saves (SavePCForce(0): OP_SaveOnZoneReq), sends OP_DeleteSpawn and waits again.
                // Neither goes out when the link is down (the connection's status +0x8c, 0x542380) or while the
                // respawn window is up (PlayerZoneClient RespawnTimer).
                if (!respawn_) {
                    const auto save = send_zone(messages::outgoing(GameOpcode::SaveOnZoneReq, save_request(0)));
                    const auto remove = send_zone(messages::outgoing(GameOpcode::DeleteSpawn, SpawnDespawn{world_.player_id}));
                    if (!save || *save.value() == 0 || !remove || *remove.value() == 0) {
                        fail("camp logout cleanup could not be admitted");
                        return;
                    }
                }
                zone_exit_ = ZoneExit::CampDeleteSpawn;
                zone_exit_deadline_ = Clock::now() + kZoneDropWait;
            } else {
                log::info("the zone did not drop the link; closing it");
                zone_endpoint_->disconnect();
                zone_endpoint_.reset();
                leave_zone_dropped();
            }
        }
        if (zone_endpoint_ && zone_endpoint_->state() == SessionState::Disconnected) {
            std::string why = zone_endpoint_->error();
            zone_endpoint_.reset();
            if (zone_exit_ != ZoneExit::None) {
                leave_zone_dropped();
            } else if (state_ != ConnectionState::Zoning && state_ != ConnectionState::Disconnected) {
                // A link death. In game (also during the camp's 30 s, before OP_Logout) DoMainLoop's
                // HandleWorldDisconnect(…, 0) sets 0xddfd5b = 1 after its pause, and DoMainLoop leaves the camp's way
                // with the camp kind as it stands; during the zone-in, HandleWorldDisconnect(…, 1) goes to the front
                // end.
                bool in_game = state_ == ConnectionState::InZone || state_ == ConnectionState::Camping;
                bool connected = state_ != ConnectionState::ZoneConnecting;
                fail("lost the connection to the zone server" + (why.empty() ? std::string() : ": " + why));
                bool offline = in_game && offline_mode_at_ != 0 && now_ms() - offline_mode_at_ < 15000; // @0x53ddc3: 0x3a98 ms
                if (connected) link_death_ = LinkDeath{in_game, in_game ? camp_kind_ : 0, offline};
            }
        }
    }

    rewards_.update(reward_now_ms());
    update_evolving_transfer();
    update_achievements();
    camp_tick();
    auto_target_tick(); // before attack_hold_tick: a new target ends the attack hold with auto attack still on
    attack_hold_tick();
    apply_poison_tick();
    guild_invite_tick();
    fellowship_invite_tick();
    guild_bank_tick();
    update_shroud_bank();
    real_estate_tick();
    adventure_frame();
    if (alarm_due_ms_ && now_ms() >= alarm_due_ms_) {
        alarm_due_ms_ = 0;
        ui_message("Your alarm has gone off."); // this client's line (the original's is not decoded)
    }
    path_tick();
    save_tick();
    if (state_ == ConnectionState::Camping && camp_started_ != Clock::time_point{} &&
        (fast_camp_ || Clock::now() - camp_started_ >= std::chrono::seconds(30))) {
        // DoPassageOfTime ("I have completed camping"): OP_Logout, then up to 3 s for the zone to drop the link.
        // EQEmu's Handle_OP_Logout drops it at once (its OP_LogoutReply has no RoF2 opcode and is not sent).
        // fast_camp: at the first update after OP_Camp (not the original's; set_fast_camp).
        // The local player is the controlled one (not riding): Send_OP_FloatListThing(1) first.
        if (world_.controlled_id == 0 || world_.controlled_id == world_.player_id) send_position_history(true);
        const auto logout = send_zone(messages::outgoing(GameOpcode::Logout));
        if (!logout || *logout.value() == 0) {
            cancel_camp(12290);
        } else {
            camp_started_ = {};
            zone_exit_ = ZoneExit::CampLogout;
            zone_exit_deadline_ = Clock::now() + kLogoutDropWait;
        }
    }

    if (zone_change_pending_ && zone_change_retry_at_ != Clock::time_point{} && Clock::now() >= zone_change_retry_at_) {
        zone_change_retry_at_ = {};
        log::info("zone change: the zone was not ready, asking again");
        const auto admitted = send_zone(messages::outgoing(GameOpcode::ZoneChange, zone_change_request_));
        if (!admitted || *admitted.value() == 0) {
            zone_change_pending_ = false;
            ui_message("The zone change retry could not be sent.");
        } else {
            zone_change_sent_ = Clock::now();
        }
    }
    if (zone_change_pending_ && Clock::now() - zone_change_sent_ > kZoneChangeAnswerWait) {
        zone_change_pending_ = false;
        chat(ChatEvent::Kind::System, "The zone change request timed out.");
    }

    update_chat_server();
    if (state_ == ConnectionState::InZone) {
        maintain_tracking();
        // CTrackingWnd::OnProcessFrame: OP_Track every 6 s with auto update on.
        auto f = game::tracking_frame(tracking_.open, static_cast<std::uint32_t>(now_ms()), tracking_.next_controls,
                                      tracking_.next_track, tracking_.auto_update);
        if (f.send_track) send_track();
        send_position(false);
        update_afk();
        update_spells();
        update_npc_interactions();
        update_loot();
        update_merc();
        update_inspect();
        update_doors();
        process_cursor_queue();
        update_item_job();
        update_tradeskill_batch();
        // The server extension may run the zone-in's last step again (NMS: its client check was not echoed).
        if (server_ext().retry_zone_entry(*this)) finish_zone_in();
    }
    // The rules last while the character is in the world. From the world login to the zone-in (character select
    // too) the last login's rules hold, provisional: only what has the 'login' permission runs. Disconnected,
    // nothing runs.
    bool in_world = state_ == ConnectionState::InZone || state_ == ConnectionState::Zoning ||
                    state_ == ConnectionState::ZoneConnecting || state_ == ConnectionState::ZoneEntering ||
                    state_ == ConnectionState::Camping;
    bool logging_in = state_ == ConnectionState::WorldConnecting || state_ == ConnectionState::WorldAuth ||
                      state_ == ConnectionState::CharacterSelect || state_ == ConnectionState::EnteringWorld ||
                      state_ == ConnectionState::Creating;
    if (logging_in && (!server_rules_ || !server_rules_->provisional)) {
        server_rules_ = cached_server_rules(options_.server_files_dir);
        ++server_rules_serial_;
    } else if (!in_world && !logging_in && server_rules_) {
        server_rules_.reset();
        ++server_rules_serial_;
    }
    // The original reapplies the new-armor filter to a member who joins or leaves the group or raid
    // (PlayerClient_ApplyNewArmorFilterGroupMember / RaidMember); a new spawn has it made in apply_spawn.
    if (new_armor_membership_changed())
        for (auto& entry : world_.spawns) refresh_new_armor_bits(entry.second);
    raise_hook_events();
    marketplace_frame(now_ms());
    // Network-update scope: subsequent cure actions must not see this frame's faded buffs.
    faded_counter_spas_ = 0;
}

void GameConnection::refresh_new_armor_bits(game::Spawn& s) const {
    // Type (+0x125) as the spawn record set it; a spawn that died here keeps the bits it had (the death does not
    // reapply the filter) until the next membership change.
    const std::uint8_t type = s.is_corpse ? 2 : s.is_npc ? 1 : 0;
    s.appearance.new_armor_bits = game::new_armor_bits(type, s.id == world_.player_id, group_.has_member(s.name),
                                                       raid_.is_member(s.name));
}

bool GameConnection::new_armor_membership_changed() {
    bool same = new_armor_player_ == world_.player_id;
    std::size_t n = 0;
    auto visit = [&](const std::string& name) {
        if (same && (n >= new_armor_members_.size() || new_armor_members_[n] != name)) same = false;
        ++n;
    };
    for (const auto& m : group_.slots) visit(m.name);
    for (const auto& m : raid_.members) visit(m.name);
    if (same && n == new_armor_members_.size()) return false;
    new_armor_player_ = world_.player_id;
    new_armor_members_.clear();
    for (const auto& m : group_.slots) new_armor_members_.push_back(m.name);
    for (const auto& m : raid_.members) new_armor_members_.push_back(m.name);
    return true;
}

std::optional<Item> GameConnection::inspect_item_wire(std::span<const std::byte> bytes) const {
    if (zone_endpoint_) return zone_endpoint_->inspect_item_wire(bytes);
    // Offline/native-plugin diagnostics use the configured codec, too. Test
    // worlds and replay clients need not open a network session to inspect
    // a received packet, and no RoF2 parser is hard-coded at this boundary.
    auto codec = codec_registry_.create(options_.zone_protocol.game_codec);
    return codec ? codec->inspect_item_wire(bytes) : std::nullopt;
}

protocol::WireDiagnosticPort* GameConnection::diagnostic_port(messages::ServiceScope scope) {
    switch (scope) {
    case messages::ServiceScope::World: return world_endpoint_.get();
    case messages::ServiceScope::Zone: return zone_endpoint_.get();
    default: return nullptr;
    }
}

messages::CodecResult<std::size_t> GameConnection::send_world(messages::OutgoingGameMessage message) {
    auto endpoint = world_endpoint_;
    if (!endpoint || endpoint->state() != SessionState::Connected)
        return messages::CodecResult<std::size_t>::failure(
            {messages::CodecErrorKind::DeliveryRejected, "world endpoint is not connected"});
    return endpoint->send(message);
}

messages::CodecResult<std::size_t> GameConnection::send_zone(messages::OutgoingGameMessage message,
        const std::function<bool()>& still_current) {
    auto endpoint = zone_endpoint_;
    if (!endpoint || endpoint->state() != SessionState::Connected)
        return messages::CodecResult<std::size_t>::failure(
            {messages::CodecErrorKind::InvalidValue, "zone endpoint is not connected"});
    if (still_current && !still_current())
        return messages::CodecResult<std::size_t>::failure(
            {messages::CodecErrorKind::DeliveryRejected, "outbound action is no longer current"});
    return endpoint->send(message, still_current);
}

bool GameConnection::send_zone_if_current(messages::OutgoingGameMessage message,
                                          const std::function<bool()>& still_current) {
    if (!still_current()) return false;
    const auto admission = send_zone(std::move(message), still_current);
    return admission && *admission.value() > 0 && still_current();
}

bool GameConnection::submit_zone_action(messages::OutgoingGameMessage message, std::string_view action,
                                        const std::function<bool()>& still_current) {
    auto result = send_zone(std::move(message), still_current);
    if (result && *result.value() != 0) return true;
    const std::string reason = !result ? result.error()->detail : "no packet admitted";
    const std::string detail = std::string(action) + " was not sent: " + reason;
    log::warn("{}", detail);
    error_ = detail;
    ui_message(detail);
    return false;
}

messages::CodecResult<std::size_t> GameConnection::send_world_message(messages::OutgoingGameMessage message) {
    return send_world(std::move(message));
}

messages::CodecResult<std::size_t> GameConnection::send_zone_message(messages::OutgoingGameMessage message) {
    return send_zone(std::move(message));
}

void GameConnection::raise_hook_events() {
    plugin::Hooks* hooks = options_.hooks;
    if (!hooks) return;
    if (state_ != hook_state_) {
        ConnectionState was = std::exchange(hook_state_, state_);
        if (state_ == ConnectionState::Zoning) hooks->zone(plugin::ZonePhase::Begin);
        if (state_ == ConnectionState::InZone && (was == ConnectionState::ZoneEntering || was == ConnectionState::Zoning)) {
            hooks->zone(plugin::ZonePhase::End);
            hooks->zone(plugin::ZonePhase::Zoned);
        }
        int game_state = 0;
        switch (state_) {
        case ConnectionState::WorldConnecting:
        case ConnectionState::WorldAuth: game_state = plugin::kGameStatePreCharSelect; break;
        case ConnectionState::CharacterSelect:
        case ConnectionState::EnteringWorld: game_state = plugin::kGameStateCharSelect; break;
        case ConnectionState::Creating: game_state = plugin::kGameStateCharCreate; break;
        case ConnectionState::InZone:
        case ConnectionState::Camping: game_state = plugin::kGameStateInGame; break;
        case ConnectionState::ZoneConnecting:
        case ConnectionState::ZoneEntering:
        case ConnectionState::Zoning: game_state = hook_game_state_ == plugin::kGameStateInGame ? plugin::kGameStateInGame
                                                                                           : plugin::kGameStateLoggingIn;
            break;
        case ConnectionState::Idle:
        case ConnectionState::Disconnected: game_state = plugin::kGameStatePreCharSelect; break;
        }
        if (game_state != hook_game_state_) {
            hook_game_state_ = game_state;
            hooks->game_state(game_state);
        }
    }
    // OnAddSpawn / OnRemoveSpawn: the spawn list's changes since the last update.
    if (!hooks->has_spawn_hooks()) {
        hook_spawns_.clear();
        return;
    }
    // Unchanged (the usual frame): as many spawns as last time and every one raised still there. That costs a lookup a
    // spawn and no allocation; only a change builds the sorted list again.
    if (hook_spawns_.size() == world_.spawns.size() &&
        std::all_of(hook_spawns_.begin(), hook_spawns_.end(), [&](std::uint32_t id) { return world_.spawns.contains(id); }))
        return;
    std::vector<std::uint32_t>& now = hook_spawns_scratch_; // the last list's buffer, kept to be filled again
    now.clear();
    now.reserve(world_.spawns.size());
    for (const auto& [id, s] : world_.spawns) now.push_back(id);
    std::sort(now.begin(), now.end());
    std::vector<std::uint32_t> gone, added;
    std::set_difference(hook_spawns_.begin(), hook_spawns_.end(), now.begin(), now.end(), std::back_inserter(gone));
    std::set_difference(now.begin(), now.end(), hook_spawns_.begin(), hook_spawns_.end(), std::back_inserter(added));
    hook_spawns_.swap(now);
    for (auto id : gone) hooks->spawn(id, false);
    for (auto id : added) hooks->spawn(id, true);
}

// ---------------------------------------------------------------- world

bool GameConnection::send_checksums() {
    std::mt19937 rng(std::random_device{}());
    struct File { const char* path; ChecksumKind kind; };
    for (const File& f : {File{"eqgame.exe", ChecksumKind::Exe}, File{"Resources/SkillCaps.txt", ChecksumKind::SkillCaps},
                          File{"Resources/BaseData.txt", ChecksumKind::BaseData}}) {
        auto data = game_.read(f.path);
        ClientFileProofSource proof;
        proof.kind = f.kind;
        if (data) proof.contents = std::move(*data);
        proof.seed = static_cast<std::uint32_t>(rng());
        const GameOpcode opcode = f.kind == ChecksumKind::Exe ? GameOpcode::World_Client_CRC1
            : f.kind == ChecksumKind::SkillCaps ? GameOpcode::World_Client_CRC2 : GameOpcode::World_Client_CRC3;
        auto admitted = send_world(messages::outgoing(opcode, std::move(proof)));
        if (!admitted || *admitted.value() == 0) return false;
    }
    return true;
}

void GameConnection::handle_world(const messages::GameMessage& message) {
    switch (message.opcode) {
    case GameOpcode::ApproveWorld:
    case GameOpcode::PostEnterWorld:
        // These are protocol handshake notifications with no client-used fields.
        // Decoding them still validates their exact RoF2 payload layouts.
        break;
    case GameOpcode::SendMaxCharacters:
        if (const auto* limit = messages::message_get<std::uint32_t>(message)) max_characters_ = *limit;
        break;
    case GameOpcode::ExpansionInfo:
        // The account's expansions (0xde09d4): the character creation's combinations need them.
        if (const auto* e = messages::message_get<std::uint32_t>(message)) world_expansions_ = *e;
        break;
    case GameOpcode::RandomNameGenerator:
        // Handle_OP_RandomNameGenerator @0x64ae20: the name for the creation window's name box.
        if (auto r = messages::message_get<RandomName>(message)) random_name_ = r->name;
        break;
    case GameOpcode::LogServer:
        // WorldAuthenticationHandler::OnRoutePacket: the rule set dword at +4 goes to 0xdcee70 (and 0xddfd2c), the
        // byte at +8 to 0xdcee74. Its readers: con levels, AllowedToAttack, casting, HitBySpell, resists, steal, bind
        // wound, invites, the death coin rule and the PvP windows.
        if (const auto* l = messages::message_get<LogServer>(message)) {
            pvp_rules_ = l->pvp;
            spell_scribe_allowed_ = l->spell_scribe;
            log::info("world: PvP rule set {}", pvp_rules_);
        }
        break;
    case GameOpcode::SendCharInfo: {
        // The world provides a refreshed list, not a request identifier. Accept
        // it only during character selection or after a submitted edit; a late
        // refresh must never undo an enter-world/zone transition.
        if (state_ != ConnectionState::WorldAuth && state_ != ConnectionState::CharacterSelect &&
            !(state_ == ConnectionState::Creating && pending_creation_submitted_)) break;
        const auto* list = messages::message_get<std::vector<CharacterPreview>>(message);
        if (!list) { fail("world codec returned an invalid character list"); return; }
        characters_ = *list;
        characters_serial_ = core::next_process_serial();
        world_phase_deadline_ = {};
        bool first = state_ == ConnectionState::WorldAuth;
        state_ = ConnectionState::CharacterSelect;
        status_.clear();
        if (first) {
            if (!send_checksums()) {
                fail("world did not accept character-selection file checksums");
                return;
            }
            // DoCharacterSelection @0x53ab59, once the character select screen is up. EQEmu's world eats it.
            auto ready = send_world(messages::outgoing(GameOpcode::WorldClientReady));
            auto creation = send_world(messages::outgoing(GameOpcode::CharacterCreateRequest));
            if (!ready || *ready.value() == 0 || !creation || *creation.value() == 0) {
                fail("world did not accept character-selection initialization");
                return;
            }
        }
        pending_create_.reset();
        pending_create_name_.clear();
        pending_creation_submitted_ = false;
        pending_delete_name_.clear();
        break;
    }
    case GameOpcode::CharacterCreateRequest:
        if (const auto* data = messages::message_get<CharCreateData>(message)) create_data_ = *data;
        else create_data_.reset();
        break;
    case GameOpcode::SendMembership:
        // FreeToPlayClient::Instance() (GameFeatureRestrictions_ReadEntry); the original then refreshes the character
        // list's membership marks, the creation buttons and the augment slots.
        if (const auto* m = messages::message_get<Membership>(message)) {
            log::info("world: membership level {}", m->level);
            membership_ = *m;
        }
        break;
    case GameOpcode::SendMembershipDetails:
        if (const auto* d = messages::message_get<MembershipDetails>(message)) membership_details_ = *d;
        break;
    case GameOpcode::ApproveName: {
        const auto* ok = messages::message_get<std::uint8_t>(message);
        if (state_ != ConnectionState::Creating || !pending_create_ || pending_create_name_.empty()) break;
        if (pending_creation_submitted_) {
            // A duplicate success is not a second character-create request.
            if (ok && *ok == 1) break;
        }
        if (!pending_creation_submitted_ && ok && *ok == 1) {
            auto sent = send_world(messages::outgoing(GameOpcode::CharacterCreate, *pending_create_));
            if (!sent || *sent.value() == 0) {
                error_ = "The character creation request was not accepted locally.";
                pending_create_.reset();
                pending_create_name_.clear();
                pending_creation_submitted_ = false;
                state_ = ConnectionState::CharacterSelect;
                break;
            }
            pending_creation_submitted_ = true; // wait for the world's refreshed character list
        } else {
            pending_create_.reset();
            pending_create_name_.clear();
            pending_creation_submitted_ = false;
            error_ = "The server refused that name or character.";
            // Handle_OP_ApproveName @0x64d070: its refusal as an OK dialog (45 s): 2 1591, 3 5502, 4 6862, 5 6863,
            // -1 1576 (not valid), anything else 1581 (rejected).
            if (ok) {
                std::int32_t r = static_cast<std::int8_t>(*ok);
                std::uint32_t id = r == 2 ? 1591 : r == 3 ? 5502 : r == 4 ? 6862 : r == 5 ? 6863 : r == -1 ? 1576 : 1581;
                Confirmation c;
                c.id = kConfirmNameResult;
                c.text = strings_.format(id, std::vector<std::string>{});
                c.timeout_ms = 45000;
                c.ok_only = true;
                ask(std::move(c));
            }
            state_ = ConnectionState::CharacterSelect;
        }
        break;
    }
    case GameOpcode::EnterWorld:
        // While zoning, world names the character to re-enter with.
        if (state_ == ConnectionState::Zoning) {
            const auto* name = messages::message_get<std::string>(message);
            // Zoning must re-enter as the same player. A late or unrelated
            // world's EnterWorld reply cannot substitute another character.
            if (name && !name->empty() && !iequals(*name, character_)) {
                log::warn("ignoring unrelated zoning enter-world reply for {}", *name);
                break;
            }
            EnterWorld e;
            e.name = character_;
            auto sent = send_world(messages::outgoing(GameOpcode::EnterWorld, std::move(e)));
            if (!sent || *sent.value() == 0) {
                fail("world rejected enter-world handoff");
                return;
            }
            world_phase_deadline_ = {};
            pending_enter_name_ = character_;
            world_handoff_deadline_ = Clock::now() + kWorldHandoffWait;
            state_ = ConnectionState::EnteringWorld;
            status_ = "Entering the world";
        }
        break;
    case GameOpcode::ZoneServerInfo: {
        if (state_ != ConnectionState::EnteringWorld || world_drop_pending_ || zone_endpoint_ ||
            pending_enter_name_.empty() || !iequals(pending_enter_name_, character_)) {
            log::warn("ignoring unexpected or duplicate world zone handoff");
            break;
        }
        const auto* info = messages::message_get<ZoneServerInfo>(message);
        if (!info || info->port == 0) { fail("world codec returned an invalid zone endpoint"); return; }
        const std::string next_zone_host = info->host.empty() ? ticket_.world_host : info->host;
        if (next_zone_host.empty()) { fail("world provided an empty zone host"); return; }
        // Commit the destination only after OP_WorldComplete is admitted.
        const auto next_zone_port = info->port;
        log::info("zone server {}:{}", next_zone_host, next_zone_port);
        // OP_WorldComplete, then the zone is connected once world has dropped the link (case @0x4cac19; EQEmu
        // closes it on OP_WorldComplete). The new endpoint must not exist yet.
        auto completed = send_world(messages::outgoing(GameOpcode::WorldComplete));
        if (!completed || *completed.value() == 0) {
            fail("world did not accept handoff completion");
            return;
        }
        zone_host_ = next_zone_host;
        zone_port_ = next_zone_port;
        pending_enter_name_.clear();
        world_handoff_deadline_ = {};
        world_drop_pending_ = true;
        world_drop_deadline_ = Clock::now() + kWorldDropWait;
        retire_zone_observations();
        own_spawn_seen_ = weather_seen_ = client_spawn_requested_ = zone_ready_ = false;
        instrument_published_ = false;
        zone_initial_ = {};
        // The new local EQPlayer (its constructor @0x596ac0): Send_OP_FloatListThing(1) for it, then the samples
        // are forgotten. MoveToZone sent the last zone's already; here they are only forgotten.
        position_tracker_.flush();
        save_ticks_ = save_tick_ms_ = 0; // SavePCForce on the way out (MoveToZone)
        motion_known_ = false;
        new_zone_.reset();
        spells_.zone_suspends_buffs = false;
        profile_.reset();
        drunkenness_ = 0;
        invis_state_ = 0;
        zone_points_.clear();
        zone_change_pending_ = false;
        auto_attack_ = sent_auto_attack_ = false;
        auto_fire_ = sent_auto_fire_ = false;
        clear_bard_twist(); // the new zone's PlayerClient
        attack_locked_ = false; // CEverQuest_ResetClientStateForZone
        safelock_.clear();      // CDisplay::StartWorldDisplay: each zone-in unlocks the interface (/safelock)
        // The last zone's auras, aggro meter and stun: the new zone's server sends its own (EQEmu keeps none of them
        // across a zone; a stun is a timer on the old zone's Client). The meter window keeps its shown state.
        auras_.clear();
        aggro_.lock_id = aggro_.target_id = aggro_.secondary_id = 0;
        aggro_.pct = {};
        aggro_.received = {};
        stun_timer_ms_ = 0;
        stun_state_ = false;
        // The helm goes with the zone: the new zone may reuse the boat's id for another vehicle, which must smooth.
        controlled_boat_ = standing_on_ = world_.driven_id = 0;
        spell_book_open_ = false; // MoveToZone closes the book
        respawn_.reset();
        clear_hover_rez();
        respawn_spawn_pending_ = false;
        respawn_placement_.reset();
        pstatus_.dead = false;
        invalidate_interaction_context();
        pstatus_.owner_status = 0;
        pstatus_.stand_state = 100;
        spells_.casting = {};
        spells_.pending = 0;
        item_preview_ = {}; // CObjectPreviewWnd::OnPreZone: hidden, its preview actor gone
        poison_started_ms_ = 0; // CEverQuest_ResetClientStateForZone
        poison_slot_ = 0;
        poison_interacting_ = false;
        spells_.memorizing.reset();
        spells_.others.clear();
        cast_stage_shown_.clear();
        landing_animation_spell_ = 0;
        landing_animation_until_ = 0;
        faded_counter_spas_ = 0;
        effect_packets_.clear();
        blood_spurts_.clear();
        spells_.target_buffs.clear();
        spells_.target_buffs_entity = 0;
        loot_.reset();
        loot_popup_pending_ = false; // the corpse's question goes with reset_player_for_zone below
        doors_.clear();
        ground_objects_.clear();
        world_container_.reset();
        world_container_requested_.reset();
        close_tradeskill();
        switch_clock_ = {};
        reset_group_for_zone();
        reset_social_for_zone();
        reset_fellowship(true);
        reset_guild_banner();
        reset_commerce_for_zone();
        // The guild's confirmed rank permissions and names stay: the membership has not changed, and EQEmu's
        // zone-in sends every rank's permissions and the names again (SendGuildRanks / SendGuildRankNames). A
        // staged table for a guild the GuildID appearance never confirmed is dropped; the next zone resends it.
        reset_progress_for_zone();
        reset_achievements();
        reset_rewards();
        reset_player_for_zone();
        reset_market_for_zone();
        expedition_.compass.clear();
        // NPC windows close with the zone (a guild bank deposit outside the guild hall would lose the item).
        reset_real_estate_for_zone();
        guild_bank_.banker = 0;
        guild_bank_.pending = 0;
        guild_bank_.selected = -1;
        reset_adventure_for_zone();
        guild_tribute_.master_id = 0;
        state_ = ConnectionState::ZoneConnecting;
        status_ = "Connecting to the zone server";
        break;
    }
    case GameOpcode::ZoneUnavail: {
        if (state_ != ConnectionState::EnteringWorld && state_ != ConnectionState::Zoning) break;
        pending_enter_name_.clear();
        world_handoff_deadline_ = {};
        const auto* zone = messages::message_get<std::string>(message);
        error_ = "That zone is not available" + (zone ? " (" + *zone + ")" : std::string()) + ".";
        state_ = ConnectionState::CharacterSelect;
        break;
    }
    case GameOpcode::MOTD:
        if (const auto* m = messages::message_get<std::string>(message); m && !m->empty()) chat(ChatEvent::Kind::System, *m);
        break;
    case GameOpcode::GuildsList:
    case GameOpcode::SetChatServer:
    case GameOpcode::SetChatServer2:
        handle_world_social(message);
        break;
    default:
        log::debug("world: unhandled semantic message {}", static_cast<unsigned>(message.opcode));
        break;
    }
}

std::uint32_t GameConnection::classes_mask(const std::string& name) const {
    auto it = character_classes_.find(name);
    return it == character_classes_.end() ? 0 : it->second;
}

void GameConnection::set_character_classes(const std::string& name, std::uint32_t mask) {
    if (classes_mask(name) == mask) return;
    if (mask) character_classes_[name] = mask;
    else character_classes_.erase(name);
    characters_serial_ = core::next_process_serial();
}

std::string GameConnection::guild_member_class(std::uint32_t class_value) const {
    if (std::uint32_t mask = server_ext().guild_member_classes(*this, class_value)) return game::class_mask_name(mask);
    return class_value ? game::class_short_name(class_value) : std::string();
}


void GameConnection::create_character(const std::string& name, const CharCreate& request) {
    if (state_ != ConnectionState::CharacterSelect) return;
    NameApproval a;
    a.name = name;
    a.race = request.race;
    a.class_ = request.class_;
    a.deity = request.deity;
    auto admitted = send_world(messages::outgoing(GameOpcode::ApproveName, std::move(a)));
    if (!admitted || *admitted.value() == 0) {
        error_ = "Unable to submit character name approval.";
        return;
    }
    log::info("character creation: {} race {} class {} gender {}, face {} hair {} / {} beard {} / {} eyes {} / {} "
              "heritage {} tattoo {} details {}", name, request.race, request.class_, request.gender, request.face,
              request.hair_style, request.hair_color, request.beard, request.beard_color, request.eye_color1,
              request.eye_color2, request.drakkin_heritage, request.drakkin_tattoo, request.drakkin_details);
    pending_create_ = request;
    pending_create_name_ = name;
    pending_creation_submitted_ = false;
    error_.clear();
    state_ = ConnectionState::Creating;
}

void GameConnection::request_random_name(std::uint32_t race, std::uint32_t gender) {
    // CCharacterCreation::WndNotification's CC_Get_Name_Button: {race, gender, "Bob"} (72 bytes).
    if (state_ != ConnectionState::CharacterSelect) return;
    RandomName r;
    r.race = race;
    r.gender = gender;
    r.name = "Bob";
    auto admitted = send_world(messages::outgoing(GameOpcode::RandomNameGenerator, std::move(r)));
    if (!admitted || *admitted.value() == 0) error_ = "Unable to request a random name.";
}

void GameConnection::delete_character(const std::string& name) {
    if (state_ != ConnectionState::CharacterSelect || !pending_delete_name_.empty()) return;
    DeleteCharacter d;
    d.name = name;
    auto admitted = send_world(messages::outgoing(GameOpcode::DeleteCharacter, std::move(d)));
    if (!admitted || *admitted.value() == 0) {
        error_ = "Unable to submit character deletion.";
        return;
    }
    pending_delete_name_ = name; // consumed by the next character-list refresh
}

CharacterFiles GameConnection::own_files() const {
    std::string profile = options_.profile_id.empty() ? profile_id_for_world(ticket_.server_name) : options_.profile_id;
    return options_.servers_dir.empty() ? character_files(profile, ticket_.server_name, character_)
                                        : character_files(profile, ticket_.server_name, character_, options_.servers_dir);
}

std::optional<ServerProfile> GameConnection::saved_server() const {
    std::string profile = options_.profile_id.empty() ? profile_id_for_world(ticket_.server_name) : options_.profile_id;
    if (!saved_server_ || saved_server_->first != profile) {
        auto loaded = options_.servers_dir.empty() ? load_profile(profile) : load_profile(profile, options_.servers_dir);
        saved_server_.emplace(profile, std::move(loaded));
    }
    return saved_server_->second;
}

void GameConnection::enter_world(const std::string& name, bool return_home) {
    if (state_ != ConnectionState::CharacterSelect || !pending_delete_name_.empty()) return;
    EnterWorld e;
    e.name = name;
    e.return_home = return_home; // RoF2 translates this into its own flag layout.
    auto admitted = send_world(messages::outgoing(GameOpcode::EnterWorld, std::move(e)));
    if (!admitted || *admitted.value() == 0) {
        error_ = "Unable to submit enter-world request.";
        return;
    }
    adventure_ = {};
    reset_shroud_selection();
    monster_mission_catalog_.reset();
    shroud_mode_ = ShroudMode::Normal;
    typed_shroud_mode_ = false;
    shroud_restoring_ = false;
    shroud_progress_available_ = false;
    shroud_progress_.clear();
    shroud_bank_available_ = false;
    close_shroud_bank();
    recruiter_state_ = 0;
    character_ = name;
    pending_enter_name_ = name;
    {
        // The files an earlier build kept in characters/<name>/ of the profile, copied once to the world's folder.
        std::string profile = options_.profile_id.empty() ? profile_id_for_world(ticket_.server_name) : options_.profile_id;
        if (options_.servers_dir.empty()) adopt_unsorted_character_files(profile, ticket_.server_name, character_);
        else adopt_unsorted_character_files(profile, ticket_.server_name, character_, options_.servers_dir);
    }
    load_chat_filters(); // the character's chat.ini over eqclient.ini's
    load_client_options(); // and options.ini
    load_kept_items();     // Hailstone: the Keep marks (kept_items.txt)
    blocked_buffs_ = {};   // the character's own come from its ini at the zone-in (restore_blocked_buffs)
    load_bank_snapshot();  // Hailstone: the bank as last seen (bank.dat)
    error_.clear();
    world_phase_deadline_ = {};
    world_handoff_deadline_ = Clock::now() + kWorldHandoffWait;
    state_ = ConnectionState::EnteringWorld;
    status_ = (return_home ? "Returning home as " : "Entering the world as ") + name;
}

// ---------------------------------------------------------------- zone

namespace {
double steady_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

// A spawn's position update, as the original takes it: the position and
// heading snap, the deltas become the velocity (units per 50 ms tick), the
// animation field the SpeedRun that Step pushes along the heading.
void GameConnection::set_spawn_motion(game::Spawn& s, const Motion& m) {
    // Handle_OP_ClientUpdate / unpack_physics: the update snaps the physics block
    // (game/mechanics/remote_spawns.h); WorldState::smooth() steps it.
    glm::vec3 updated = game::server_to_world({m.x, m.y, m.z});
    s.snap_error = glm::length(glm::vec2(updated - s.position));
    // A boat keeps its drawn pose and fades the correction in (game/boats.h).
    if (game::is_vehicle_race(s.race, s.gender)) game::vehicle_note_update(s, updated, m.heading);
    s.position = updated;
    s.heading = m.heading;
    s.velocity = game::server_to_world({m.delta_x, m.delta_y, m.delta_z});
    s.speed_run = m.speed_run;
    s.speed_heading = m.delta_heading;
    s.update_time = steady_seconds();
}

void GameConnection::smooth(float dt, const game::WorldState::FloorFn& floor,
                            const game::WorldState::ActorModelFn& actor_model,
                            const game::WorldState::RegionFn& region,
                            const game::WorldState::VehicleFn& vehicle_under,
                            const game::WorldState::FloorFn& ground_floor,
                            const game::WorldState::RemoteCollisionFn& collision,
                            bool ground_floor_includes_zone) {
    mount_up(world_.make_actors(actor_model));
    world_.smooth(dt, steady_seconds(), floor, region, vehicle_under, ground_floor, collision,
                  ground_floor_includes_zone);
}

void GameConnection::smooth_vehicles(float dt) { world_.smooth_vehicles(dt, steady_seconds()); }

void GameConnection::apply_spawn(const Spawn& s) {
    if (world_.spawns.contains(s.spawn_id)) forget_leadership_spawn(s.spawn_id);
    group_.mana_percent.erase(s.spawn_id);
    group_.endurance_percent.erase(s.spawn_id);
    spells_.others.erase(s.spawn_id);
    if (spells_.target_buffs_entity == s.spawn_id) {
        spells_.target_buffs_entity = 0;
        spells_.target_buffs.clear();
    }
    if (aggro_.target_id == s.spawn_id) aggro_.received = {};
    game::Spawn& out = world_.spawns[s.spawn_id];
    ++world_.spawn_set_serial; // a full same-id replacement also retires the previous identity
    out.id = s.spawn_id;
    out.incarnation = core::next_process_identity();
    out.name = s.name;
    out.last_name = s.last_name;
    out.title = s.title;
    out.suffix = s.suffix;
    out.race = static_cast<std::uint16_t>(s.race);
    out.class_id = s.class_;
    out.level = s.level;
    out.gender = s.gender;
    out.is_npc = s.npc == 1;
    out.is_corpse = s.npc >= 2;
    out.size = s.size;
    // A new EQPlayer: Height is the record's size, then SetHeights(0, 1.0); its actor is made later.
    out.heights = {};
    out.heights.height = s.size;
    out.actor_made = false;
    out.physics_step_ms.reset();
    out.form_mount_id = 0;
    world_.set_heights(out);
    out.melee_radius = s.melee_radius;
    out.walk_speed = s.walk_speed;
    out.run_speed = s.run_speed;
    set_spawn_motion(out, s.motion);
    out.fly_mode = s.fly_mode; // UnSerializeSpawn's PlayerPhysics_SetFlyMode
    ++out.fly_mode_sets;
    out.hp_percent = s.hp_percent;
    out.hp_known = s.hp_percent <= 100;
    out.stand_state = s.stand_state ? s.stand_state : 100;
    out.targetable = s.targetable;
    out.cyclable = s.targetable_with_hotkey;
    out.show_name = s.show_name;
    out.click_through = (s.bitfield >> 31) != 0;
    out.mercenary = s.is_mercenary != 0;
    out.body_type = s.body_type;
    out.properties = s.properties;
    out.summoned = s.is_pet;
    out.temp_pet = s.temp_pet != 0;
    out.pet_owner_id = s.pet_owner_id;
    out.guild_id = s.guild_id;
    out.guild_rank = s.guild_rank;
    out.guild_show = s.guild_show != 0;
    out.anon = s.anon;
    out.afk = s.afk;
    out.lfg = s.lfg;
    out.linkdead = s.linkdead;
    out.gm = s.gm;
    out.invis_rank = s.invis;
    out.light_type = s.light;
    if (out.id == world_.player_id) invis_state_ = out.invis_rank;
    out.pvp = s.pvp;
    out.deity = s.deity;
    out.trader = s.trader;
    out.buyer = (s.other_data & 1) != 0;
    out.offline = (s.other_data & 2) != 0;
    out.player_state = s.player_state;
    game::Appearance& a = out.appearance;
    a = {};
    a.texture = s.texture_type;
    a.helm = s.head_type;
    a.face = s.face;
    a.show_helm = s.show_helm;
    a.hair_style = s.hair_style;
    a.hair_color = s.hair_color;
    a.beard = s.beard;
    a.beard_color = s.beard_color;
    a.eye_color1 = s.eye_color1;
    a.eye_color2 = s.eye_color2;
    a.drakkin_heritage = s.drakkin_heritage;
    a.drakkin_tattoo = s.drakkin_tattoo;
    a.drakkin_details = s.drakkin_details;
    a.npc_tint = s.npc_tint_id;
    a.class_id = s.class_;
    if (s.interactive) a.banner = s.interactive->banner;
    for (std::size_t i = 0; i < 9; ++i) {
        a.material[i] = s.equipment[i].material;
        a.material_variation[i] = s.equipment[i].unknown1;
        a.elite_material[i] = s.equipment[i].elite_material;
        a.hero_forge[i] = s.equipment[i].hero_forge_model;
        a.new_armor_type[i] = s.equipment[i].material2;
        a.tint[i] = s.long_equipment ? s.equipment_tint[i] : 0;
    }
    game::normalize_appearance(a, s.race);
    // EQPlayer::EQPlayer applies the new-armor filter. Every corpse (NPC 2 from EQEmu for a player's and an
    // NPC's alike; RoF2 takes any Type above 1 as a corpse) has hero's forge hidden.
    refresh_new_armor_bits(out);
    note_pet_spawn(out);
    reconcile_eye_control();
}

void GameConnection::finish_server_files() {
    if (server_files_->state() == ServerFileSync::State::Failed)
        chat(ChatEvent::Kind::System, "Could not download the server's files: " + server_files_->error());
    server_files_changed_ = server_files_->changed();
    update_server_rules();
    if (zone_endpoint_) zone_endpoint_->set_no_hold(false);
    status_ = "Entering the zone";
    maybe_request_client_spawn();
}

// The server's rules from its files (plugin/policy.h): its hailstone.cfg, or none from a server that sent no list (no
// Hailstone script, or a login without a server folder). Either way the rules name the server's folder: plugins keep
// their per-server files under it (server_config_dir) on a server without Hailstone's script too.
void GameConnection::update_server_rules() {
    plugin::Rules rules;
    rules.dir = options_.server_files_dir;
    if (server_files_ && server_files_->state() != ServerFileSync::State::NoAnswer) {
        std::vector<plugin::ListedFile> files;
        for (const auto& f : server_files_->installed()) files.push_back({f.path, f.sha256});
        std::vector<std::string> warnings;
        rules = plugin::server_rules(options_.server_files_dir, files, &warnings);
        for (const auto& w : warnings) log::warn("{}: {}", plugin::kServerCfgName, w);
    }
    log::info("server rules: {}", rules.configured ? std::format("{} ({} scripts and plugins of its own)", plugin::kServerCfgName,
                                                                 rules.server_files.size())
                                                   : std::string("none (no hailstone.cfg): plugins and scripts run freely"));
    server_rules_ = std::move(rules);
    ++server_rules_serial_;
}

void GameConnection::maybe_request_client_spawn() {
    // All required zone-in values must be available; their arrival order is
    // irrelevant. Weather/time/AA notifications cannot substitute for them.
    if (state_ != ConnectionState::ZoneEntering || client_spawn_requested_ ||
        !zone_initial_.new_zone || !zone_initial_.profile || !zone_initial_.own_spawn ||
        (server_files_ && !server_files_->finished())) return;
    // ProcessGame requests AA and the tribute lists before requesting the
    // initial spawn burst. The server may answer each on the same tick.
    request_aa_data();
    if (!tributes_requested_) {
        // Tribute_SendPendingRequests @0x482d50 from ProcessGame's first zone-in: the personal list, then the guild
        // list (in a guild or not). Its two flags are cleared only by the constructor, so later zone-ins keep the
        // lists. EQEmu answers both only while we zone in (ConnectingOpcodes).
        tributes_requested_ = true;
        send_zone(messages::outgoing(GameOpcode::SendTributes));
        send_zone(messages::outgoing(GameOpcode::RequestGuildTributes));
    }
    // The spawn request starts the object burst, after the preceding table requests.
    const auto admitted = send_zone(messages::outgoing(GameOpcode::ReqClientSpawn));
    if (!admitted || *admitted.value() == 0) {
        fail("zone spawn request could not be admitted to the session");
        return;
    }
    client_spawn_requested_ = true;
    maybe_complete_zone_in();
    if (state_ == ConnectionState::Disconnected) return;
    if (!zone_ready_) status_ = "Receiving the zone";
}

void GameConnection::set_alarm(std::int64_t ms) { alarm_due_ms_ = ms > 0 ? now_ms() + ms : 0; }

void GameConnection::send_bug_report(std::uint32_t category, std::uint32_t flags, const std::string& text) {
    const game::Spawn* me = world_.player();
    if (state_ != ConnectionState::InZone || !me || text.empty()) return;
    BugReport b;
    b.category = category;
    b.reporter = character_;
    glm::vec3 at = game::world_to_server(motion_known_ ? position_ : me->position);
    b.x = at.x;
    b.y = at.y;
    b.z = at.z;
    b.heading = static_cast<std::uint32_t>(me->heading);
    b.minutes_played = profile_ ? profile_->time_played_min : 0;
    b.flags = flags;
    if (flags & kBugTargetInfo)
        if (auto it = world_.spawns.find(world_.target_id); it != world_.spawns.end()) {
            b.target_id = world_.target_id;
            b.target_name = it->second.name;
        }
    b.text = text;
    b.system_info = "Hailstone";
    send_zone(messages::outgoing(GameOpcode::Bug,b));
    ui_message("Your bug report has been sent."); // this client's line
}

void GameConnection::lfguild_post_player(const LFGuildPlayer& v) {
    if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(messages::GameOpcode::LFGuild,v));
}

void GameConnection::lfguild_post_guild(const LFGuildGuild& v) {
    if (state_ == ConnectionState::InZone && guild_.in_guild())
        send_zone(messages::outgoing(messages::GameOpcode::LFGuild,v));
}

void GameConnection::lfguild_search_players(std::uint32_t from_level, std::uint32_t to_level, std::uint32_t min_aa,
                                            std::uint32_t time_zone, std::uint32_t classes) {
    if (state_ == ConnectionState::InZone)
        send_zone(messages::outgoing(messages::GameOpcode::LFGuild, LFGuildPlayerSearch{from_level,to_level,min_aa,time_zone,classes}));
}

void GameConnection::lfguild_search_guilds(std::uint32_t level, std::uint32_t aa, std::uint32_t time_zone, std::uint32_t class_) {
    if (state_ == ConnectionState::InZone)
        send_zone(messages::outgoing(messages::GameOpcode::LFGuild, LFGuildGuildSearch{level,aa,time_zone,class_}));
}

void GameConnection::set_pvp_leaderboard_open(bool open) {
    pvp_.leaderboard_open = open;
    if (open && !pvp_.leaderboard) pvp_leaderboard_request(0);
}

void GameConnection::pvp_leaderboard_request(std::uint32_t sort) {
    if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(GameOpcode::PVPLeaderBoardRequest,sort));
}

void GameConnection::set_claims_open(bool open) {
    claims_.open = open;
    if (open) claims_refresh();
}

void GameConnection::claims_refresh() {
    if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(GameOpcode::VetClaimRequest,kVetClaimList));
}

void GameConnection::claim_reward(std::uint32_t claim_id) {
    if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(GameOpcode::VetClaimRequest,claim_id));
}

void GameConnection::dye_armor(const std::array<std::uint32_t, 9>& tints) {
    if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(GameOpcode::Dye,DyeIntent{tints}));
}

std::uint32_t GameConnection::prismatic_dye_count() const {
    std::uint32_t n = 0;
    for (const auto& [key, item] : inventory_.items()) {
        if (key.first != kInvPossessions) continue;
        if (item.id == kPrismaticDye) n += item.stackable() ? item.stack_count : 1;
        for (const auto& b : item.contents)
            if (b.item.id == kPrismaticDye) n += b.item.stackable() ? b.item.stack_count : 1;
    }
    return n;
}

void GameConnection::remove_aura(std::uint32_t spawn_id) {
    // EQEmu removes the aura and echoes the packet, which takes it off the list.
    if (state_ == ConnectionState::InZone) send_zone(messages::outgoing(GameOpcode::UpdateAura,messages::AuraUpdate{kAuraRemove, 0, spawn_id, 0, {}}));
}

void GameConnection::aggro_lock_toggle() {
    if (state_ != ConnectionState::InZone) return;
    std::uint32_t lock = aggro_.lock_id ? 0 : world_.target_id;
    if (!lock && !aggro_.lock_id) return;
    send_zone(messages::outgoing(GameOpcode::AggroMeterLockTarget, AggroTargetLock{lock})); // the server answers with OP_AggroMeterTargetInfo
}

void GameConnection::maybe_complete_zone_in() {
    if (state_ != ConnectionState::ZoneEntering || zone_ready_ || zone_initial_.completion_sent ||
        !zone_initial_.objects_sent || !zone_initial_.new_zone || !zone_initial_.profile ||
        !zone_initial_.own_spawn || !client_spawn_requested_ ||
        (server_files_ && !server_files_->finished())) return;
    // A successful admission is NOT a server acknowledgment. Remember each
    // admitted response independently; never duplicate the first after a
    // rejection of the second packet.
    if (!zone_initial_.objects_ack_sent) {
        const auto response = send_zone(messages::outgoing(GameOpcode::WorldObjectsSent));
        if (!response || *response.value() == 0) {
            fail("zone object-completion response could not be admitted");
            return;
        }
        zone_initial_.objects_ack_sent = true;
    }
    if (!zone_initial_.client_ready_sent) {
        const auto response = send_zone(messages::outgoing(GameOpcode::ClientReady));
        if (!response || *response.value() == 0) {
            fail("zone ClientReady response could not be admitted");
            return;
        }
        zone_initial_.client_ready_sent = true;
    }
    zone_initial_.completion_sent = true;
    zone_ready_ = true;
    // Republish any instrument equipped during zone entry after ClientReady is admitted.
    refresh_instrument_state();
    position_tracker_.enable();
    finish_zone_in();
    send_server_filter();
    send_target_group_buff_setting();
    merc_zone_in();
    chat(ChatEvent::Kind::System, "You have entered " +
         (world_.zone_long_name.empty() ? world_.zone_short_name : world_.zone_long_name) + ".");
}

void GameConnection::finish_zone_in() {
    if (!zone_ready_) return;
    zone_observations_retired_ = false;
    bool entered = state_ != ConnectionState::InZone;
    zone_attach_deadline_ = {};
    zone_initial_deadline_ = {};
    state_ = ConnectionState::InZone;
    status_.clear();
    report_state_ = 1; // Cmd_report's 0xddf640, set at each zone-in (the 0x345d step)
    // ProcessGame's zone-in after a death (CEverQuest +0x39580, set by 0x531730): the death tip, once.
    if (death_tip_pending_) {
        death_tip_pending_ = false;
        window_toggle_ = "tip:31";
    }
    // ProcessLocalPCIni sends the lists from the character ini after each zone-in (restore_blocked_buffs, which the
    // app calls once it has given the ini); without one, the lists this session has.
    if (!char_ini_get_) resend_blocked_buffs();
    // CEverQuest's zone-in (before "Initialization complete."): with /hidecorpses looted on, OP_HideCorpse action 3
    // again (the second word is a stack value in the original).
    if (hide_looted_corpses_) send_zone(messages::outgoing(GameOpcode::HideCorpse,HideCorpseIntent{kHideCorpseLooted,0}));
    // Which plugins and scripts may run here, once the server's files are checked (back from character select they
    // were at the login's first zone-in).
    if (entered && (!server_rules_ || server_rules_->provisional) && (!server_files_ || server_files_->finished()))
        update_server_rules();
    // Probe only an explicitly enabled extension. Do not advertise guild or
    // script capabilities until a validated settings reply is received.
    guild_world_invites_ = false;
    if (entered && zone_endpoint_ && zone_endpoint_->has_extension("hailstone"))
        send_zone(messages::outgoing(messages::GameOpcode::HailstoneSettingsRequest));
    // DoMainLoop after ReadyEnterWorld: the task window rebuilt and the task compass mark looked for (0x76ff50).
    ++tasks_.revision;
    task_compass_dirty_ = true;
}

void GameConnection::request_quest_npcs() {
    // Only of a server whose zone answered the settings request (it runs the script): others never see the opcode.
    if (!npc_find_enabled() || quest_npcs_asked_ || !zone_ready_ || !hailstone_script_) return;
    quest_npcs_asked_ = true;
    send_zone(messages::outgoing(messages::GameOpcode::HsQuestNpcsRequest));
}

std::optional<TimeOfDay> GameConnection::current_time_of_day() const {
    if (!time_) return std::nullopt;
    const auto now = options_.zone_protocol.session_config.now();
    // EQWorldData::AdvanceTime @0x7dbff0 (net::advance_time_of_day) over whole seconds. The original takes the
    // difference of two Util::FastTime readings (each whole seconds) and advances its copy at most every 3 s, always
    // from the time the packet came; the whole seconds elapsed here differ from that by under a real second. The
    // packet's time already includes the zone's offset.
    const auto seconds = std::max<std::int64_t>(
        0, std::chrono::duration_cast<std::chrono::seconds>(now - time_received_).count());
    return advance_time_of_day(*time_, static_cast<std::uint32_t>(seconds));
}

void GameConnection::handle_zone(const messages::GameMessage& message) {
    const auto op = message.opcode;
    if (op == messages::GameOpcode::HsFiles) {
        if (const auto* reply = messages::message_get<ServerFilesReply>(message);
            reply && server_files_ && !server_files_->finished()) {
            server_files_->handle(*reply, ServerFileSync::Clock::now());
            if (server_files_->finished()) finish_server_files();
        }
        return;
    }
    // The mercenary packets come on the zone stream (EQEmu queues them to the client in zone); the original's
    // HandleWorldMessage serves both streams.
    if (handle_merc(message)) return;
    if (handle_name_change(message)) return; // net/connection/game_connection_names.cpp
    if (handle_group_search(message)) return; // the group finder's replies (net/connection/game_connection_group_search.cpp)
    // Marketplace semantic messages are decoded by RoF2, never parsed as AppPacket here.
    if (handle_marketplace(message)) return;  // the marketplace's packets (net/connection/game_connection_marketplace.cpp)
    if (handle_rewards(message)) return; // the two stock Select Reward managers
    if (handle_achievements(message)) return;
    if (handle_real_estate(message)) return; // net/connection/game_connection_real_estate.cpp
    if (op == messages::GameOpcode::HailstoneGuildReply) {
        if (const auto* v = messages::message_get<HailstoneGuild>(message)) handle_hailstone_guild(*v);
        return;
    }
    if (op == messages::GameOpcode::HsQuestNpcs) {
        if (const auto* list = messages::message_get<std::vector<QuestNpc>>(message)) {
            quest_npcs_.clear();
            for (const auto& n : *list) {
                std::string key = game::display_name(n.name);
                for (char& ch : key) ch = ascii_lower(ch);
                auto& flags = quest_npcs_[key];
                flags = static_cast<std::uint8_t>(flags | n.flags);
            }
            log::info("find: {} quest NPCs from the server", quest_npcs_.size());
        }
        return;
    }
    if (op == messages::GameOpcode::HsZoneLinks) {
        if (const auto* v = messages::message_get<ZoneLinks>(message)) {
            log::info("zone links: {} from the server", v->links.size());
            zone_links_ = *v;
            ++zone_links_serial_;
        }
        return;
    }
    if (op == messages::GameOpcode::HailstoneSettings) {
        if (const auto* v = messages::message_get<HailstoneSettings>(message)) {
            log::info("server settings: flags {:#x}", v->flags);
            if (!hailstone_script_) {
                hailstone_script_ = true; // valid response to the opt-in probe
                if (zone_endpoint_ && zone_endpoint_->has_extension("hailstone"))
                    send_zone(messages::outgoing(messages::GameOpcode::HailstoneGuild, HailstoneGuild{}));
            }
        }
        return;
    }
    if (op == GameOpcode::DuelRefusal) {
        if (const auto* r = messages::message_get<DuelRefusal>(message)) handle_duel_refusal(*r);
        return;
    }
    if (op == GameOpcode::NetworkControl) {
        if (const auto* notice = messages::message_get<NetworkControlNotice>(message)) {
            switch (notice->kind) {
            case NetworkControlNotice::Kind::OfflineRefused:
                offline_mode_at_ = 0;
                break;
            case NetworkControlNotice::Kind::AttackLock:
                if (!notice->enabled) attack_locked_ = false;
                else if (notice->character == character_) {
                    attack_locked_ = true;
                    set_auto_attack(false);
                    set_auto_fire(false);
                }
                break;
            case NetworkControlNotice::Kind::ChatBlock:
                if (notice->character == character_) chat_blocked_ = notice->enabled;
                break;
            }
        }
        return;
    }
    if (op == GameOpcode::UptimeReply) {
        if (const auto* uptime = messages::message_get<UptimeNotice>(message)) {
            const auto fmt = [](std::uint32_t seconds) {
                return std::format("{:02}:{:02}:{:02}:{:02}", seconds/86400, seconds/3600%24, seconds/60%60, seconds%60);
            };
            system(4102, {fmt(uptime->world_seconds)});
            system(4103, {fmt(uptime->zone_seconds)});
        }
        return;
    }
    if (op == GameOpcode::SplitCoinCredit) {
        if (const auto* c = messages::message_get<Coins>(message)) {
            pending_split_.platinum += c->platinum;
            pending_split_.gold += c->gold;
            pending_split_.silver += c->silver;
            pending_split_.copper += c->copper;
        }
        return;
    }
    if (op == GameOpcode::TeleportIndex) { waiting_for_port_ = false; return; }
    handle_ability(message); // Actions UI consumes typed ability notices
    if (handle_fellowship(message)) return;
    if (handle_guild_banner(message)) return;
    switch (op) {
    case GameOpcode::Track:
        if (const auto* reply=messages::message_get<TrackReply>(message)) handle_track_reply(*reply);
        break;
    case GameOpcode::PlayerProfile:
        if (const auto* value = messages::message_get<PlayerProfile>(message)) profile_ = *value;
        else profile_.reset();
        zone_initial_.profile = static_cast<bool>(profile_);
        if (profile_) drunkenness_ = static_cast<std::int32_t>(std::min(profile_->intoxication, 200u));
        if (profile_) {
            sync_shroud_mode();
            fellowship_.vitality.normal = profile_->fellowship_vitality_normal;
            fellowship_.vitality.aa = profile_->fellowship_vitality_aa;
            pstatus_.hp = profile_->cur_hp;
            pstatus_.mana = profile_->mana;
            pstatus_.endurance = profile_->endurance;
            pstatus_.level = profile_->level;
            position_ = game::server_to_world({profile_->x, profile_->y, profile_->z});
            heading_ = profile_->heading;
            motion_known_ = true;
            init_spells_from_profile();
            coins_ = {profile_->platinum, profile_->gold, profile_->silver, profile_->copper};
            bank_coins_ = {profile_->platinum_bank, profile_->gold_bank, profile_->silver_bank, profile_->copper_bank,
                           profile_->platinum_shared};
            cursor_coins_ = {profile_->copper_cursor, profile_->silver_cursor, profile_->gold_cursor, profile_->platinum_cursor};
            leadership_.group_exp = profile_->group_leadership_exp;
            leadership_.raid_exp = profile_->raid_leadership_exp;
            leadership_.group_points = profile_->group_leadership_points;
            leadership_.raid_points = profile_->raid_leadership_points;
            leadership_.exp_on = profile_->leadership_exp_on;
            for (std::size_t i = 0; i < leadership_.ranks.size() && i < profile_->leadership_ranks.size(); ++i)
                leadership_.ranks[i] = profile_->leadership_ranks[i];
            tribute_.points = static_cast<std::int32_t>(profile_->tribute_points);
            tribute_.career = static_cast<std::int32_t>(profile_->career_tribute_points);
            adventure_.points.available = profile_->adventure_points_available;
            adventure_.points.theme = profile_->adventure_points;
            for (std::size_t i = 0; i < tribute_.settings.tributes.size() && i < profile_->tributes.size(); ++i) {
                tribute_.settings.tributes[i] = profile_->tributes[i].first;
                tribute_.settings.tiers[i] = profile_->tributes[i].second;
            }
            if (guild_.guild_id != profile_->guild_id) {
                reset_guild_tribute_bonuses(profile_->guild_id);
                reset_guild_permissions(profile_->guild_id);
            }
            guild_.guild_id = profile_->guild_id;
            guild_.rank = profile_->guild_rank;
            guild_.status = profile_->guild_rank;
            init_bandolier_from_profile();
            maybe_request_client_spawn();
        } else {
            log::warn("zone codec delivered a player profile without player values");
        }
        break;
    case GameOpcode::ZoneEntry:
    case GameOpcode::NewSpawn: {
        const auto* s = messages::message_get<Spawn>(message);
        if (!s) { log::warn("zone codec delivered a spawn without spawn values"); break; }
        // The old ID still belongs to our corpse. A stale living record for that ID cannot publish a respawn.
        if (respawn_spawn_pending_ && s->spawn_id == world_.player_id && s->npc == 0 && s->name == character_)
            break;
        apply_spawn(*s);
        if ((!own_spawn_seen_ || respawn_spawn_pending_) && s->npc == 0 && s->spawn_id != 0 && s->name == character_) {
            const bool respawning = respawn_spawn_pending_;
            world_.player_id = s->spawn_id;
            invis_state_ = s->invis;
            own_spawn_seen_ = true;
            if (!respawning) zone_initial_.own_spawn = true;
            // The character's anonymity is the server's (the spawn record's anon bits, EQEmu's m_pp.anon): the
            // Options window's boxes (Options/Anonymous) take it as Handle_OP_SpawnAppearance 0x15 does for us.
            int kept = client_options_.get(game::ClientOption::Anonymous) |
                       (client_options_.get(game::ClientOption::Roleplaying) << 1);
            if (s->anon != kept) keep_anon_flags(s->anon);
            // The local player is made from its own spawn record (msg_eq_netplayer): its packed position, not
            // the profile's floats.
            position_ = game::server_to_world({s->motion.x, s->motion.y, s->motion.z});
            heading_ = s->motion.heading;
            motion_known_ = true;
            if (respawning) {
                // The corpse keeps the previous ID. Only this living, matching own spawn can release the
                // same-zone respawn wait, and both network motion and the app's physics must use its placement.
                respawn_spawn_pending_ = false;
                if (zone_endpoint_) zone_endpoint_->invalidate_transient_requests();
                world_.controlled_id = 0;
                standing_on_ = controlled_boat_ = world_.driven_id = 0;
                velocity_ = glm::vec3(0.0f);
                moving_ = false;
                speed_run_ = 0.0f;
                motion_dirty_ = true;
                position_tracker_.flush();
                pstatus_.dead = false;
                invalidate_interaction_context();
                pstatus_.owner_status = 0;
                pstatus_.stand_state = s->stand_state ? s->stand_state : kStandStanding;
                respawn_placement_ = RespawnPlacement{position_, heading_};
                if (profile_) {
                    profile_->x = s->motion.x;
                    profile_->y = s->motion.y;
                    profile_->z = s->motion.z;
                    profile_->heading = heading_;
                }
            } else {
                maybe_request_client_spawn();
            }
            // An own-spawn record may overwrite the locally published instrument state.
            refresh_instrument_state();
        } else if (s->npc == 0 && s->spawn_id == world_.player_id && s->name == character_) {
            refresh_instrument_state();
        }
        break;
    }
    case GameOpcode::MobRename:
        if (const auto* rename = messages::message_get<MobRename>(message)) {
            // The server addresses the actor by its previous name, rather
            // than a spawn id. Preserve its identity, target and appearance.
            for (auto& [id, actor] : world_.spawns) {
                if (actor.name != rename->old_name) continue;
                actor.name = rename->new_name;
                if (id == world_.player_id) character_ = rename->new_name;
                ++world_.spawn_set_serial; // refresh nameplate and spawn-name lookup caches
                break;
            }
        }
        break;
    case GameOpcode::GuildBannerAppearance:
        if (const auto* update = messages::message_get<GuildBannerAppearance>(message)) {
            if (auto it = world_.spawns.find(update->spawn_id); it != world_.spawns.end() && it->second.is_npc) {
                it->second.appearance.banner = update->appearance;
                if (it->second.name != update->name) {
                    it->second.name = update->name;
                    ++world_.spawn_set_serial;
                }
            }
        }
        break;
    case GameOpcode::ControlBoat:
        if (const auto* b = messages::message_get<BoatControl>(message)) handle_control_boat(*b);
        break;
    case GameOpcode::TimeOfDay:
        if (const auto* v = messages::message_get<TimeOfDay>(message)) time_ = *v;
        time_received_ = options_.zone_protocol.session_config.now();
        break;
    case GameOpcode::Weather:
        if (const auto* w = messages::message_get<Weather>(message)) {
            log::info("weather: OP_Weather kind {:#x} intensity {}", w->kind, w->intensity);
            weather_messages_.push_back(*w);
        }
        weather_seen_ = true;
        maybe_request_client_spawn();
        break;
    case GameOpcode::NewZone:
        if (const auto* v = messages::message_get<NewZone>(message)) new_zone_ = *v;
        else new_zone_.reset();
        if (new_zone_ && !is_zone_short_name(new_zone_->zone_short_name)) {
            // It names the zone's files (archives, map layers, the navmesh cache): a path here could reach outside
            // their folders. Stock short names are letters, digits and '_' (core/zone_name.h); this one is dropped
            // and the zone loads without scenery.
            log::warn("zone: OP_NewZone's short name ({} bytes) is not a zone name; it is not used",
                      new_zone_->zone_short_name.size());
            new_zone_->zone_short_name.clear();
        }
        zone_initial_.new_zone = new_zone_ && !new_zone_->zone_short_name.empty();
        if (new_zone_) {
            world_.zone_short_name = new_zone_->zone_short_name;
            world_.zone_long_name = new_zone_->zone_long_name;
            zone_to_load_ = new_zone_->zone_short_name;
            world_.set_old_model_scale(new_zone_->old_model_scale != 0); // 0xddfba4 (NewZone +0x2b4)
            maybe_request_client_spawn();
            spells_.zone_suspends_buffs = new_zone_->suspend_buffs != 0; // 0xddfc5c (NewZone +0x36c)
        }
        break;
    case GameOpcode::SendAAStats:
        // EQEmu's AA timer reply is not a prerequisite for zone readiness.
        zone_initial_.aa_stats_seen = true;
        break;
    case GameOpcode::ClientReady:
        // This is the server-originated RoF-era notice.
        zone_initial_.server_client_ready = true;
        break;
    case GameOpcode::SendExpZonein:
        if (!zone_initial_.exp_ack_sent) {
            const auto admitted = send_zone(messages::outgoing(GameOpcode::SendExpZonein));
            if (admitted && *admitted.value() != 0) zone_initial_.exp_ack_sent = true;
        }
        break;
    case GameOpcode::WorldObjectsSent:
        // May arrive before one of the initial state records. Defer the
        // response until those records and ReqClientSpawn are complete.
        zone_initial_.objects_sent = true;
        maybe_complete_zone_in();
        break;
    case GameOpcode::ClientUpdate:
    case GameOpcode::NPCMoveUpdate: {
        if (const auto* u = messages::message_get<SpawnPositionUpdate>(message))
            rider_update(u->spawn_id, u->motion);
        break;
    }
    case GameOpcode::TargetCommand:
        if (const auto* t = messages::message_get<TargetSelection>(message)) world_.target_id = t->spawn_id;
        break;
    case GameOpcode::XTargetResponse:
        if (const auto* x = messages::message_get<XTargetResponse>(message)) apply_xtarget_response(*x);
        break;
    case GameOpcode::DeleteSpawn:
        if (const auto* removed = messages::message_get<SpawnDespawn>(message)) {
            const auto id = removed->spawn_id;
            duel_spawn_removed(id, removed->decayed);
            // msgEQRmPlayer: the trade partner (pinstTradeTarget) leaving cancels and closes the trade
            // (CTradeWnd_CancelAndClose, which sends OP_CancelTrade). EQEmu tells only the player who left.
            if (trade_ && trade_->partner_id == id) trade_cancel();
            spawn_leaving(id);
            forget_leadership_spawn(id);
            // Observations belong to this actor's lifetime, not the numerical id a later actor may reuse.
            group_.mana_percent.erase(id);
            group_.endurance_percent.erase(id);
            spells_.others.erase(id);
            if (spells_.target_buffs_entity == id) {
                spells_.target_buffs_entity = 0;
                spells_.target_buffs.clear();
            }
            if (aggro_.target_id == id) aggro_.received = {};
            if (world_.spawns.erase(id)) ++world_.spawn_set_serial;
            group_.mana_percent.erase(id);
            group_.endurance_percent.erase(id);
            forget_pet_spawn(id);
            // msgEQRmPlayer @0x4b8380: the target leaving stops auto attack and clears the target (no OP_TargetMouse).
            // Hailstone, as after a death: attack_until_xtarget_clear keeps attacking while the extended target list
            // has more, and auto_target_xtarget may pick another attacker.
            if (world_.target_id == id) {
                if (attack_until_xtarget_clear_ && auto_attack_ && xtarget_holds_spawn(id)) attack_held_for_xtarget_ = true;
                else set_auto_attack(false);
                world_.target_id = 0;
                auto_target_due_ = true;
            }
            std::erase(xtarget_attackers_, id);
        }
        break;
    case GameOpcode::SetFace:
        // Handle_OP_SetFace @0x4b6b00: another player's OP_FaceChange (the spawn by the id at +0x14): SwapFace, the
        // features, then ApplyFacialFeatures (the renderer dresses the spawn again from them).
        if (const auto* f = messages::message_get<FaceChange>(message)) {
            auto it = world_.spawns.find(f->spawn_id);
            if (it == world_.spawns.end()) break;
            game::Appearance& a = it->second.appearance;
            a.beard = f->beard;
            a.beard_color = f->beard_color;
            a.face = f->face;
            a.hair_style = f->hair_style;
            a.hair_color = f->hair_color;
            a.eye_color1 = f->eye1;
            a.eye_color2 = f->eye2;
            a.drakkin_heritage = f->heritage;
            a.drakkin_tattoo = f->tattoo;
            a.drakkin_details = f->details;
        }
        break;
    case GameOpcode::PlayerStateAdd:
    case GameOpcode::PlayerStateRemove:
        // Handle_OP_PlayerStateAdd @0x4b6d90 / Handle_OP_PlayerStateRemove @0x4b6dc0: the bit set or cleared on the
        // spawn's PlayerState.
        if (const auto* s = messages::message_get<PlayerState>(message)) {
            auto it = world_.spawns.find(s->spawn_id);
            if (it == world_.spawns.end()) break;
            if (op == GameOpcode::PlayerStateAdd) it->second.player_state |= s->state;
            else it->second.player_state &= ~s->state;
        }
        break;
    case GameOpcode::WearChange:
        if (const auto* w = messages::message_get<WearChange>(message)) {
            auto it = world_.spawns.find(w->spawn_id);
            if (it != world_.spawns.end())
                game::apply_wear_change(it->second.appearance, w->slot, w->armor.material, w->armor.hero_forge_model,
                                        w->tint, w->armor.unknown1, w->armor.elite_material, w->armor.material2);
        }
        break;
    case GameOpcode::Illusion:
        if (const auto* ill = messages::message_get<Illusion>(message)) {
            auto it = world_.spawns.find(ill->spawn_id);
            if (it != world_.spawns.end()) {
                // do_change_form: 0xff / -1 keep a field.
                game::Spawn& sp = it->second;
                game::Appearance& a = sp.appearance;
                game::MountEvents unseat;
                if (const auto mount = game::change_form_unseat(world_, sp, unseat)) sp.form_mount_id = mount;
                mount_events(unseat);
                if (ill->race != 0xffff && ill->race != 0) sp.race = ill->race;
                if (ill->gender != 0xff) sp.gender = ill->gender;
                if (ill->size > 0.0f) sp.size = ill->size;
                // do_change_form (0x5971e1..0x59722c): a size of 0 keeps Height, any other is stored (-1 then
                // takes racedata's default), then SetHeights(0, 1.0); the actor is made again.
                if (!(ill->size == 0.0f)) sp.heights.height = ill->size;
                world_.set_heights(sp, 0.0f, 1.0f);
                sp.actor_made = false;
                if (game::is_pc_model_race(sp.race)) {
                    a.texture = game::kNoTexture;
                } else {
                    a.texture = ill->texture;
                }
                if (ill->helm != 0xff) a.helm = ill->helm;
                if (ill->face != 0xffffffffu && ill->face != 0xff) a.face = static_cast<std::uint8_t>(ill->face);
                if (ill->hair_style != 0xff) a.hair_style = ill->hair_style;
                if (ill->hair_color != 0xff) a.hair_color = ill->hair_color;
                if (ill->beard != 0xff) a.beard = ill->beard;
                if (ill->beard_color != 0xff) a.beard_color = ill->beard_color;
                if (ill->drakkin_heritage != 0xffffffffu) a.drakkin_heritage = ill->drakkin_heritage;
                if (ill->drakkin_tattoo != 0xffffffffu) a.drakkin_tattoo = ill->drakkin_tattoo;
                if (ill->drakkin_details != 0xffffffffu) a.drakkin_details = ill->drakkin_details;
            }
        }
        break;
    case GameOpcode::ChangeSize:
        // PlayerZoneClient::ChangeHeight(size, viewOffset, animScale) (0x4ccefd). OP_SpawnAppearance's size
        // (type 29) is not handled by the original.
        if (const auto* c = messages::message_get<ChangeSize>(message)) {
            auto it = world_.spawns.find(c->spawn_id);
            if (it != world_.spawns.end()) {
                it->second.size = c->size;
                world_.change_height(it->second, c->size, c->view_offset, c->anim_scale);
            }
        }
        break;
    case GameOpcode::MobHealth:
    case GameOpcode::HPUpdate:
    case GameOpcode::ManaChange:
    case GameOpcode::ManaUpdate:
    case GameOpcode::EnduranceUpdate:
    case GameOpcode::ExpUpdate:
    case GameOpcode::LevelUpdate:
    case GameOpcode::Consider:
    case GameOpcode::Damage:
    case GameOpcode::Death:
    case GameOpcode::Animation:
    case GameOpcode::SomeItemPacketMaybe:
    case GameOpcode::SpawnAppearance:
    case GameOpcode::ZonePlayerToBind:
    case GameOpcode::RespawnWindow:
        handle_combat(message);
        break;
    case GameOpcode::RestState:
    case GameOpcode::BeginCast:
    case GameOpcode::InterruptCast:
    case GameOpcode::MemorizeSpell:
    case GameOpcode::Buff:
    case GameOpcode::BuffCreate:
    case GameOpcode::TargetBuffs:
    case GameOpcode::Action:
        handle_spell(message);
        break;
    case GameOpcode::DeleteSpell:
    case GameOpcode::SwapSpell:
    case GameOpcode::ColoredText:
    case GameOpcode::Stamina:
    case GameOpcode::LinkedReuse:
    case GameOpcode::DisciplineTimer:
    case GameOpcode::BlockedBuffs:
    case GameOpcode::RemoveBlockedBuffs:
    case GameOpcode::ClearBlockedBuffs:
    case GameOpcode::SpellEffect:
    case GameOpcode::AddNimbusEffect:
    case GameOpcode::RemoveNimbusEffect:
        handle_spell(message);
        break;
    case GameOpcode::CharInventory:
    case GameOpcode::ItemPacket:
    case GameOpcode::ItemLinkResponse:
    case GameOpcode::EvolveItem:
    case GameOpcode::MoveItem:
    case GameOpcode::DeleteItem:
    case GameOpcode::DeleteCharge:
    case GameOpcode::LootRequest:
    case GameOpcode::LootItem:
    case GameOpcode::LootComplete:
    case GameOpcode::MoneyOnCorpse:
    case GameOpcode::MoneyUpdate:
    case GameOpcode::Sound:
    case GameOpcode::CashReward:
    case GameOpcode::ItemVerifyReply:
    case GameOpcode::ApplyPoison:
        handle_item(message);
        break;
    case GameOpcode::AugmentInfo:
        if (const auto* info = messages::message_get<AugmentInfo>(message)) handle_augment_info(*info);
        break;
    case GameOpcode::ShopRequest:
    case GameOpcode::ShopPlayerBuy:
    case GameOpcode::ShopPlayerSell:
    case GameOpcode::ShopEnd:
    case GameOpcode::ShopEndConfirm:
    case GameOpcode::ShopDelItem:
    case GameOpcode::AltCurrency:
    case GameOpcode::AltCurrencyMerchantReply:
    case GameOpcode::AltCurrencySellSelection:
    case GameOpcode::AltCurrencySell:
    case GameOpcode::AdventureMerchantResponse:
    case GameOpcode::AdventureMerchantSell:
    case GameOpcode::ShopParcelIcon:
    case GameOpcode::ShopDeleteParcel:
    case GameOpcode::ShopSendParcel:
    case GameOpcode::ShopRetrieveParcel:
        handle_commerce(message);
        break;
    case GameOpcode::TradeRequest:
    case GameOpcode::TradeRequestAck:
    case GameOpcode::TradeAcceptClick:
    case GameOpcode::TradeCoins:
    case GameOpcode::TradeBusy:
    case GameOpcode::CancelTrade:
    case GameOpcode::FinishTrade:
    case GameOpcode::FinishWindow:
    case GameOpcode::FinishWindow2:
        handle_trade(message);
        break;
    case GameOpcode::PetCommandState:
        if (const auto* st = messages::message_get<PetButtonState>(message);
            st && st->button < pet_toggles_.size()) pet_toggles_[st->button] = st->enabled;
        break;
    case GameOpcode::PetHoTT:
        if (const auto* v = messages::message_get<PetTargetState>(message)) pets_.pet_target = v->target;
        break;
    case GameOpcode::TargetHoTT:
        if (const auto* v = messages::message_get<TargetOfTargetState>(message)) target_of_target_ = v->target;
        break;
    case GameOpcode::GroupInvite:
    case GameOpcode::GroupInvite2:
    case GameOpcode::GroupFollow:
    case GameOpcode::GroupFollow2:
    case GameOpcode::CancelInvite:
    case GameOpcode::GroupUpdate:
    case GameOpcode::GroupRoles:
    case GameOpcode::GroupUpdateB:
    case GameOpcode::GroupAcknowledge:
    case GameOpcode::GroupDisbandYou:
    case GameOpcode::GroupDisbandOther:
    case GameOpcode::GroupLeaderChange:
    case GameOpcode::GroupDelete:
    case GameOpcode::GroupLeadershipAAUpdate:
    case GameOpcode::DelegateAbility:
    case GameOpcode::RaidDelegateAbility:
    case GameOpcode::SetGroupTarget:
    case GameOpcode::MarkNPC:
    case GameOpcode::MarkRaidNPC:
    case GameOpcode::ClearNPCMarks:
    case GameOpcode::RaidClearNPCMarks:
    case GameOpcode::InspectBuffs:
    case GameOpcode::UpdateLeadershipAA:
    case GameOpcode::LeadershipExpUpdate:
    case GameOpcode::MobManaUpdate:
    case GameOpcode::MobEnduranceUpdate:
    case GameOpcode::PetBuffWindow:
        handle_group(message);
        break;
    case GameOpcode::SpawnDoor:
    case GameOpcode::MoveDoor:
    case GameOpcode::RemoveAllDoors:
    case GameOpcode::GroundSpawn:
    case GameOpcode::ClickObject:
    case GameOpcode::ClickObjectAction:
    case GameOpcode::ClearObject:
        handle_door(message);
        break;
    case GameOpcode::ChannelMessage:
    case GameOpcode::Emote:
    case GameOpcode::WhoAllResponse:
    case GameOpcode::RandomReply:
    case GameOpcode::Assist:
    case GameOpcode::GuildsList:
    case GameOpcode::GuildMOTD:
    case GameOpcode::GetGuildMOTDReply:
    case GameOpcode::GuildMemberList:
    case GameOpcode::GuildMemberUpdate:
    case GameOpcode::GuildMemberAdd:
    case GameOpcode::GuildMemberDelete:
    case GameOpcode::GuildMemberLevel:
    case GameOpcode::GuildMemberPublicNote:
    case GameOpcode::SetGuildRank:
    case GameOpcode::GuildMemberRankAltBanker:
    case GameOpcode::GuildMemberDetails:
    case GameOpcode::GuildInvite:
    case GameOpcode::ConsentResponse:
    case GameOpcode::DenyResponse:
    case GameOpcode::GuildUpdate:
    case GameOpcode::RaidUpdate:
        handle_social(message);
        break;
    case GameOpcode::SendAATable:
    case GameOpcode::RespondAA:
    case GameOpcode::AAExpUpdate:
    case GameOpcode::AAAction:
    case GameOpcode::ClearAA:
        handle_aa(message);
        break;
    case GameOpcode::GMTraining:
    case GameOpcode::GMTrainSkillConfirm:
        handle_training(message);
        break;
    case GameOpcode::SkillUpdate:
        if (const auto* value=messages::message_get<SkillUpdate>(message)) handle_skill_update(*value);
        break;
    case GameOpcode::TradeSkillCombine:
    case GameOpcode::RecipeReply:
    case GameOpcode::RecipeDetails:
    case GameOpcode::RecipeAutoCombine:
        handle_tradeskill(message);
        break;
    case GameOpcode::TaskDescription:
    case GameOpcode::TaskActivity:
    case GameOpcode::CompletedTasks:
    case GameOpcode::TaskSelectWindow:
    case GameOpcode::SharedTaskSelectWindow:
    case GameOpcode::TaskHistoryReply:
    case GameOpcode::TaskActivityComplete:
    case GameOpcode::CancelTask:
    case GameOpcode::TaskRequestTimer:
    case GameOpcode::SharedTaskMemberList:
    case GameOpcode::SharedTaskMemberChange:
        handle_progress(message);
        break;
    case GameOpcode::ShroudSelectionWindow:
    case GameOpcode::ShroudRespondStats:
    case GameOpcode::ShroudRespondAbility:
    case GameOpcode::ShroudRespondItem:
    case GameOpcode::ShroudProgress:
    case GameOpcode::ShroudProgress2:
    case GameOpcode::Shroud:
    case GameOpcode::ShroudRemove:
        handle_shroud(message);
        break;
    case GameOpcode::MonsterMissionCatalog:
    case GameOpcode::MonsterMissionStats:
    case GameOpcode::MonsterMissionAbility:
    case GameOpcode::MonsterMissionItem:
    case GameOpcode::MonsterMissionAssignment:
    case GameOpcode::MonsterMissionResult:
        handle_monster_mission(message);
        break;
    case GameOpcode::AltStorage:
    case GameOpcode::AltStorageList:
    case GameOpcode::AltStorageNoItems:
        handle_shroud_bank(message);
        break;
    case GameOpcode::Trader:
    case GameOpcode::BecomeTrader:
    case GameOpcode::TraderShop:
    case GameOpcode::TraderBulkSend:
    case GameOpcode::BazaarSearch:
    case GameOpcode::TraderDelItem:
    case GameOpcode::Barter:
    case GameOpcode::BuyerItems:
        handle_market(message);
        break;
    case GameOpcode::TributeInfo:
    case GameOpcode::TributeUpdate:
    case GameOpcode::TributeTimer:
    case GameOpcode::TributePointUpdate:
    case GameOpcode::SelectTribute:
    case GameOpcode::OpenTributeMaster:
    case GameOpcode::TributeItem:
    case GameOpcode::TributeMoney:
    case GameOpcode::SendGuildTributes:
    case GameOpcode::GuildSelectTribute:
    case GameOpcode::GuildSendActiveTributes:
    case GameOpcode::GuildTributeToggleReply:
    case GameOpcode::GuildTributeFavorAndTimer:
    case GameOpcode::GuildOptInOut:
    case GameOpcode::GuildTributeDonateItem:
    case GameOpcode::GuildTributeDonatePlat:
    case GameOpcode::OpenGuildTributeMaster:
    case GameOpcode::GuildModifyBenefits:
        handle_tribute(message);
        break;
    case GameOpcode::DzExpeditionInfo:
    case GameOpcode::DzExpeditionInvite:
    case GameOpcode::DzExpeditionLockoutTimers:
    case GameOpcode::DzMemberList:
    case GameOpcode::DzMemberListName:
    case GameOpcode::DzMemberListStatus:
    case GameOpcode::DzSetLeaderName:
    case GameOpcode::DzExpeditionEndsWarning:
    case GameOpcode::DzChooseZone:
    case GameOpcode::DzCompass:
        handle_dz(message);
        break;
    case GameOpcode::AdventureInfo:
    case GameOpcode::AdventureDetails:
    case GameOpcode::AdventureData:
    case GameOpcode::AdventureUpdate:
    case GameOpcode::AdventureFinish:
    case GameOpcode::AdventurePointsUpdate:
    case GameOpcode::AdventureStatsReply:
    case GameOpcode::AdventureLeaderboardReply:
        handle_adventure(message);
        break;
    case GameOpcode::GuildBank:
    case GameOpcode::GuildBankItemList:
        handle_guild_bank(message);
        break;
    case GameOpcode::Charm:
        // A charm lands (1) or breaks (0); sent to everyone near the pet, ours when the owner is us.
        if (const auto* c = messages::message_get<PetCharmState>(message)) {
            auto it = world_.spawns.find(c->pet);
            if (it == world_.spawns.end()) break;
            if (c->charmed) {
                it->second.pet_owner_id = c->owner;
                note_pet_spawn(it->second);
            } else if (it->second.pet_owner_id == c->owner) {
                it->second.pet_owner_id = 0;
                forget_pet_spawn(c->pet);
            }
        }
        break;
    case GameOpcode::DisciplineUpdate:
        // Sent when a discipline is learned (at zone-in the profile carries the same list).
        if (const auto* d = messages::message_get<LearnedDisciplines>(message); d && profile_) profile_->disciplines = d->spell_ids;
        break;
    case GameOpcode::Untargetable:
        if (const auto* u = messages::message_get<TargetabilityNotice>(message))
            if (auto it = world_.spawns.find(u->spawn_id); it != world_.spawns.end()) it->second.targetable = u->targetable;
        break;
    case GameOpcode::YellForHelp:
        // Sent to the players near the yeller; the line is this client's.
        if (const auto* notice = messages::message_get<AssistanceNotice>(message))
            if (auto it = world_.spawns.find(notice->spawn_id); it != world_.spawns.end())
                ui_message(std::format("{} yells for help!", game::display_name(it->second.name)));
        break;
    case GameOpcode::Weblink:
        // The case @0x4c5b5d hands the address to WebManager_CreateHtmlWnd (an in-game HTML window); the app opens it.
        if (const auto* url = messages::message_get<ExternalLinkNotice>(message)) web_links_.push_back(url->url);
        break;
    case GameOpcode::Stun:
        // The case @0x4c7093: StunMe(duration, 0, 0, the lean byte) with a local character. EQEmu's UnStun sends a
        // duration of 0 too (a fear fading with Combat:EnableFearPathing off, a quest's UnStun): StunMe ignores it
        // while a stun runs, and makes it a 1000 ms stun otherwise.
        if (const auto* s = messages::message_get<StunNotice>(message)) stun_me(s->duration_ms, s->lean);
        break;
    case GameOpcode::ItemPreviewRequest:
        if (const auto* reply = messages::message_get<ItemPreviewReply>(message)) handle_item_preview(*reply);
        break;
    case GameOpcode::ItemRecastDelay:
        // The case @0x4c70e8: with a local PC and player, when LaunchSpellData_IsCastItemValid (the cast's item is
        // at its location, Type 0, with the cast's item id) or the packet's ignore_casting_requirement byte is set,
        // PcZoneClient_SetItemRecastTimer(the cast item when LaunchSpellData_IsItemCast, else none, delay, type).
        if (const auto* d = messages::message_get<ItemReuseNotice>(message); d && profile_) {
            if (valid_cast_item() || d->ignore_cast_requirement) {
                set_item_recast_timer(cast_item(), static_cast<std::int32_t>(d->delay_s), static_cast<std::int32_t>(d->reuse_type));
            }
        }
        break;
    case GameOpcode::SharedTaskInvite:
        // CTaskManager_HandleSharedTaskInvite @0x4803b0: the invitation id kept (+0x19c64c AddPlayerID), eqstr 8934
        // with the inviter (+0x48) and the task (+8), Yes / No without a time limit.
        if (const auto* invitation = messages::message_get<SharedTaskInvite>(message)) {
            Confirmation question;
            question.id = kConfirmSharedTask;
            question.text = strings_.format(8934, std::vector<std::string>{invitation->inviter, invitation->task});
            question.pending_message = message;
            ask(std::move(question));
        }
        break;
    case GameOpcode::Translocate:
    case GameOpcode::RezzRequest: {
        // Unanswered, the server waits (its own time limit applies). This client's questions (the original's dialogs
        // are not decoded).
        const auto* offer = messages::message_get<ZoneOffer>(message);
        if (!offer) break;
        const bool tl = offer->kind == ZoneOfferKind::Translocate;
        if (!tl) {
            if (respawn_spawn_pending_ || (respawn_ && respawn_chosen_)) break;
            clear_hover_rez();
            // A new server resurrection offer replaces the old one; queued dialogs must not answer superseded
            // corpse/spell data after the current offer has been accepted or the hover timeout has completed.
            discard_questions([](const Confirmation& c) { return c.id == kConfirmRezz; });
        }
        std::string where = game::zone_long_name(offer->zone);
        if (where.empty()) where = std::format("zone {}", offer->zone);
        std::string text = tl ? std::format("{} offers to move you to {}. Go?", game::display_name(offer->source), where)
                              : std::format("{} offers to resurrect you. Accept?", game::display_name(offer->source));
        Confirmation question;
        question.id = tl ? kConfirmTranslocate : kConfirmRezz;
        question.text = std::move(text);
        question.pending_message = message;
        ask(std::move(question));
        break;
    }
    case GameOpcode::FindPersonReply:
        if (const auto* points=messages::message_get<std::vector<glm::vec3>>(message)) path_reply(*points);
        break;
    case GameOpcode::SendFindableNPCs:
        if (const auto* npc = messages::message_get<FindableNpc>(message)) {
            if (npc->remove) findable_npcs_.erase(npc->spawn_id);
            else findable_npcs_[npc->spawn_id] = *npc;
        }
        break;
    case GameOpcode::LFGuild:
        if (const auto* v = messages::message_get<LFGuildReply>(message)) {
            if (v->player) lfguild_.player = *v->player;
            if (v->guild) lfguild_.guild = *v->guild;
            if (v->players) lfguild_.players = *v->players;
            if (v->guilds) lfguild_.guilds = *v->guilds;
        }
        break;
    case GameOpcode::PVPStats:
        if (const auto* value = messages::message_get<PvPStats>(message)) pvp_.stats = *value;
        break;
    case GameOpcode::PVPLeaderBoardReply:
        if (const auto* value = messages::message_get<PvPLeaderboard>(message)) pvp_.leaderboard = *value;
        break;
    case GameOpcode::CrystalCountUpdate:
        if (const auto* value = messages::message_get<CrystalCounts>(message)) crystals_ = *value;
        break;
    case GameOpcode::VetRewardsAvaliable:
        if (const auto* value = messages::message_get<std::vector<VetReward>>(message)) claims_.rewards = *value;
        break;
    case GameOpcode::VetClaimReply:
        if (const auto* value = messages::message_get<VetClaimReply>(message)) {
            ui_message(value->claimed ? "Your reward has been claimed." : "That reward could not be claimed.");
            if (value->claimed) claims_refresh();
        }
        break;
    case GameOpcode::UpdateAura:
        if (const auto* value = messages::message_get<messages::AuraUpdate>(message)) {
            auto same = [&](const messages::AuraUpdate& a) { return a.spawn_id == value->spawn_id; };
            if (value->action == kAuraClear) auras_.clear();
            else if (value->action == kAuraRemove) std::erase_if(auras_, same);
            else if (value->action == kAuraAdd && std::none_of(auras_.begin(), auras_.end(), same)) auras_.push_back(*value);
        }
        break;
    case GameOpcode::AggroMeterTargetInfo:
        if (const auto* v = messages::message_get<AggroTargetInfo>(message)) {
            aggro_.lock_id = v->lock_id;
            if (aggro_.target_id != v->target_id) aggro_.received = {};
            aggro_.target_id = v->target_id;
        }
        break;
    case GameOpcode::AggroMeterUpdate:
        if (const auto* v = messages::message_get<AggroUpdate>(message)) {
            if (v->secondary_id) aggro_.secondary_id = *v->secondary_id;
            for (auto [type, pct] : v->entries)
                if (type < kAggroTypes) {
                    aggro_.pct[type] = pct;
                    aggro_.received[type] = true;
                }
            if (!aggro_seen_) aggro_.window_open = aggro_seen_ = true;
        }
        break;
    case GameOpcode::InspectAnswer:
        if (const auto* answer=messages::message_get<InspectAnswer>(message)) handle_inspect_answer(*answer);
        break;
    case GameOpcode::RequestDuel:
    case GameOpcode::DuelAccept:
        handle_player(message);
        break;
    case GameOpcode::ReadBook:
        if (const auto* book=messages::message_get<ReadBook>(message)) handle_read_book(*book);
        break;
    case GameOpcode::Bandolier:
    case GameOpcode::Surname:
    case GameOpcode::OnLevelMessage:
    case GameOpcode::SendTitleList:
    case GameOpcode::SetTitleReply:
    case GameOpcode::NewTitlesAvailable:
        handle_player(message);
        break;
    case GameOpcode::SpecialMesg:
        if (const auto* m = messages::message_get<SpecialMessage>(message)) handle_special_message(*m);
        break;
    case GameOpcode::SimpleMessage:
        if (const auto* m = messages::message_get<SimpleMessage>(message))
            chat(ChatEvent::Kind::System, strings_.format(m->string_id, {}), 0, m->color);
        break;
    case GameOpcode::FormattedMessage:
        if (const auto* m = messages::message_get<FormattedMessage>(message)) {
            auto args = m->args;
            if (m->string_id == 1383) duel_decline_message(args); // EQEmu's DUEL_DECLINE
            auto full = strings_.format(m->string_id, args);
            std::optional<std::string> display;
            auto line = game::spell_message_hit_report(m->string_id, args);
            if (!line && m->string_id == 6688 && args.size() == 1)
                line = game::spell_text_hit_report(args[0], m->type);
            if (line) {
                auto mode = spell_hit_modes_[static_cast<std::size_t>(line->kind)];
                display = game::report_spell_hit_line(*line, full, mode);
            }
            chat(ChatEvent::Kind::System, std::move(full), 0, m->type, {}, std::move(display));
            if (m->string_id > 0x1d4 && m->string_id < 0x1d9) context_tip(0x1e); // msgTokenText: the faction tip
        }
        break;
    case GameOpcode::SendZonepoints:
        if (const auto* zp = messages::message_get<std::vector<ZonePoint>>(message)) {
            zone_points_ = *zp;
            log::debug("{} zone points", zone_points_.size());
        }
        break;
    case GameOpcode::RequestClientZoneChange:
        // One request at a time: MoveToZone (@0x541710) blocks on the answer to its OP_ZoneChange
        // (RequestZoneChangeStatusBlocking), so a second request the server sends for the same move (a teleport
        // door's click is seen to send two) is never answered. Answering it is harmful: the server has already
        // taken the first and reads the second as unsolicited, and its SendZoneCancel puts the saved position
        // back where the click was, so the character arrives in the new zone at the old zone's x, y.
        if (zone_change_pending_ || state_ == ConnectionState::Zoning) {
            log::debug("ignoring OP_RequestClientZoneChange while a zone change is under way");
            break;
        }
        if (const auto* r = messages::message_get<RequestClientZoneChange>(message)) {
            // CEverQuest::HandleWorldMessage @0x4d13cd: DoTeleportB(zone dword, +8, +0xc, z, heading, "", type,
            // +4, ...) when the zone's .s3d or .eqg exists (the zone is loaded here): a request for the zone the
            // player is in (#goto, a summon) moves the player without zoning; another zone goes on to MoveToZone.
            if (teleport_handler_) {
                game::TeleportBArgs a;
                a.zone = static_cast<std::int32_t>(static_cast<std::uint32_t>(r->zone_id) |
                                                   (static_cast<std::uint32_t>(r->instance_id) << 16));
                a.x = r->y; // world x is the server's y
                a.y = r->x;
                a.z = r->z;
                a.heading = r->heading;
                a.reason = static_cast<std::int32_t>(r->type);
                a.arg8 = static_cast<std::int32_t>(r->unknown004);
                a.arg9 = 0;
                teleport_handler_(a);
                break;
            }
            ZoneChange z;
            z.char_name = character_;
            z.zone_id = r->zone_id;
            z.instance_id = r->instance_id;
            z.x = r->x;
            z.y = r->y;
            z.z = r->z;
            z.zone_reason = r->type;
            start_zone_change(z);
        }
        break;
    case GameOpcode::ZoneChange:
        if (const auto* z = messages::message_get<ZoneChange>(message)) {
            if (z->success == -1 && zone_change_pending_ && Clock::now() - zone_change_started_ < kZoneChangeRetryLimit) {
                zone_change_retry_at_ = Clock::now() + kZoneChangeRetryDelay;
                break;
            }
            zone_change_pending_ = false;
            zone_change_retry_at_ = {};
            if (z->success == 1 && state_ == ConnectionState::Zoning) {
                // A second answer (seen live after a zone-line request) must
                // not open a second world session: the server then sees two
                // zone connections and drops the character as link-dead.
                log::debug("ignoring a repeated OP_ZoneChange answer");
            } else if (z->success == 1) {
                // MoveToZone @0x541710: OP_DeleteSpawn with our id, then WaitForWorldDisconnect until the zone drops
                // the link (EQEmu's Handle_OP_DeleteSpawn does); then back in through world (zoning = 1).
                const auto admitted = send_zone(messages::outgoing(GameOpcode::DeleteSpawn, SpawnDespawn{world_.player_id}));
                if (!admitted || *admitted.value() == 0) {
                    ui_message("The zone transition could not be sent.");
                    break;
                }
                chat(ChatEvent::Kind::System, "Zoning...");
                // A camp under way ends here, silently: the next zone's EnterZone runs ResetClientStateForZone(1)
                // @0x5319f0, which clears the camp's time (0xddf8a0) and its kind (0xdd2664). Nothing reads them
                // while zoning (a link death then is not in game).
                camp_started_ = {};
                camp_kind_ = 0;
                state_ = ConnectionState::Zoning;
                retire_zone_observations();
                reset_fellowship(true);
                reset_guild_banner();
                reset_achievements();
                reset_rewards();
                status_ = "Leaving the zone";
                zone_exit_ = ZoneExit::Zoning;
                zone_exit_deadline_ = Clock::now() + kZoneDropWait;
            } else if (z->success < 0) {
                chat(ChatEvent::Kind::System, "You can't go there right now.");
            }
        }
        break;
    default:
        log::debug("unhandled semantic zone message {}", static_cast<std::uint32_t>(op));
        break;
    }
}

std::optional<std::string> GameConnection::take_zone_to_load() {
    if (server_files_ && !server_files_->finished()) return std::nullopt;
    auto z = std::move(zone_to_load_);
    zone_to_load_.reset();
    return z;
}

void GameConnection::set_combat_report_range(float range) {
    combat_report_range_ = std::isfinite(range) && range > 0.0f ? range : 0.0f;
}

bool GameConnection::hit_reported(std::uint32_t target, std::uint32_t source) const {
    // Handle_OP_Damage reports no hit between others more than 75 units away (@0x4b5a8a). Hailstone extension:
    // combat.report_range replaces the 75 when set.
    const std::uint32_t me = world_.player_id;
    if (target == me || source == me) return true;
    auto t = world_.spawns.find(target);
    auto m = world_.spawns.find(me);
    return t != world_.spawns.end() && m != world_.spawns.end() &&
           glm::distance(t->second.position, m->second.position) <= (combat_report_range_ > 0.0f ? combat_report_range_ : kRof2HitReportRange);
}

void GameConnection::report_successful_hit(const CombatDamage& d, bool reported) {
    const std::uint32_t me = world_.player_id;
    auto spawn = [&](std::uint32_t id) -> game::Spawn* {
        auto it = world_.spawns.find(id);
        return it == world_.spawns.end() ? nullptr : &it->second;
    };
    if (d.type == 231) {
        // A spell hit: ReportSuccessfulHit's spell branch.
        if (const auto* sp = spell_table_->get(d.spell_id))
            if (auto full = game::spell_hit_message(strings_, world_, *sp, d.target, d.damage)) {
                std::optional<std::string> display;
                if (auto line = game::spell_damage_hit_report(world_, *sp, d.target, d.damage)) {
                    auto mode = spell_hit_modes_[static_cast<std::size_t>(line->kind)];
                    display = game::report_spell_hit_line(*line, *full, mode);
                }
                chat(ChatEvent::Kind::Spell, std::move(*full), 0, 0, {}, std::move(display));
            }
    } else if (game::is_environment_hit(d.type)) {
        // The environmental branch (game/combat.h environment_hit): its line, fall_hit, and the victim's Hit1..4
        // when there is damage (the shared tail @0x5311ee; no FlshHit for these types).
        if (const game::Spawn* t = spawn(d.target)) {
            game::EnvHitInput in;
            in.type = d.type;
            in.damage = d.damage;
            in.on_me = d.target == me;
            in.victim = game::display_name(t->name);
            in.spell = d.spell_id;
            in.zone_id = static_cast<std::int32_t>(zone_id());
            in.filter_mode = chat_filters_.mode(0x11);
            game::EnvHit e = game::environment_hit(strings_, in);
            if (e.fall_hit_sound) {
                SoundCue c;
                c.kind = SoundCue::Kind::SpawnWave;
                c.target = d.target;
                c.spell = game::kSoundFallHurt;
                sound_cues_.push_back(c);
            }
            if (reported && !e.text.empty()) chat(ChatEvent::Kind::Combat, e.text, 0, e.color);
            if (d.damage > 0 && !t->is_corpse) {
                SoundCue c;
                c.kind = SoundCue::Kind::Hit;
                c.target = d.target;
                c.source = d.source;
                c.damage = d.damage;
                c.environment = true;
                sound_cues_.push_back(c);
            }
        }
    } else {
        if (auto r = game::damage_report(strings_, world_, d.target, d.source, d.type, d.damage, d.special)) {
            report_hit(*r, d.source, reported);
            last_hit_ = {d.target, d.source, d.damage, Clock::now()};
        }
        melee_sound(d.target, d.source, d.damage);
    }
    // ReportSuccessfulHit's animation on the target (game/combat.h hit_animation).
    if (auto* t = spawn(d.target)) {
        game::HitAnimInput h;
        h.type = d.type;
        h.damage = d.damage;
        h.target_alive = !t->is_corpse;
        h.target_stand_state = t->stand_state;
        h.local_stand_state = static_cast<std::uint8_t>(pstatus_.stand_state);
        if (d.type == 231)
            if (const auto* sp = spell_table_->get(d.spell_id)) {
                h.spell_target_anim = sp->target_anim;
                h.spell_detrimental = !sp->beneficial();
            }
        if (auto anim = game::hit_animation(h)) {
            t->action = *anim;
            t->action_speed = 10;
            ++t->action_seq;
        }
        // The tail's landed-hit half (@0x5311ee, game/combat.h hit_landed), for every hit type.
        if (game::hit_landed(h)) {
            // ReportSuccessfulHit's blood branch: only the local attacker's sharp-weapon skills. The renderer
            // checks the target's BLD_DAG attachment and sprite; other hits never invent an attachment.
            if (d.source == me && (d.type <= 3 || d.type == 8 || d.type == 10 || d.type == 36 || d.type == 77) &&
                blood_spurts_.size() < 128)
                blood_spurts_.push_back(d.target);
            if (d.target == me) {
                // The spell book closes (its Deactivate: OnShow stands a player it sat down), then a kneeling or
                // sitting player stands, and a mounted one too (unk_SetStandState(100)).
                if (spell_book_open_) spell_book_closed_by_game(true);
                if (pstatus_.stand_state == kStandLooting || pstatus_.stand_state == kStandSitting)
                    set_stand_state(kStandStanding);
                if (mount_id()) set_stand_state(kStandStanding);
            }
            // Hit1..4: the melee and environmental branches above queue theirs (game::hit_sounds); a detrimental
            // spell's plays here, without the melee branch's 0x93 / 0x94.
            if (d.type == 231) {
                SoundCue c;
                c.kind = SoundCue::Kind::Hit;
                c.target = d.target;
                c.source = d.source;
                c.damage = d.damage;
                c.environment = true;
                sound_cues_.push_back(c);
            }
        }
    }
}

void GameConnection::melee_sound(std::uint32_t target, std::uint32_t source, std::int32_t damage) {
    if (!hit_reported(target, source)) return;
    SoundCue c;
    c.kind = SoundCue::Kind::Hit;
    c.target = target;
    c.source = source;
    c.damage = damage;
    sound_cues_.push_back(c);
}

std::vector<SoundCue> GameConnection::take_sound_cues() {
    for (auto& d : doors_) {
        for (std::uint8_t st : d.sound_states) {
            SoundCue c;
            c.kind = SoundCue::Kind::Door;
            c.door_type = d.type;
            c.door_state = st;
            c.position = d.world_position();
            sound_cues_.push_back(c);
        }
        d.sound_states.clear();
    }
    std::vector<SoundCue> out;
    out.swap(sound_cues_);
    return out;
}

std::optional<ChatEvent> GameConnection::poll_chat() {
    if (chat_.empty()) return std::nullopt;
    ChatEvent e = std::move(chat_.front());
    chat_.pop_front();
    return e;
}

void GameConnection::set_player_motion(const glm::vec3& position, float heading, const glm::vec3& velocity,
                                       bool moving) {
    // During hover the player ID belongs to the corpse; local physics must not move its server-owned pose.
    if (pstatus_.dead) return;
    // Against the place last sent, not last frame's: at a high frame rate a slow walk (sneaking, snared) moves less
    // than 0.01 a frame and would never be reported.
    if (glm::distance(position, last_sent_position_) > 0.01f || moving != moving_) motion_dirty_ = true;
    float dh = std::abs(heading - last_sent_heading_);
    if (std::min(dh, 512.0f - dh) >= 2.0f) motion_dirty_ = true;
    // PlayerAnimation_SelectMovementAnim (run when 0x4afe60 finds the player moving: squared speed 0.01 or
    // more) deactivates an open spell book unless the horizontal speed is below 0.001 on both axes.
    if (spell_book_open_ && glm::dot(velocity, velocity) >= 0.01f &&
        (std::abs(velocity.x) >= 0.001f || std::abs(velocity.y) >= 0.001f))
        spell_book_closed_by_game(true);
    position_ = position;
    heading_ = heading;
    velocity_ = velocity;
    moving_ = moving;
    if (auto it = world_.spawns.find(world_.player_id); it != world_.spawns.end()) {
        it->second.position = position;
        it->second.heading = heading;
    }
}

void GameConnection::send_position(bool force) {
    // A dead player's spawn is a corpse now; nothing to report until the re-zone.
    if (!zone_endpoint_ || world_.player_id == 0 || pstatus_.dead) return;
    reconcile_eye_control();
    send_eye_position(force);
    auto now = Clock::now();
    if (!force && (!motion_dirty_ || now - last_position_send_ < kPositionInterval)) return;
    // DoMainLoop: the sequence is the position tracker's sample count, then sample 2 is recorded.
    ClientPositionUpdate u;
    u.sequence = position_tracker_.sequence();
    record_position_sample(game::kPositionUpdate);
    u.spawn_id = static_cast<std::uint16_t>(world_.player_id);
    glm::vec3 world_p = position_, world_v = velocity_;
    float heading = heading_;
    // Standing on a vehicle: the physics block in the vehicle's frame (Vehicle_ToLocal) and the vehicle's id
    // (PackPlayerPositionUpdateOnVehicle; the server adds the vehicle's position and heading back). The frame is the
    // boat as it is drawn, the deck the player stands on (game/boats.h): the place on it is what the server needs.
    if (auto veh = standing_on_ ? world_.spawns.find(standing_on_) : world_.spawns.end(); veh != world_.spawns.end()) {
        const game::Spawn& vs = veh->second;
        const game::BoatPose deck = game::drawn_pose(vs);
        game::mechanics::VehicleBody b{world_p.x, world_p.y, world_p.z, world_v.x, world_v.y, world_v.z, heading, 0.0f};
        game::mechanics::vehicle_to_local({deck.position.x, deck.position.y, deck.position.z, deck.heading, 0.0f}, b);
        world_p = glm::vec3(b.x, b.y, b.z);
        world_v = glm::vec3(b.speed_x, b.speed_y, b.speed_z);
        heading = b.heading;
        u.vehicle_id = static_cast<std::uint16_t>(vs.id);
    }
    glm::vec3 p = game::world_to_server(world_p);
    glm::vec3 v = game::world_to_server(world_v);
    u.motion.x = p.x;
    u.motion.y = p.y;
    u.motion.z = p.z;
    u.motion.heading = heading;
    u.motion.delta_x = v.x;
    u.motion.delta_y = v.y;
    u.motion.delta_z = v.z;
    // PackPlayerPositionUpdate @0x4dd360: the animation field is SpeedRun (+0x7c, clamped to -12.8..12.7) x 40,
    // truncated (__ftol2_sse); other clients unpack it x 0.025 as the spawn's SpeedRun (its walk / run animation).
    // A ridden mount's (the controlled spawn's) goes out under the rider's id (DoMainLoop @0x53e28b).
    float speed_run = std::clamp(speed_run_, -12.8f, 12.7f);
    u.motion.animation = static_cast<std::int32_t>(speed_run * 40.0f);
    send_zone(messages::outgoing(GameOpcode::ClientUpdate, u,
                                     {messages::Reliability::Unreliable, false}));
    last_position_send_ = now;
    last_sent_position_ = position_;
    last_sent_heading_ = heading_;
    motion_dirty_ = false;
}

void GameConnection::acquire_eye_control() {
    if (state_ != ConnectionState::InZone || pstatus_.dead || own_effect_with(67) < 0) return;
    if (!game::take_eye_control(world_, game::find_eye_summon(world_))) return;
    // SetViewActor @0x48f030 acknowledges the remote view with its ID (0 restores the local view). Stock EQEmu
    // ignores this message, but it is still part of the original client's view transition.
    send_zone(messages::outgoing(GameOpcode::AckPacket, ViewActorAcknowledgement{world_.view_id}));
    eye_motion_dirty_ = true;
    moving_ = false;
    motion_dirty_ = true;
}

void GameConnection::release_eye_control(bool remove_effect) {
    if (!world_.eye_id) return;
    if (remove_effect)
        if (int slot = own_effect_with(67); slot >= 0) {
            send_zone(messages::outgoing(messages::GameOpcode::BuffRemoveRequest,
        messages::BuffRemoval{static_cast<std::uint32_t>(slot), world_.player_id}));
            spells_.buffs[static_cast<std::size_t>(slot)] = {};
        }
    if (!game::release_eye_control(world_)) return;
    eye_motion_dirty_ = false;
    last_eye_position_send_ = {};
    send_zone(messages::outgoing(GameOpcode::AckPacket, ViewActorAcknowledgement{}));
}

void GameConnection::reconcile_eye_control() {
    if (!world_.eye_id) return;
    // The held Eye stays held while its spawn lives under the Eye's name; the ambiguity search is only for taking
    // control. Re-running it here let another player's same-prefix Eye retire ours (and remove our buff).
    const bool retired = !game::is_own_eye(world_, world_.eye_id);
    if (pstatus_.dead || retired || own_effect_with(67) < 0) release_eye_control(retired || pstatus_.dead);
}

void GameConnection::send_eye_position(bool force) {
    const auto eye = world_.spawns.find(world_.eye_id);
    if (eye == world_.spawns.end() || !zone_endpoint_) return;
    const auto now = Clock::now();
    if (!force && (!eye_motion_dirty_ || now - last_eye_position_send_ < kPositionInterval)) return;
    const game::Spawn& spawn = eye->second;
    ClientPositionUpdate u;
    u.sequence = position_tracker_.sequence();
    u.spawn_id = static_cast<std::uint16_t>(spawn.id);
    const auto p = game::world_to_server(spawn.position);
    const auto v = game::world_to_server(spawn.velocity);
    u.motion.x = p.x;
    u.motion.y = p.y;
    u.motion.z = p.z;
    u.motion.heading = spawn.heading;
    u.motion.delta_x = v.x;
    u.motion.delta_y = v.y;
    u.motion.delta_z = v.z;
    u.motion.animation = static_cast<std::int32_t>(std::clamp(spawn.speed_run, -12.8f, 12.7f) * 40.0f);
    send_zone(messages::outgoing(GameOpcode::ClientUpdate, u,
                                     {messages::Reliability::Unreliable, false}));
    last_eye_position_send_ = now;
    eye_motion_dirty_ = false;
}

void GameConnection::leave_zone_dropped() {
    reset_achievements();
    reset_rewards();
    // The zone has dropped the link (or was closed after the wait).
    ZoneExit exit = std::exchange(zone_exit_, ZoneExit::None);
    if (exit == ZoneExit::Zoning) {
        connect_world(true);
    } else if (state_ != ConnectionState::Disconnected) {
        // DoMainLoop's exit: 0x1e6 back to character select, or 0x1e5 for /camp desk and /camp server.
        // ProcessGame's "Cleaning up groups." for those exits: in a raid, CRaid::SetTargetRaidPlayer(0) then
        // RemoveRaidMember (our own leave, sent to the link the zone has already dropped), then ResetRaid.
        if (raid_.active) raid_.target.clear();
        raid_.clear();
        raid_.inviter.clear(); // ResetRaid's state 1 ends an invitation too
        reset_fellowship();
        capture_bank();
        state_ = ConnectionState::Disconnected;
        error_ = "Camped out.";
        camped_out_ = camp_kind_;
    }
}

bool GameConnection::start_zone_change(const ZoneChange& request) {
    // A server-initiated teleport can arrive while the 30-second camp timer
    // is active. A successful zone transfer cancels that camp, rather than
    // leaving the character stranded on the old zone connection.
    if ((state_ != ConnectionState::InZone && state_ != ConnectionState::Camping) || zone_change_pending_) return false;
    const auto admitted = send_zone(messages::outgoing(GameOpcode::ZoneChange, request));
    if (!admitted || *admitted.value() == 0) return false;
    if (respawn_ || respawn_spawn_pending_) {
        clear_hover_rez();
        discard_questions([](const Confirmation& c) { return c.id == kConfirmRezz; });
    }
    zone_change_request_ = request;
    zone_change_pending_ = true;
    zone_change_started_ = zone_change_sent_ = Clock::now();
    zone_change_retry_at_ = {};
    return true;
}

bool GameConnection::request_zone_change(std::uint16_t zone_id, std::uint16_t instance_id, const glm::vec3& target) {
    if (state_ != ConnectionState::InZone || zone_change_pending_ || !zone_endpoint_) return false;
    // The server picks the zone point nearest to the last reported position,
    // so report where we are first (DoTeleportB sends OP_ClientUpdate first).
    motion_dirty_ = true;
    send_position(true);
    ZoneChange z;
    z.char_name = character_;
    z.zone_id = zone_id;
    z.instance_id = instance_id;
    glm::vec3 s = game::world_to_server(target);
    z.x = s.x;
    z.y = s.y;
    z.z = s.z;
    z.zone_reason = 0;
    if (!start_zone_change(z)) return false;
    log::info("zone change requested: zone {} instance {} target server ({:.1f}, {:.1f}, {:.1f})", zone_id,
              instance_id, s.x, s.y, s.z);
    return true;
}

void GameConnection::simulate_link_loss() {
    log::info("testing: the links go silent");
    if (zone_endpoint_) zone_endpoint_->simulate_link_loss();
    if (world_endpoint_) world_endpoint_->simulate_link_loss();
}

void GameConnection::disconnect() {
    close_evolving_transfer();
    reset_fellowship();
    capture_bank();
    reset_achievements();
    reset_rewards();
    respawn_.reset();
    clear_hover_rez();
    respawn_spawn_pending_ = false;
    respawn_placement_.reset();
    reset_tribute_donations();
    reset_tribute_bonuses();
    reset_guild_permissions(kTributeNone);
    release_eye_control();
    blood_spurts_.clear();
    world_drop_pending_ = false;
    clear_session_pending();
    if (chat_server_) chat_server_->disconnect();
    if (zone_endpoint_) zone_endpoint_->disconnect();
    if (world_endpoint_) world_endpoint_->disconnect();
    // Let the disconnects go out.
    for (int i = 0; i < 5; ++i) {
        if (zone_endpoint_) zone_endpoint_->update();
        if (world_endpoint_) world_endpoint_->update();
    }
    zone_endpoint_.reset();
    world_endpoint_.reset();
    discard_questions(); // nothing is left to answer them to
    if (state_ != ConnectionState::Disconnected) state_ = ConnectionState::Disconnected;
}

// ---------------------------------------------------------------- combat

void GameConnection::set_target(std::uint32_t spawn_id) {
    if (spawn_id == world_.target_id) return;
    world_.target_id = spawn_id;
    // CTargetWnd::OnProcessFrame on a new target: the selected group member is its slot (-1 when it is no member
    // or there is no group).
    if (spawn_id) {
        group_member_targeted_ = -1;
        if (group_.active)
            if (auto it = world_.spawns.find(spawn_id); it != world_.spawns.end()) {
                std::string n = it->second.name;
                while (!n.empty() && std::isdigit(static_cast<unsigned char>(n.back()))) n.pop_back();
                group_member_targeted_ = group_.slot_of(n);
            }
    }
    if (state_ != ConnectionState::InZone) return;
    send_zone(messages::outgoing(GameOpcode::TargetMouse, TargetSelection{spawn_id}));
    // Newbie guard (DoPassageOfTime @0x45dcb1): below level 11, switching to a
    // target more than 2 levels higher turns attacking and auto fire off.
    if ((auto_attack_ || auto_fire_) && spawn_id) {
        auto it = world_.spawns.find(spawn_id);
        if (it != world_.spawns.end() && pstatus_.level < 11 && static_cast<int>(it->second.level) - pstatus_.level > 2) {
            set_auto_attack(false);
            set_auto_fire(false);
        }
    }
}

void GameConnection::target_by_name(const std::string& name) {
    if (name.empty()) {
        chat(ChatEvent::Kind::System, "Usage: /target NAME");
        return;
    }
    // Nearest spawn whose name starts with the text (case-insensitive).
    auto lower = [](std::string v) {
        for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return v;
    };
    std::string want = lower(name);
    std::uint32_t best = 0;
    float best_d = 1e30f;
    for (const auto& [id, sp] : world_.spawns) {
        if (lower(game::display_name(sp.name)).rfind(want, 0) != 0 && lower(sp.name).rfind(want, 0) != 0) continue;
        float d = glm::distance(sp.position, position_);
        if (d < best_d) {
            best_d = d;
            best = id;
        }
    }
    if (best) set_target(best);
    else chat(ChatEvent::Kind::System, "There is no '" + name + "' here."); // the original's text is not known
}

void GameConnection::set_auto_attack(bool on) {
    auto_attack_ = on;
    if (!on) attack_held_for_xtarget_ = false;
    if (on == sent_auto_attack_ || state_ != ConnectionState::InZone) return;
    // DoPassageOfTime sends both whenever __Attack differs from the last sent value.
    send_zone(messages::outgoing(GameOpcode::AutoAttack, AttackToggle{on}));
    send_zone(messages::outgoing(GameOpcode::AutoAttack2, AttackToggle{on}));
    sent_auto_attack_ = on;
}

void GameConnection::set_auto_fire(bool on) {
    // SetAutoRangeAttack @0x520b40.
    const bool changed = auto_fire_ != on;
    auto_fire_ = on;
    if (attack_locked_) auto_attack_ = auto_fire_ = false; // 0xddf70c clears both flags (+0x83f92 as a word)
    if (changed) system(auto_fire_ ? 278 : 279, {}, 0xd); // Auto fire on. / Auto fire off.
    // DoPassageOfTime sends OP_AutoFire (and the attack pair) when the flags differ from the last sent copies.
    if (state_ != ConnectionState::InZone) return;
    if (auto_fire_ != sent_auto_fire_) {
        send_zone(messages::outgoing(GameOpcode::AutoFire, AttackToggle{auto_fire_}));
        sent_auto_fire_ = auto_fire_;
    }
    if (auto_attack_ != sent_auto_attack_) set_auto_attack(auto_attack_);
}

void GameConnection::autofire_command() {
    // cmd_autofire @0x4fb9b0.
    if (state_ != ConnectionState::InZone) return;
    if (!world_.target_id) {
        system(12697, {}, 0x12f); // You must first click on the being you wish to attack!
        return;
    }
    if (auto_fire_) {
        set_auto_fire(false);
        return;
    }
    set_auto_attack(false);
    set_auto_fire(true);
}

void GameConnection::play_animation(std::uint32_t spawn_id, AnimationType action) {
    play_animation(spawn_id, static_cast<std::uint8_t>(action));
}

void GameConnection::play_animation(std::uint32_t spawn_id, std::uint8_t action) {
    auto it = world_.spawns.find(spawn_id);
    if (it == world_.spawns.end()) return;
    it->second.action = action;
    it->second.action_speed = 10;
    ++it->second.action_seq;
}

bool GameConnection::allowed_to_attack(std::uint32_t target, std::int32_t spell) const {
    game::AttackCheckInput in;
    in.world = &world_;
    in.spells = spell_table_.get();
    in.target_id = target;
    in.spell = spell;
    in.my_level = static_cast<std::uint8_t>(pstatus_.level);
    in.zone = static_cast<std::uint32_t>(zone_id()) | (static_cast<std::uint32_t>(zone_instance()) << 16);
    in.rules.pvp_rules = pvp_rules_;
    in.self_or_group_member = [this](const std::string& name) { return group_.has_member(name); };
    return game::allowed_to_attack(in);
}

bool GameConnection::toggle_auto_attack() {
    if (state_ != ConnectionState::InZone || pstatus_.dead) return false;
    auto say = [&](std::uint32_t id) { chat(ChatEvent::Kind::System, strings_.format(id, {})); };
    // __ExecuteCmd case 10 (AUTOPRIM).
    if (pstatus_.stand_state == 100) {
        if (!auto_attack_ && world_.target_id == world_.player_id && world_.player_id) {
            say(12696); // Try attacking someone other than yourself...
            return false;
        }
    } else if (!auto_attack_) {
        say(pstatus_.stand_state == 102 ? 12368 : 12369); // You must be standing to attack!
        return false;
    }
    set_auto_attack(!auto_attack_);
    say(auto_attack_ ? 12370 : 12371); // Auto attack is on. / off.
    return auto_attack_ && face_on_attack_;
}

void GameConnection::attack_command(const std::string& args) {
    // Cmd_attack @0x4e8c70: "off" stops, "on" or a toggle while not attacking
    // starts (not on ourselves), a toggle while attacking stops. No message.
    if (state_ != ConnectionState::InZone || pstatus_.dead) return;
    // With auto fire on, both stop.
    if (auto_fire_) {
        set_auto_attack(false);
        set_auto_fire(false);
        return;
    }
    bool off = args == "off", on = args == "on";
    if (off || (!on && auto_attack_)) {
        set_auto_attack(false);
        return;
    }
    if (world_.target_id == world_.player_id && world_.player_id) {
        chat(ChatEvent::Kind::System, strings_.format(12696, {}));
        return;
    }
    set_auto_attack(true);
}

void GameConnection::consider() {
    if (state_ != ConnectionState::InZone) return;
    auto it = world_.spawns.find(world_.target_id);
    if (it == world_.spawns.end()) {
        chat(ChatEvent::Kind::System, strings_.format(12240, {})); // Consider who?
        return;
    }
    if (it->second.is_corpse) {
        chat(ChatEvent::Kind::System, strings_.format(12241, {})); // It is futile to consider the dead...
        return;
    }
    Consider c;
    c.player_id = world_.player_id;
    c.target_id = world_.target_id;
    send_zone(messages::outgoing(GameOpcode::Consider,c));
    context_tip(0x1d); // Cmd_consider: the consider tip
}

void GameConnection::report_hit(const game::HitLine& line, std::uint32_t source, bool report) {
    // ReportSuccessfulHit @0x52efd3: PetID, or bTempPet with MasterID equal to the player's SpawnID.
    const auto attacker = world_.spawns.find(source);
    const bool by_my_pet = source != 0 && (source == pets_.focused ||
        (attacker != world_.spawns.end() && attacker->second.temp_pet && world_.player_id != 0 &&
         attacker->second.pet_owner_id == world_.player_id));
    if (!game::hit_line_shown(line, by_my_pet, chat_filters_, report)) return;
    // ReportHit_* @0x4f7d70..0x4f8270: choose the display form but log the original full input independently.
    auto mode = hit_modes_[static_cast<std::size_t>(line.kind)];
    // their user colours: 265 your hits, 267 your misses, 266 / 268 you hit / missed, 279 / 280 others, 328 the pet's
    static constexpr std::uint32_t kColors[] = {265, 267, 266, 268, 279, 280};
    const bool others = line.kind == game::HitReport::OthersHits || line.kind == game::HitReport::OthersMisses;
    if (auto text = game::report_hit_line(strings_, line, mode))
        chat(ChatEvent::Kind::Combat, line.text, 0,
             others && by_my_pet ? 0x148u : kColors[static_cast<std::size_t>(line.kind)], {}, std::move(*text));
}

bool GameConnection::set_stand_state(std::uint32_t state, bool room_to_stand) {
    // PlayerZoneClient::ChangePosition while camping: the camp is abandoned (12290).
    if (state_ == ConnectionState::Camping && static_cast<std::uint8_t>(state) != pstatus_.stand_state) cancel_camp(12290);
    if (state_ != ConnectionState::InZone || pstatus_.dead || world_.player_id == 0) return false;
    auto want = static_cast<std::uint8_t>(state);
    std::uint8_t current = pstatus_.stand_state;
    if (current == want) return false;
    // PlayerZoneClient::ChangePosition: leaving sitting closes the spell book first (its OnShow would stand the
    // player up; the change below takes it away from sitting either way).
    if (spell_book_open_ && current == kStandSitting && want != kStandSitting) spell_book_closed_by_game(false);
    // Sitting while mounted (EQPlayer::GetMount) does nothing.
    if (want == kStandSitting && mount_id()) return false;
    // The controlled player's auto attack goes off (0xddf720), before the room check.
    if (auto_attack_) set_auto_attack(false);
    // The local player's poison application is cancelled (0xddf724: 12497) by any change.
    if (poison_started_ms_ != 0) {
        system(12497, {}, 0x111); // dsp_chat_2
        poison_started_ms_ = 0;
        poison_slot_ = 0;
    }
    // With 3D on: crouching to standing or sitting needs room (CanIFitHere, floor - z against the height).
    if (current == kStandCrouching && (want == kStandStanding || want == kStandSitting) && !room_to_stand) return false;
    pstatus_.stand_state = want;
    if (auto it = world_.spawns.find(world_.player_id); it != world_.spawns.end()) it->second.stand_state = want;
    SpawnAppearance a;
    a.spawn_id = static_cast<std::uint16_t>(world_.player_id);
    a.type = kAppearanceAnimation;
    a.parameter = want;
    send_zone(messages::outgoing(GameOpcode::SpawnAppearance,a));
    log::info("stand state {} -> {}: OP_SpawnAppearance {{{}, 14, {}}}", current, want, a.spawn_id, want);
    // Then the new state's animation: lying down ('s', 0x73) gets off the mount and ends the summon mount effect.
    if (want == 0x73 && mount_id()) {
        game::MountEvents ev;
        game::dismount(world_, world_.spawns.at(world_.player_id), ev);
        remove_mount_affect();
        mount_events(ev);
    }
    if (stops_motion(want)) {
        ++speed_stops_;
        ++turn_stops_;
    }
    motion_dirty_ = true; // a sit/stand change marks the position dirty
    return true;
}

void GameConnection::sit_toggle() {
    // Also while camping: CEverQuest::Sit stands a camping player up through ChangePosition, which abandons the
    // camp (set_stand_state: 12290). This is the sit key and moving forward with AllowAutoStand.
    if (state_ != ConnectionState::InZone && state_ != ConnectionState::Camping) return;
    // TotalEffect(31) (mesmerized): "You do not have control of yourself right now."
    for (const auto& b : spells_.buffs) {
        if (b.empty()) continue;
        if (const auto* s = spell_table_->get(b.spell); s && s->has_effect(31)) {
            chat(ChatEvent::Kind::System, strings_.format(12368, {}));
            return;
        }
    }
    std::uint8_t st = pstatus_.stand_state;
    if (st == kStandStanding || st == kStandCrouching) {
        if (st != kStandFreeze) {
            set_stand_state(kStandSitting);
            context_tip(0x16); // CEverQuest::Sit / Cmd_sit: the sitting tip
        }
    } else {
        set_stand_state(kStandStanding);
    }
}

void GameConnection::set_sitting(bool sit) { set_stand_state(sit ? kStandSitting : kStandStanding); }

void GameConnection::handle_combat(const messages::GameMessage& message) {
    const auto op = message.opcode;
    const std::uint32_t me = world_.player_id;
    auto spawn = [&](std::uint32_t id) -> game::Spawn* {
        auto it = world_.spawns.find(id);
        return it == world_.spawns.end() ? nullptr : &it->second;
    };
    switch (op) {
    case GameOpcode::MobHealth:
        if (auto h = messages::message_get<MobHealth>(message))
            if (auto* s = spawn(h->spawn_id)) { s->hp_percent = h->percent; s->hp_known = h->percent <= 100; }
        break;
    case GameOpcode::HPUpdate:
        if (auto h = messages::message_get<HpUpdate>(message)) {
            auto id = static_cast<std::uint32_t>(static_cast<std::uint16_t>(h->spawn_id));
            if (id == me || h->spawn_id == -1) {
                pstatus_.hp = static_cast<std::int32_t>(h->cur_hp);
                pstatus_.max_hp = h->max_hp;
                set_own_spawn_hp(); // the player's own: Cur_HP / Max_HP, the items' HP included (own_hp)
            } else if (auto* s = spawn(id); s && h->max_hp > 0) {
                s->hp_percent = spawn_hp_percent(std::int64_t{static_cast<std::int32_t>(h->cur_hp)}, h->max_hp);
                s->hp_known = true;
            }
        }
        break;
    case GameOpcode::ManaChange:
        if (auto m = messages::message_get<ManaChange>(message)) {
            pstatus_.mana = m->mana;
            pstatus_.endurance = m->endurance;
        }
        handle_spell(message); // ends the cast (game_connection_spells.cpp)
        break;
    case GameOpcode::ManaUpdate:
    case GameOpcode::EnduranceUpdate:
        if (auto u = messages::message_get<PointsUpdate>(message); u && (u->spawn_id == me ||
            (u->spawn_id == 0 && pstatus_.dead && (respawn_ || respawn_spawn_pending_)))) {
            if (op == GameOpcode::ManaUpdate) {
                pstatus_.mana = u->current;
                pstatus_.max_mana = u->maximum;
            } else {
                pstatus_.endurance = u->current;
                pstatus_.max_endurance = u->maximum;
            }
        }
        break;
    case GameOpcode::ExpUpdate:
        if (auto e = messages::message_get<ExpUpdate>(message)) {
            // Handle_OP_ExpUpdate @0x4b7060 sets only PcBase.Exp; AA experience is OP_AAExpUpdate's. With the
            // second dword 2 and experience above 0, the experience context tip (EQEmu always sends 0 there).
            pstatus_.exp = e->exp;
            if (e->tip == 2 && e->exp > 0) context_tip(9);
        }
        break;
    case GameOpcode::LevelUpdate:
        if (auto l = messages::message_get<LevelUpdate>(message)) {
            // Handle_OP_LevelUpdate: the lost-level lines and practice points, the level-up sound when
            // the new level is higher, and the experience (dword 2).
            handle_level_update(l->level);
            if (l->level == 10) context_tip(0x18); // Handle_OP_LevelUpdate: the level ten tip
            if (l->level > pstatus_.level && pstatus_.level != 0) sound_cues_.push_back({SoundCue::Kind::LevelUp});
            if (l->level != pstatus_.level) pstatus_.exp = l->exp;
            pstatus_.level = static_cast<std::uint8_t>(l->level);
            if (auto* s = spawn(me)) s->level = pstatus_.level;
        }
        break;
    case GameOpcode::Consider:
        if (auto c = messages::message_get<Consider>(message)) {
            const game::Spawn* t = spawn(c->target_id);
            if (!t) {
                chat(ChatEvent::Kind::System, strings_.format(12240, {}));
            } else if (t->is_corpse) {
                chat(ChatEvent::Kind::System, strings_.format(12241, {}));
            } else {
                // BuildConsiderMessage colours by the client's own con level.
                game::Con con = game::con_level(*t, pstatus_.level, pvp_rules_);
                chat(ChatEvent::Kind::System, game::consider_message(strings_, *t, pstatus_.level, c->faction, 0, pvp_rules_),
                     game::con_rgb(con));
            }
        }
        break;
    case GameOpcode::Damage:
        if (auto d = messages::message_get<CombatDamage>(message)) {
            // Handle_OP_Damage @0x4b5a40, for a hit on the player by another: damage above 0 closes the spell book
            // (any type) and, but for a spell, comes off the HP; a spell's negative damage heals (ModifyCurHP, then
            // Cur_HP caps it). A local prediction until the server's HP update.
            if (d->target == me && d->source != me) {
                if (d->damage > 0) {
                    if (spell_book_open_) spell_book_closed_by_game(true);
                    if (d->type != 231) pstatus_.hp -= d->damage;
                } else if (d->damage < 0 && d->type == 231) {
                    pstatus_.hp -= d->damage;
                    if (pstatus_.max_hp > 0 && pstatus_.hp > pstatus_.max_hp) pstatus_.hp = pstatus_.max_hp;
                }
                update_player_consciousness(true);
            }
            report_successful_hit(*d, hit_reported(d->target, d->source));
            // Hailstone (auto_target_xtarget): an NPC attacking the player, a pick due when there is no target.
            if (d->target == me && d->source != me && d->source != 0)
                if (auto* s = spawn(d->source); s && s->is_npc) {
                    if (std::find(xtarget_attackers_.begin(), xtarget_attackers_.end(), d->source) ==
                        xtarget_attackers_.end())
                        xtarget_attackers_.push_back(d->source);
                    if (world_.target_id == 0) {
                        auto_target_due_ = true;
                        auto_target_prefer_ = d->source;
                    }
                }
        }
        break;
    case GameOpcode::Death:
        if (auto d = messages::message_get<Death>(message)) handle_death(*d); // net/connection/game_connection_death.cpp
        break;
    case GameOpcode::SomeItemPacketMaybe: // HandleWorldMessage -> the EQMissile constructor (game/missiles.h)
        if (const auto* m = messages::message_get<game::MissilePacket>(message)) missiles_.push_back(*m);
        break;
    case GameOpcode::Animation:
        if (auto a = messages::message_get<Animation>(message))
            if (auto* s = spawn(a->spawn_id)) {
                s->action = a->action;
                s->action_speed = a->speed;
                ++s->action_seq;
            }
        break;
    case GameOpcode::SpawnAppearance:
        if (auto a = messages::message_get<SpawnAppearance>(message)) {
            std::uint32_t id = a->spawn_id ? a->spawn_id : me;
            if (a->type == kAppearancePet) {
                if (auto* s = spawn(id)) s->pet_owner_id = a->parameter; // MasterID (Handle_OP_SpawnAppearance case 0x19)
            }
            if (a->type == kAppearanceAnimation) {
                auto want = static_cast<std::uint8_t>(a->parameter);
                game::Spawn* s = spawn(id);
                // PlayerZoneClient::ChangePosition: sitting while mounted does nothing; lying down (0x73) gets off
                // the mount (and ends the local player's SPA 113).
                if (!(s && want == kStandSitting && s->mount_id && s->stand_state != want)) {
                    if (s && want == 0x73 && s->mount_id && s->stand_state != want) {
                        if (id == me) remove_mount_affect();
                        game::MountEvents ev;
                        game::dismount(world_, *s, ev);
                        mount_events(ev);
                    }
                    // ChangePosition's StopMoving: sitting, looting or lying down clears SpeedRun and the turn
                    // rate, so a spawn that feigns while it runs stops where it is instead of running on with its
                    // last update's speed while that update is under 12 s old (mechanics::kSpawnStaleMs).
                    const bool changed = id == me ? pstatus_.stand_state != want : s && s->stand_state != want;
                    if (changed && stops_motion(want)) {
                        if (s) {
                            s->speed_run = 0.0f;
                            s->speed_heading = 0.0f;
                        }
                        if (id == me) {
                            ++speed_stops_;
                            ++turn_stops_;
                        }
                    }
                    if (s) s->stand_state = want;
                    if (id == me) pstatus_.stand_state = want;
                }
            }
            // Social appearance types (EQEmu AppearanceType; Handle_OP_SpawnAppearance).
            if (auto* s = spawn(id)) {
                switch (a->type) {
                case 3: s->invis_rank = static_cast<std::int32_t>(a->parameter); break;
                case 5: s->light_type = static_cast<std::uint8_t>(a->parameter); break;
                case 18: s->linkdead = a->parameter != 0; break;
                case 19: // PlayerPhysics_SetFlyMode
                    s->fly_mode = static_cast<std::uint8_t>(a->parameter);
                    ++s->fly_mode_sets;
                    break;
                case 20: s->gm = a->parameter != 0; break;
                case 21:
                    s->anon = static_cast<std::uint8_t>(a->parameter);
                    // Handle_OP_SpawnAppearance 0x15 for us: Options/Anonymous takes the flags (and the save).
                    if (id == me) keep_anon_flags(s->anon);
                    break;
                case 22: {
                    // Case 0x16: the join and leave lines (game::guild_id_appearance_line), then our guild id.
                    auto line = game::guild_id_appearance_line(guild_, id == me, s->guild_id, a->parameter, s->name);
                    if (line.eqstr && profile_) system(line.eqstr, line.args, 0x111);
                    if (id == me && guild_.guild_id != a->parameter) {
                        reset_guild_tribute_bonuses(a->parameter);
                        reset_guild_permissions(a->parameter);
                        // A new guild's rank must arrive before it can authorize the staged permissions. Keep
                        // GuildStatus's native history for rank-change chat; without membership no action is allowed.
                        if (a->parameter && a->parameter != 0xffffffff) {
                            guild_.rank = 0;
                            s->guild_rank = 0;
                        }
                        // another guild's ranks, names, page and channel
                        guild_.url.clear();
                        guild_.channel.clear();
                        guild_.guild_id = a->parameter;
                    }
                    s->guild_id = a->parameter;
                    break;
                }
                case 23: {
                    // Case 0x17: "You are now the rank of %1." / "%1 is now the rank of %2." for ranks 1..8; ours
                    // sets GuildStatus (only then).
                    auto line = game::guild_rank_appearance_line(guild_, id == me, s->guild_id, s->guild_rank,
                                                                 a->parameter, s->name);
                    if (line.eqstr && profile_) system(line.eqstr, line.args, 0x111);
                    if (id == me && a->parameter - 1 < 8) {
                        guild_.status = a->parameter;
                        guild_.rank = a->parameter;
                    }
                    s->guild_rank = a->parameter;
                    break;
                }
                case 52: s->guild_show = a->parameter != 0; break; // bGuildShowAnim
                case 53: s->offline = a->parameter != 0; break;    // bOfflineMode
                case 24:
                    s->afk = a->parameter != 0;
                    // Handle_OP_SpawnAppearance 0x18: the spawn's AFK through vtable +0xd4; ours is the local flag
                    // the away reply and /afk read. The zone sends it for us only from its own auto AFK.
                    if (id == me) {
                        social_.afk = s->afk;
                        afk_manual_ = false;
                        afk_keepalive_.zone_afk_since_ms = s->afk ? now_ms() : -1;
                    }
                    break;
                // NPC tint (ActorClient_ApplyNpcTint, new-style races only;
                // the renderer checks the race) and show helm.
                case 39: s->appearance.npc_tint = a->parameter; break;
                case 43: s->appearance.show_helm = a->parameter != 0; break;
                default: break;
                }
            }
            // Case 38 (FreezeBeneficialBuffs, @0x4b53c1): the spawn's character's buff timers on hold (value != 0).
            if (id == me && a->type == 38) spells_.buff_timers_on_hold = a->parameter != 0;
            if (id == me && a->type == 3) invis_state_ = static_cast<std::int32_t>(a->parameter); // the spawn's +0x338
        }
        break;
    case GameOpcode::ZonePlayerToBind:
        if (const auto* dest = messages::message_get<BindZoneTransition>(message)) handle_zone_to_bind(*dest); // net/connection/game_connection_death.cpp
        break;
    case GameOpcode::RespawnWindow:
        // Handle_OP_RespawnWindow: CRespawnWnd filled and shown; Select waits for the delay.
        // A malformed packet must not discard a valid window or unlock a selection already sent. Once the server
        // acknowledged a same-zone respawn, only the new own spawn can complete it, not another window.
        if (!respawn_spawn_pending_)
            if (auto window = messages::message_get<RespawnWindow>(message)) {
                const bool chosen = respawn_ && respawn_chosen_;
                if (!respawn_) {
                    respawn_session_ = core::next_process_serial();
                    clear_hover_rez();
                }
                respawn_ = *window;
                // A refreshed server list must not apply local consent to a different or withdrawn destination.
                const auto* rez = respawn_resurrection_option();
                if (hover_rez_choice_ && (!rez || *hover_rez_choice_ != *rez)) clear_hover_rez();
                respawn_shown_ms_ = respawn_now_ms();
                respawn_chosen_ = chosen;
                log::info("hover respawn window: {} options, {} ms left, default {}", respawn_->options.size(),
                          respawn_->time_remaining_ms, respawn_->selected);
            }
        break;
    default:
        break;
    }
}

void GameConnection::request_zone_links() {
    if (!hailstone_script_ || zone_links_asked_ || !zone_endpoint_) return;
    zone_links_asked_ = true;
    send_zone(messages::outgoing(messages::GameOpcode::HsZoneLinksRequest));
}

} // namespace hailstone::net
