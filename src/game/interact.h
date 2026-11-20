#pragma once

// What the client keeps about the player-to-player commands: duels, the
// inspect window, the bandolier and pending confirmations. The original keeps
// the duel in three globals (0xddf7e4 the player we challenged or accepted,
// 0xddf7e8 the player who challenged us, 0xddf7ec the duel is on), the inspect
// result in CInspectWnd and the bandolier in PcProfile +0x6d7c (20 × 0x140).
// GameConnection (net/connection/game_connection_player.cpp) drives them.

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hailstone::game {

struct DuelState {
    std::uint32_t challenged = 0; // 0xddf7e4: whom we challenged, or the challenger we accepted
    std::uint32_t challenger = 0; // 0xddf7e8: who challenged us
    bool active = false;          // 0xddf7ec: both accepted, the fight is on
    bool pending() const { return challenged != 0 || challenger != 0; }
};

// The original's answer to a duel request (Handle_OP_RequestDuel @0x4b67c0).
struct DuelRequestOutcome {
    enum class Kind : std::uint8_t {
        Ignore,     // nothing happens
        Accepted,   // our challenge was accepted: eqstr 1424, the duel is on
        Challenged, // a new challenge: eqstr 1426 and the duel tip
        Refuse,     // answer with opcode 0x52f7 and `refusal_kind`
    };
    Kind kind = Kind::Ignore;
    std::uint8_t refusal_kind = 0; // 0 ignored, 1 already dueling, 2 considering another duel
};
// Applies an incoming challenge from `challenger` to the state (ignored: the
// challenger is on our ignore list).
DuelRequestOutcome duel_request(DuelState& s, std::uint32_t challenger, bool ignored);

// What /duel does with the current target (Cmd_duel @0x4f6930).
struct DuelCommandOutcome {
    enum class Kind : std::uint8_t {
        Message,   // print `eqstr` only
        Accept,    // eqstr 13252, send OP_RequestDuel and OP_DuelAccept
        Challenge, // eqstr 13254, send OP_RequestDuel
    };
    Kind kind = Kind::Message;
    std::uint32_t eqstr = 0;
};
// target: the target's spawn id (0 = none), target_is_pc: a player (not an
// NPC or corpse), target_is_me.
DuelCommandOutcome duel_command(DuelState& s, std::uint32_t target, bool target_is_pc, bool target_is_me);

// The inspect window (CInspectWnd) after OP_InspectAnswer.
struct InspectView {
    std::uint32_t player_id = 0;
    std::string player_name;
    std::array<std::string, 23> item_names;    // by worn slot 0..22; empty = nothing
    std::array<std::uint32_t, 23> item_icons{}; // 0xffffffff = nothing
    std::string text;                          // the player's inspect message
    bool own = false;                          // inspecting ourselves: the message is editable
};

// One bandolier set (PcBase_GetBandolierSet @0x7c4720) and the potion belt.
struct BeltItem {
    std::uint32_t id = 0, icon = 0;
    std::string name;
};
struct BandolierSet {
    std::string name;               // empty = unused
    std::array<BeltItem, 4> items;  // primary (13), secondary (14), range (11), ammo (22)
};
inline constexpr int kBandolierSets = 20;
inline constexpr std::array<std::int16_t, 4> kBandolierSlots{13, 14, 11, 22};
// The sets the player may use: 4 + SPA 363 (Bandolier Slots), at most 20
// (PcZoneClient::BandolierSwap @0x585aa0).
inline int bandolier_set_count(std::int32_t spa363) { return std::min(4 + spa363, kBandolierSets); }

struct Spawn;
// Hailstone (a deliberate difference, switchable: Options > Interface "Player Right-Click Menu"): a mouse right click
// on another player opens a menu of Send Tell, Invite to Group, Invite to Raid, Trade and Inspect. The original has no
// such menu: CEverQuest::RightClickedOnPlayer @0x536d80 inspects the player (with /toggleinspect on). Only a plain
// player has it; a trader or a buyer still opens its shop or barter window, and the player's own spawn, NPCs and
// corpses keep the original's right click.
bool opens_player_menu(const Spawn& sp, bool self);

} // namespace hailstone::game
