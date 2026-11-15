#include "mock_server/mock_eqemu.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "core/byte_io.h"
#include "mock_server/profile_builder.h"
#include "net/des.h"
#include "net/packet_io.h"

namespace hailstone::test {

using namespace hailstone::net;
using soe::Bytes;

namespace {

std::vector<std::byte> text_bytes(std::string_view s, bool nul = true) {
    auto sp = std::as_bytes(std::span(s.data(), s.size()));
    std::vector<std::byte> out(sp.begin(), sp.end());
    if (nul) out.push_back(std::byte{0});
    return out;
}

void cstr(ByteWriter& w, std::string_view s) { w.bytes(text_bytes(s)); }

// A login message's header (docs/protocol/login.md "Message format").
void login_header(ByteWriter& w, std::uint32_t sequence, std::uint8_t encrypt) {
    w.le<std::uint32_t>(sequence);
    w.le<std::uint8_t>(0);
    w.le<std::uint8_t>(encrypt);
    w.le<std::uint32_t>(0);
}

// A reply's common part: success u8, error id u32, text.
void reply_base(ByteWriter& w, bool ok, std::int32_t error) {
    w.le<std::uint8_t>(ok);
    w.le<std::int32_t>(error);
    cstr(w, "");
}

std::string string_of(std::span<const std::byte> data) {
    std::string s;
    for (std::byte b : data) {
        if (b == std::byte{0}) break;
        s.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
    }
    return s;
}

AppPacket empty(Op op) { return make_empty(op); }

} // namespace

// ---------------------------------------------------------------- builders

Spawn make_spawn(const std::string& name, std::uint32_t id, std::uint8_t npc, std::uint32_t race, float x, float y, float z,
                 float heading) {
    Spawn s;
    s.name = name;
    s.spawn_id = id;
    s.level = 10;
    s.melee_radius = 5.3f; // EQEmu sends size - 0.7
    s.npc = npc;
    s.gender = 0;
    s.show_helm = true;
    s.targetable = true;
    s.show_name = true;
    s.body_type = 1;
    s.hp_percent = 100;
    s.size = 6.0f;
    s.walk_speed = 0.7f;
    s.run_speed = 1.25f;
    s.race = race;
    s.class_ = npc ? 1 : 1;
    s.stand_state = 100;
    s.guild_id = 0xffffffffu;
    s.long_equipment = !npc || is_pc_model_race(race);
    s.motion.x = x;
    s.motion.y = y;
    s.motion.z = z;
    s.motion.heading = heading;
    return s;
}

Item make_item(std::uint32_t item_id, const std::string& name, std::int16_t slot, std::uint32_t stack) {
    Item it;
    char serial[17] = {};
    std::snprintf(serial, sizeof serial, "%016u", item_id);
    it.serial = serial;
    it.stack_count = stack;
    it.slot = ItemSlot::possessions(slot);
    it.instance_id = item_id;
    it.charges = 0xffffffffu;
    it.name = name;
    it.lore = name;
    it.id_file = "IT63";
    it.id = item_id;
    it.icon = 500;
    it.weight = 10;
    it.no_drop = 255;
    it.size = 1;
    it.equip_slots = 1u << 13;
    it.value = 100;
    it.classes = 0xffff;
    it.races = 0xffff;
    if (stack > 1) it.max_stack = 20; // stackable
    return it;
}

AppPacket MockEqServer::build_new_zone(const MockZone& zone, const std::string& character) {
    NewZone z;
    z.char_name = character;
    z.zone_short_name = zone.short_name;
    z.zone_long_name = zone.long_name;
    z.zone_type = 1;
    z.gravity = 0.4f;
    z.time_type = 2;
    z.sky = 1;
    z.safe_x = zone.start_x;
    z.safe_y = zone.start_y;
    z.safe_z = zone.start_z;
    z.max_z = 1000;
    z.underworld = -1000;
    z.minclip = 450;
    z.maxclip = 1800;
    z.zone_short_name2 = zone.short_name;
    z.zone_id = zone.id;
    z.zone_instance = 0;
    z.fog_density = 0.33f;
    return encode(z);
}

AppPacket MockEqServer::build_zone_points(const std::vector<ZonePoint>& points) {
    // rof2.cpp ENCODE(OP_SendZonepoints): count, 32-byte entries, one more entry the client ignores.
    ByteWriter w;
    w.le<std::uint32_t>(static_cast<std::uint32_t>(points.size()));
    for (const auto& p : points) {
        w.le(p.number);
        w.f32(p.y);
        w.f32(p.x);
        w.f32(p.z);
        w.f32(p.heading);
        w.le(p.zone_id);
        w.le(p.instance_id);
        w.le(p.unknown024);
        w.le(p.unknown028);
    }
    w.zeros(32);
    return make_packet(Op::SendZonepoints, std::move(w));
}

MockConfig MockConfig::standard() {
    MockConfig c;
    MockCharacter gennro;
    gennro.name = "Gennro";
    gennro.inventory = {make_item(1001, "Short Sword", 13), make_item(1002, "Rations", 23, 5)};
    c.characters.push_back(gennro);

    MockZone qeynos2;
    qeynos2.id = 2;
    qeynos2.spawns = {make_spawn("a_rat001", 101, 1, 54, -300.0f, -140.0f, 3.0f, 64.0f),
                      make_spawn("Guard_Bob", 102, 1, 1, -250.0f, -160.0f, 3.5f, 256.0f)};
    qeynos2.zone_points = {ZonePoint{.number = 1, .y = 10.0f, .x = 20.0f, .z = 3.0f, .heading = 128.0f, .zone_id = 1}};
    c.zones[2] = qeynos2;

    MockZone qeynos;
    qeynos.id = 1;
    qeynos.short_name = "qeynos";
    qeynos.long_name = "South Qeynos";
    qeynos.start_x = 20.0f;
    qeynos.start_y = 10.0f;
    qeynos.start_z = 3.0f;
    qeynos.start_heading = 128.0f;
    qeynos.spawns = {make_spawn("Merchant_Tom", 201, 1, 1, 40.0f, 30.0f, 3.0f)};
    c.zones[1] = qeynos;
    return c;
}

// ---------------------------------------------------------------- the server

MockEqServer::MockEqServer(MockConfig cfg)
    : login(SoeEndpoint::default_params()),
      world(SoeEndpoint::default_params()),
      zone(cfg.zone_params),
      ucs(SoeEndpoint::default_params(), 1),
      config(std::move(cfg)) {
    if (!config.characters.empty()) current_zone_ = config.characters.front().zone_id;
}

net::WorldTicket MockEqServer::ticket() const {
    WorldTicket t;
    t.account_id = config.account_id;
    t.session_key = config.session_key;
    t.world_host = "127.0.0.1";
    t.world_port = world.port();
    t.server_id = config.server_id;
    t.server_name = config.server_name;
    return t;
}

const MockCharacter* MockEqServer::character(const std::string& name) const {
    for (const auto& c : config.characters)
        if (c.name == name) return &c;
    return nullptr;
}

const MockZone& MockEqServer::current_zone_def() const { return config.zones.at(current_zone_); }

std::vector<Op> MockEqServer::ops(const std::vector<AppPacket>& log) {
    std::vector<Op> out;
    for (const auto& p : log) out.push_back(from_wire(p.opcode));
    return out;
}

int MockEqServer::count(const std::vector<AppPacket>& log, Op op) {
    return static_cast<int>(std::count_if(log.begin(), log.end(), [&](const AppPacket& p) { return is_op(p, op); }));
}

void MockEqServer::poll() {
    login.poll();
    while (auto p = login.take()) on_login_packet(*p);
    world.poll();
    while (auto p = world.take()) {
        world_log.push_back(*p);
        on_world_packet(*p);
    }
    zone.poll();
    while (auto p = zone.take()) {
        zone_log.push_back(*p);
        on_zone_packet(*p);
    }
    ucs.poll();
    while (auto p = ucs.take()) ucs_log.push_back(*p);
}

// ---------------------------------------------------------------- login server

void MockEqServer::send_login_reply(std::uint16_t opcode, std::vector<std::byte> data) {
    login.send_reliable(AppPacket(opcode, std::move(data)));
}

void MockEqServer::on_login_packet(const AppPacket& p) {
    std::uint32_t seq = login_codec::sequence_of(p.data);
    switch (p.opcode) {
    case login_op::SessionReady: {
        ++handshake_requests;
        if (!config.reply_to_handshake) break;
        // OP_ChatMessage: base reply and one more string; EQEmu hard-codes sequence 2.
        ByteWriter w;
        login_header(w, 2, 0);
        reply_base(w, true, login_error::NoError);
        cstr(w, "");
        send_login_reply(login_op::ChatMessage, w.take());
        break;
    }
    case login_op::Login: {
        // DES of user\0password\0token\0flag.
        ++login_requests;
        std::string user, password, token;
        bool token_mode = false, valid = false;
        if (p.data.size() > 10) {
            if (auto plain = des::zero_cbc_decrypt(std::span<const std::byte>(p.data).subspan(10))) {
                try {
                    ByteReader credentials(*plain);
                    user = credentials.cstring();
                    password = credentials.cstring();
                    token = credentials.cstring();
                    const auto flag = credentials.le<std::uint8_t>();
                    token_mode = flag == 1;
                    valid = flag <= 1;
                } catch (const std::out_of_range&) {
                    // A malformed credential envelope receives the same refusal as invalid credentials.
                }
            }
        }
        login_user = user;
        login_password = password;
        login_token = token;
        login_token_mode = token_mode;
        const bool ok = valid && (token_mode
            ? user.empty() && password.empty() && !config.login_token.empty() && token == config.login_token
            : token.empty() && user == config.account && password == config.password);
        if (!config.reply_to_login) break;
        // EQEmu's PlayerLoginReply, 58 bytes, encrypted into an 80-byte body.
        ByteWriter r;
        r.le<std::uint8_t>(ok);
        r.le<std::int32_t>(ok ? login_error::NoError : login_error::BadPassword);
        r.le<std::uint8_t>(0);
        r.le<std::uint16_t>(0);
        r.le<std::int32_t>(ok ? static_cast<std::int32_t>(config.account_id) : -1);
        r.fixed_string(ok ? config.session_key : std::string(), 11);
        r.le<std::int32_t>(0);
        r.le<std::uint8_t>(1);
        for (std::int32_t v : {99, -1, 0, 0, 99, -1, 0}) r.le<std::int32_t>(v);
        cstr(r, ok ? config.returned_login_token : std::string());
        cstr(r, "");
        auto encrypted = des::zero_cbc_encrypt(r.data());
        encrypted.resize(std::max<std::size_t>(80, encrypted.size()), std::byte{0});
        ByteWriter w;
        login_header(w, seq, 2);
        w.bytes(encrypted);
        send_login_reply(login_op::LoginAccepted, w.take());
        break;
    }
    case login_op::ServerListRequest: {
        ByteWriter w;
        login_header(w, seq, 0);
        reply_base(w, true, login_error::NoError);
        w.le<std::uint32_t>(1);
        cstr(w, "127.0.0.1");
        w.le<std::int32_t>(1);
        w.le<std::uint32_t>(config.server_id);
        cstr(w, config.server_name);
        cstr(w, "us");
        cstr(w, "en");
        w.le<std::int32_t>(config.server_status);
        w.le<std::int32_t>(1); // players
        send_login_reply(login_op::ServerListResponse, w.take());
        break;
    }
    case login_op::PlayEverquestRequest: {
        std::uint32_t id = 0;
        if (p.data.size() >= 14) {
            ByteReader r(p.data);
            r.seek(10);
            id = r.le<std::uint32_t>();
        }
        bool ok = config.play_error == 0 && id == config.server_id;
        ByteWriter w;
        login_header(w, seq, 0);
        reply_base(w, ok, ok ? login_error::NoError : (config.play_error ? config.play_error : 326));
        w.le<std::uint32_t>(id);
        send_login_reply(login_op::PlayEverquestResponse, w.take());
        break;
    }
    case login_op::KickPlayer: {
        ++kick_requests;
        if (!config.kick_reply) break;
        bool ok = *config.kick_reply == login_error::NoError;
        if (ok) config.play_error = 0;
        ByteWriter w;
        login_header(w, seq, 0);
        reply_base(w, ok, *config.kick_reply);
        send_login_reply(login_op::KickPlayerReply, w.take());
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------- world server

void MockEqServer::send_world_hello(bool zoning) {
    // world/client.cpp: OP_GuildsList (not when zoning), OP_LogServer, OP_ApproveWorld, OP_EnterWorld (the name
    // when zoning), OP_PostEnterWorld.
    if (!zoning) {
        ByteWriter g;
        g.zeros(64);
        g.le<std::uint32_t>(0);
        world.send_reliable(make_packet(Op::GuildsList, std::move(g)));
    }
    ByteWriter log;
    log.zeros(560);
    auto bytes = log.take();
    bytes[8] = std::byte{1};  // enable_FV
    for (int k = 0; k < 4; ++k) bytes[0x18 + k] = static_cast<std::byte>(config.log_server_spell_scribe >> (8 * k));
    const char name[] = "mock";
    for (std::size_t i = 0; i < sizeof name - 1; ++i) bytes[36 + i] = static_cast<std::byte>(name[i]);
    world.send_reliable(AppPacket(to_wire(Op::LogServer), std::move(bytes)));
    world.send_reliable(AppPacket(to_wire(Op::ApproveWorld), std::vector<std::byte>(544)));
    ByteWriter e;
    cstr(e, zoning ? zone_char_ : std::string());
    world.send_reliable(make_packet(Op::EnterWorld, std::move(e)));
    world.send_reliable(empty(Op::PostEnterWorld));
    if (!zoning) {
        ByteWriter x;
        x.zeros(64);
        x.le<std::uint32_t>(0xffffffffu); // expansion mask at +64
        x.zeros(0);
        world.send_reliable(make_packet(Op::ExpansionInfo, std::move(x)));
        ByteWriter m;
        m.le<std::uint32_t>(8); // max characters
        m.zeros(8);
        world.send_reliable(make_packet(Op::SendMaxCharacters, std::move(m)));
        // SendMembership / SendMembershipSettings: gold (2), as EQEmu always sends, with RoF2's 25 entries.
        Membership mem;
        mem.level = 2;
        mem.races = mem.classes = 0x1ffff;
        mem.entries.assign(25, -1);
        world.send_reliable(encode(mem));
        MembershipDetails details;
        for (std::uint32_t id = 0; id < 22; ++id)
            for (std::uint32_t level = 0; level < 3; ++level) details.settings.push_back({level, id, -1});
        details.races = details.classes = {{1, 0x1ffff}};
        world.send_reliable(encode(details));
        send_char_list();
    }
}

void MockEqServer::send_char_list() {
    std::vector<CharacterSelectEntry> list;
    for (const auto& c : config.characters) {
        CharacterSelectEntry e;
        e.name = c.name;
        e.class_ = c.class_;
        e.race = c.race;
        e.level = c.level;
        e.zone = c.zone_id;
        e.gender = c.gender;
        e.enabled = 1;
        e.deity = 396;
        list.push_back(e);
    }
    world.send_reliable(encode(list));
}

void MockEqServer::enter_world(const std::string& name) {
    entering_ = name;
    ByteWriter m;
    cstr(m, config.motd);
    world.send_reliable(make_packet(Op::MOTD, std::move(m)));
    if (config.send_chat_server) {
        // "host,port,server.Character,<type letter><8 hex key>"
        std::string info = "127.0.0.1," + std::to_string(ucs.port()) + ",mock." + name + ",U12345678";
        ByteWriter a, b;
        cstr(a, info);
        cstr(b, info);
        world.send_reliable(make_packet(Op::SetChatServer, std::move(a)));
        world.send_reliable(make_packet(Op::SetChatServer2, std::move(b)));
    }
    if (config.zone_unavailable) {
        ByteWriter u;
        u.fixed_string(config.zones.at(current_zone_).short_name, 16);
        u.zeros(8);
        world.send_reliable(make_packet(Op::ZoneUnavail, std::move(u)));
        return;
    }
    world.send_reliable(encode(ZoneServerInfo{"127.0.0.1", zone.port()}));
}

void MockEqServer::on_world_packet(const AppPacket& p) {
    switch (from_wire(p.opcode)) {
    case Op::SendLoginInfo: {
        auto info = decode_login_info(p);
        if (!info) break;
        login_infos.push_back(*info);
        // CheckAuth(account id, key): a mismatch gets the connection dropped.
        if (info->account_id != std::to_string(config.account_id) || info->session_key != config.session_key) {
            world.send_disconnect();
            break;
        }
        zoning_ = info->zoning;
        send_world_hello(info->zoning);
        break;
    }
    case Op::EnterWorld: {
        auto e = decode_enter_world(p);
        if (!e) break;
        const MockCharacter* c = character(e->name);
        if (!c) {
            world.send_disconnect();
            break;
        }
        if (!zoning_) current_zone_ = c->zone_id;
        zone_char_ = c->name;
        enter_world(c->name);
        break;
    }
    case Op::DeleteCharacter: {
        // The name is a NUL-terminated string.
        std::string name = string_of(p.data);
        std::erase_if(config.characters, [&](const MockCharacter& c) { return c.name == name; });
        send_char_list();
        break;
    }
    case Op::CharacterCreateRequest:
        world.send_reliable(encode(CharCreateData{}));
        break;
    case Op::WorldComplete:
        world.send_disconnect(); // eqs->Close()
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- zone server

void MockEqServer::send_zone_in(const MockCharacter& c, const MockZone& z) {
    // Handle_Connect_OP_ZoneEntry: OP_PlayerProfile, the player's own spawn as OP_ZoneEntry, every other spawn as
    // one OP_ZoneEntry each, OP_TimeOfDay, (tributes), OP_CharInventory, (OP_XTargetResponse), OP_Weather, OP_NewZone.
    ProfileSpec spec;
    spec.name = c.name;
    spec.race = c.race;
    spec.class_ = c.class_;
    spec.level = c.level;
    spec.gender = c.gender;
    spec.cur_hp = c.hp;
    spec.mana = c.mana;
    spec.endurance = c.endurance;
    spec.zone_id = z.id;
    spec.x = z.start_x;
    spec.y = z.start_y;
    spec.z = z.start_z;
    spec.heading = z.start_heading;
    zone.send_reliable(build_player_profile(spec));

    Spawn own = make_spawn(c.name, config.own_spawn_id, 0, c.race, z.start_x, z.start_y, z.start_z, z.start_heading);
    own.class_ = c.class_;
    own.level = c.level;
    zone.send_reliable(encode(own));
    for (const auto& s : z.spawns) zone.send_reliable(encode(s));
    zone.send_reliable(encode(TimeOfDay{12, 30, 5, 6, 3200}));
    zone.send_reliable(encode_char_inventory(c.inventory));
    zone.send_reliable(encode(Weather{0xff, 0, 0}));
    zone.send_reliable(build_new_zone(z, c.name));
}

void MockEqServer::on_zone_packet(const AppPacket& p) {
    switch (from_wire(p.opcode)) {
    case Op::ZoneEntry: {
        ++zone_entries;
        auto req = decode_zone_entry_request(p);
        const MockCharacter* c = req ? character(req->name) : nullptr;
        if (!c) {
            zone.send_disconnect();
            break;
        }
        zone_char_ = c->name;
        send_zone_in(*c, config.zones.at(current_zone_));
        break;
    }
    case Op::ReqClientSpawn: {
        // Handle_Connect_OP_ReqClientSpawn: doors, objects, zone points, OP_SendAAStats, OP_SendExpZonein,
        // OP_ClientReady, OP_WorldObjectsSent (the last four empty).
        const MockZone& z = config.zones.at(current_zone_);
        zone.send_reliable(build_zone_points(z.zone_points));
        zone.send_reliable(empty(Op::SendAAStats));
        zone.send_reliable(empty(Op::SendExpZonein));
        zone.send_reliable(empty(Op::ClientReady));
        zone.send_reliable(empty(Op::WorldObjectsSent));
        break;
    }
    case Op::WorldObjectsSent: {
        // The echo makes the server send the zone-in packets (SendZoneInPackets): the stand state here.
        zone.send_reliable(encode(SpawnAppearance{0, 14, 100}));
        break;
    }
    case Op::ClientReady: {
        // CompleteConnect finishes with an HP update.
        if (const MockCharacter* c = character(zone_char_))
            zone.send_reliable(encode(HpUpdate{static_cast<std::int16_t>(config.own_spawn_id), c->hp,
                                               static_cast<std::int32_t>(c->max_hp)}));
        break;
    }
    case Op::ClientUpdate: {
        if (auto u = decode_client_position_update(p)) {
            client_updates.push_back(*u);
            if (echo_client_updates_as) {
                SpawnPositionUpdate s;
                s.spawn_id = *echo_client_updates_as;
                s.motion = u->motion;
                zone.send_unreliable(encode(s));
            }
        }
        break;
    }
    case Op::ZoneChange: {
        auto z = decode_zone_change(p);
        if (!z || z->success != 0) break;
        std::int32_t answer = 1;
        if (!zone_change_answers.empty()) {
            answer = zone_change_answers.front();
            zone_change_answers.pop_front();
        }
        if (answer == 1 && config.zones.count(z->zone_id)) current_zone_ = z->zone_id;
        ZoneChange reply = *z;
        reply.success = answer;
        zone.send_reliable(encode(reply));
        break;
    }
    case Op::Camp:
        ++camp_requests;
        break;
    case Op::Logout:
        ++logouts;
        if (drop_on_logout) zone.send_disconnect(); // Handle_OP_Logout: Disconnect()
        break;
    case Op::DeleteSpawn:
        ++own_delete_spawns;
        zone.send_disconnect(); // Handle_OP_DeleteSpawn: Disconnect()
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- gameplay

void MockEqServer::chat(const std::string& sender, std::uint32_t channel, const std::string& text, const std::string& target) {
    ChannelMessage m;
    m.sender = sender;
    m.target = target;
    m.channel = channel;
    m.language_skill = 100; // what EQEmu sends for common
    m.message = text;
    zone.send_reliable(encode(m));
}

void MockEqServer::add_spawn(const Spawn& spawn) {
    AppPacket p = encode(spawn);
    p.opcode = to_wire(Op::NewSpawn);
    zone.send_reliable(p);
}

void MockEqServer::remove_spawn(std::uint32_t id) { zone.send_reliable(encode_delete_spawn(id)); }

void MockEqServer::move_spawn(std::uint32_t id, const Motion& motion) {
    SpawnPositionUpdate u;
    u.spawn_id = static_cast<std::uint16_t>(id);
    u.motion = motion;
    zone.send_unreliable(encode(u));
}

void MockEqServer::hp_update(std::int16_t spawn_id, std::uint32_t current, std::int32_t max) {
    zone.send_reliable(encode(HpUpdate{spawn_id, current, max}));
}

void MockEqServer::item_packet(std::uint32_t type, const Item& item) {
    ItemPacket ip;
    ip.type = type;
    ip.item = item;
    zone.send_reliable(encode_item_packet(false, ip));
}

void MockEqServer::request_zone_change(std::uint16_t zone_id, float x, float y, float z) {
    // OP_RequestClientZoneChange, 176 bytes: zone, instance, u32 -1, y, x, z, heading, type 0x0b, 144 bytes, u32.
    ByteWriter w;
    w.le<std::uint16_t>(zone_id);
    w.le<std::uint16_t>(0);
    w.le<std::uint32_t>(0xffffffffu);
    w.f32(y);
    w.f32(x);
    w.f32(z);
    w.f32(0.0f);
    w.le<std::uint32_t>(0x0b);
    w.zeros(144);
    w.le<std::uint32_t>(0);
    zone.send_reliable(make_packet(Op::RequestClientZoneChange, std::move(w)));
}

} // namespace hailstone::test
