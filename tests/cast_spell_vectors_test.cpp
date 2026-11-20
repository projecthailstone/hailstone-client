// CharacterZoneClient::CastSpell (game/mechanics/cast_spell.h) against the
// original's vectors (recorded from eqgame.exe): CastSpell_Checks (1000), _Buffs
// (600), _Items (1000), _Target (1000), _Range (1200), _BodyType (1200) and
// _Send (800).
//
// Each record holds the inputs, every mocked callee and fake virtual the
// original called (in order, with its arguments and answer), the caster fields
// and globals it wrote and the OP_CastSpell payload. Two answers of the
// recording's world are not in the record: CDisplay::PlayerDistance's float
// and whether the ChangePosition fake took effect (it sets the stand state).
// So the inputs are drawn again below from the same random stream (one per
// file, seeds 0xca51..0xca57), and the regenerated inputs are first compared
// with the record's "in". The port then runs
// against a replay: each callee it calls must be the record's next call with
// the same arguments, and gets the recorded answer. Finally the return value,
// pinstTarget, the caster fields written (sentinels, as when recorded), the
// globals and the payload are compared, and the payload net::encode builds
// for the same fields (OP_CastSpell) must give the same bytes.
#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/mechanics/cast_spell.h"
#include "net/protocol/rof2/packets/spell_packets.h"
#include "vector_files.h"

using namespace hailstone::game::mechanics;
using hailstone::test::Json;
namespace cs = hailstone::game::mechanics::cast;

namespace {

// ---------------------------------------------------------------- regenerating a record's inputs
//
// The records were recorded over inputs drawn from one seeded xorshift32 stream per file. Only the record's
// "in" is written down, so the draws below must come in the same order and with the same bounds as when the
// vectors were recorded: one draw more or less and every later record differs. Several drawn values are
// answers of mocks the replay now takes from the record; they are still drawn (and dropped) to keep the stream
// in step.

class Stream {
public:
    explicit Stream(uint32_t seed) : s_(seed == 0 ? 0x9e3779b9u : seed) {}

    uint32_t bits() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_;
    }
    // lo..hi inclusive, by the remainder over the span (the span is taken unsigned, as when recorded).
    int32_t in(int32_t lo, int32_t hi) {
        const uint32_t span = static_cast<uint32_t>(hi) - static_cast<uint32_t>(lo) + 1u;
        return static_cast<int32_t>(static_cast<uint32_t>(lo) + bits() % span);
    }
    bool odds(int32_t percent) { return static_cast<int32_t>(bits() % 100) < percent; }
    template <typename T, std::size_t N> T one_of(const T (&choices)[N]) {
        return choices[static_cast<std::size_t>(in(0, static_cast<int32_t>(N) - 1))];
    }
    // A quarter of the time an edge value, a quarter anything at all, else a small number.
    int32_t awkward() {
        static constexpr int32_t kEdge[] = {0, 1, -1, 2, 9, 10, 11, 59, 60, 61, 99, 100, 255, 256, 330, 1000,
                                            0x7fffffff, static_cast<int32_t>(0x80000000u), 10000000, 9999999};
        switch (bits() % 4) {
        case 0: return kEdge[bits() % std::size(kEdge)];
        case 3: return static_cast<int32_t>(bits());
        default: return in(-5, 400);
        }
    }

private:
    uint32_t s_;
};

// Which part of CastSpell a file's records exercise: draws that can fail a check in that part fail often,
// the others almost never.
enum Focus : uint32_t {
    kChecks = 1 << 0,
    kSanctuary = 1 << 1,
    kItems = 1 << 2,
    kTarget = 1 << 3,
    kRange = 1 << 4,
    kBody = 1 << 5,
    kSend = 1 << 6,
};
constexpr int kBuffs = 8;

uint32_t bits_of(float f) { return std::bit_cast<uint32_t>(f); }
float float_of(uint32_t bits) { return std::bit_cast<float>(bits); }

// One of the other spawns (target, pet, owner): the fields the generator gives it.
struct OtherSpawn {
    uint8_t type = 0;
    uint32_t id = 0;
    uint8_t mercenary = 0, summoned = 0;
    uint32_t master = 0;
    bool has_character = false;
    uint32_t z = 0, height = 0; // float bits
};

enum class FirstTarget { None, Caster, Target };

// The inputs of one record (float fields are bits).
struct Inputs {
    // CastSpell's arguments.
    struct {
        uint8_t slot = 0;
        int32_t spell_id = 0;
        bool item = false;
        std::array<uint32_t, 3> item_location{};
        int32_t item_cast_type = 0;
        uint8_t send_packet = 0;
        bool positioned = false;
        std::array<float, 3> position{};
    } call;
    // The caster's spawn (offsets in PlayerClient).
    struct {
        bool exists = false, is_local_player = false;
        uint8_t type = 0;            // +0x125
        uint32_t id = 0;             // +0x148
        uint32_t master = 0;         // +0x38c
        uint8_t mercenary = 0;       // +0x208
        uint8_t stand = 0;           // +0x35c
        bool stand_change_sticks = false;
        int32_t hide = 0;            // +0x338
        int32_t casting = -1;        // +0x448 (-1: not casting)
        int32_t hp_max = 0;          // +0x2dc
        uint32_t floor = 0, ceiling = 0, height = 0; // +0x28, +0x11b4, +0x138
        uint32_t x = 0, y = 0, z = 0;                // +0x68, +0x64, +0x6c
    } caster;
    // The CharacterZoneClient and its profile.
    struct {
        uint8_t effects_cached = 0;  // +0x1a4
        uint8_t stunned = 0;         // +0xfc
        uint8_t state = 0;           // +0x104
        uint8_t skip_cost = 0;       // +0x460
        int32_t level = 0;           // virtual +0x68
        uint8_t class_id = 0;        // the current profile's +0x3374
        bool is_local_pc = false;    // pinstLocalPC + 0x2dc8 == this
    } character;
    // The spell (EQ_Spell offsets).
    struct {
        int32_t id = 0;                         // +0x174
        uint32_t range = 0, min_range = 0;      // +0x0, +0x22c
        uint32_t cast_time = 0;                 // +0x10
        int32_t mana = 0, endurance = 0;        // +0x28, +0x194
        int32_t hp_cost = 0;                    // +0x2c
        int32_t group = 0, subgroup = 0, ranks = 0; // +0x1d4, +0x1d8, +0x1dc
        uint8_t in_combat = 0, out_of_combat = 0, fast_regen_only = 0, not_standing = 0, is_skill = 0;
        uint8_t kind = 0, target_type = 0, travel = 0, sneak = 0; // +0x26b, +0x26e, +0x277, +0x4dc
        int32_t no_self = 0;                    // +0x178
        int32_t pcnpc_only = 0;                 // +0x1cc
        int32_t association = 0;                // +0x1e8
        uint8_t class_level = 0;                // +0x247.. (every class)
        std::array<int32_t, 12> spa{};          // +0x11c
    } spell;
    FirstTarget first_target = FirstTarget::None;
    OtherSpawn target, pet, owner;
    // Globals.
    struct {
        uint8_t local_block = 0; // byte 0xddf70c
        uint8_t auto_stand = 0;  // byte 0xde0d5b
        uint8_t spa25_block = 0; // byte 0xddfc69
        int32_t zone_type = 0;   // 0xdcee70
        int32_t free_target = 0; // CEverQuest +0x5c0
        uint8_t quiet = 0;       // byte 0xddf654
        int32_t demo = 0;        // 0xddf790
        bool connected = false;
        uint32_t now = 0;        // CDisplay +0x154
    } world;
    // The fakes' answers the record does not hold.
    int32_t max_effects = 0;
    std::array<uint8_t, kBuffs> buff_active{};
    std::array<int32_t, kBuffs> buff_kind{}; // 0 no spell, 1 has 312, 2 ends (0xfe) before 312, 3 other
    int32_t attuned = 0, charges = 0;
    uint32_t distance = 0; // CDisplay::PlayerDistance
};

// Target types, in the order the draws index them.
constexpr uint8_t kPlainTargetTypes[] = {2, 3, 4, 19, 24, 25, 26, 29, 30, 31, 32, 33, 36, 37, 40, 41, 42, 44, 46};
constexpr uint8_t kRangedTargetTypes[] = {0, 1, 8, 21, 22, 23, 28, 45, 5, 9, 10, 11, 12, 13, 15, 16, 17, 18, 20,
                                          27, 34, 35, 39, 43, 47, 14, 38};
constexpr uint8_t kBodyTargetTypes[] = {9, 23, 10, 21, 11, 22, 12, 13, 20, 14, 15, 16, 17, 18, 19, 27, 28, 34,
                                        35, 38, 39, 43, 6, 6, 6, 43, 7, 5, 47};
constexpr uint8_t kOtherTargetTypes[] = {6, 0x0e, 0x26, 0x2d, 0x2f, 5, 0x30, 0xff, 1, 7};

class Generator {
public:
    Generator(Stream& s, uint32_t focus) : s_(s), focus_(focus) {}

    Inputs next() {
        Inputs in;
        target_type(in);
        call_args(in);
        caster(in);
        character(in);
        spell(in);
        effects(in);
        others(in);
        globals(in);
        answers(in);
        ranges(in);
        return in;
    }

private:
    // A draw that fails a check: often when the file is about that check, else one time in a hundred.
    bool fails(uint32_t focus, int32_t percent) { return s_.odds((focus_ & focus) != 0 ? percent : 1); }
    uint8_t flag(int32_t percent) { return s_.odds(percent) ? 1 : 0; }
    // percent of the time lo..hi, else other_lo..other_hi.
    int32_t in_or(int32_t percent, int32_t lo, int32_t hi, int32_t other_lo, int32_t other_hi) {
        return s_.odds(percent) ? s_.in(lo, hi) : s_.in(other_lo, other_hi);
    }
    uint32_t spawn_id() { return static_cast<uint32_t>(s_.in(1, 0xffff)); }
    // A whole number of halves 0.5..20 (a model height).
    uint32_t height() { return bits_of(static_cast<float>(s_.in(1, 40)) * 0.5f); }
    uint8_t flag_unless(int32_t percent) { return s_.odds(percent) ? 0 : 1; }
    // Mostly 0; else usually lo..hi, sometimes an awkward value.
    int32_t modifier(int32_t zero_percent, int32_t lo, int32_t hi) {
        if (s_.odds(zero_percent)) return 0;
        return s_.odds(85) ? s_.in(lo, hi) : s_.awkward();
    }
    // A coordinate in eighths.
    float eighths(int32_t limit) { return static_cast<float>(s_.in(-limit, limit)) * 0.125f; }
    // A float near `v`: itself (twice as likely), one ulp either way, or half a unit off.
    uint32_t near(float v) {
        const uint32_t b = bits_of(v);
        const int32_t way = s_.in(0, 5);
        if (way <= 1) return b;
        if (way == 2) return b + 1;
        if (way == 3) return b == 0 ? b : b - 1;
        return bits_of(way == 4 ? v + 0.5f : v - 0.5f);
    }
    uint32_t any_float() {
        static constexpr uint32_t kSpecial[] = {0, 0x80000000u, 0x7fc00000u, 0x7f800000u, 0xff800000u,
                                                0x3f800000u, 0xbf800000u, 0x00000001u, 0x7f7fffffu};
        if (s_.odds(40)) return s_.one_of(kSpecial);
        return bits_of(static_cast<float>(s_.in(-2000, 2000)) * 0.25f);
    }

    // The target type comes first: it decides which checks the record reaches.
    void target_type(Inputs& in) {
        uint8_t& t = in.spell.target_type;
        if ((focus_ & kRange) != 0) t = s_.one_of(kRangedTargetTypes);
        else if ((focus_ & kBody) != 0) t = s_.one_of(kBodyTargetTypes);
        else t = s_.odds(75) ? s_.one_of(kPlainTargetTypes) : s_.one_of(kOtherTargetTypes);
        if ((focus_ & kChecks) != 0 && s_.odds(8)) t = 0x2d;
    }

    void call_args(Inputs& in) {
        auto& a = in.call;
        if (s_.odds(80)) a.slot = static_cast<uint8_t>(s_.in(0, 0xb));
        else a.slot = static_cast<uint8_t>(s_.in(0xc, 0x10));
        if (s_.odds(3)) a.slot = 0xff;
        if (s_.odds(90)) a.spell_id = s_.in(1, 9000);
        else a.spell_id = s_.odds(50) ? 0x5de : 0xa9c;
        const int32_t item_odds = (focus_ & kItems) != 0 ? 70 : (focus_ & kSend) != 0 ? 40 : 12;
        a.item = s_.odds(item_odds);
        a.item_cast_type = s_.odds(75) ? 0 : s_.in(1, 12);
        a.send_packet = flag(85);
        a.positioned = s_.odds(in.spell.target_type == 0x2d ? 85 : 30);
        a.position[0] = eighths(4000);
        a.position[1] = eighths(4000);
        a.position[2] = eighths(800);
        a.item_location[0] = static_cast<uint32_t>(s_.in(0, 2));
        a.item_location[1] = static_cast<uint32_t>(s_.in(-1, 33));
        const int32_t index = s_.odds(50) ? -1 : s_.in(0, 9);
        const uint32_t high = s_.odds(30) ? 0xffff0000u : 0;
        a.item_location[2] = static_cast<uint32_t>(index) | high;
    }

    void caster(Inputs& in) {
        auto& c = in.caster;
        c.exists = !fails(kChecks, 3);
        c.is_local_player = s_.odds(75);
        if (fails(kChecks, 10)) in.world.local_block = s_.odds(80) ? 1 : 2;
        if (!s_.odds(80)) c.type = s_.odds(70) ? 1 : static_cast<uint8_t>(s_.in(2, 3));
        if (!s_.odds(70)) c.master = static_cast<uint32_t>(s_.in(1, 5000));
        c.mercenary = flag_unless(85);
        static constexpr uint8_t kNotStanding[] = {0x6e, 0x66, 0x6f, 0x73, 0x78, 0x65, 0};
        c.stand = fails(kChecks, 22) ? s_.one_of(kNotStanding) : uint8_t{0x64};
        c.stand_change_sticks = s_.odds(50);
        in.world.auto_stand = flag(50);
        if (!s_.odds(70)) c.hide = s_.odds(80) ? s_.in(1, 3) : -1;
        if (fails(kItems, 30)) c.casting = in.call.spell_id + 1000;
        c.hp_max = in_or(80, 1, 30000, -50, 0);
        c.id = spawn_id();
        const int32_t floor_at = s_.in(-200, 200);
        c.floor = bits_of(static_cast<float>(floor_at));
        c.height = height();
        const float floor = float_of(c.floor);
        const float room = float_of(c.height) * 3.0f;
        if (s_.odds(60)) c.ceiling = near(floor + room);
        else c.ceiling = bits_of(floor + static_cast<float>(s_.in(0, 100)));
        if (s_.odds(5)) c.ceiling = 0x7fc00000u;
        c.x = bits_of(eighths(4000));
        c.y = bits_of(eighths(4000));
        c.z = bits_of(eighths(800));
        s_.odds(80); // the caster's vtable +0xdc
    }

    void character(Inputs& in) {
        auto& ch = in.character;
        ch.effects_cached = flag(50);
        if (fails(kChecks, 10)) ch.stunned = 1;
        else ch.stunned = s_.odds(10) ? 2 : 0;
        static constexpr uint8_t kNotStanding[] = {0x73, 0x78, 0x6e, 0};
        ch.state = fails(kTarget, 15) ? s_.one_of(kNotStanding) : uint8_t{0x64};
        ch.skip_cost = flag_unless(85);
        s_.odds(70);                // virtual +0x90
        if (!s_.odds(30)) s_.in(0, 4); // virtual +0x0 (the rank)
        if (!s_.odds(50)) s_.in(1, 3); // virtual +0x34
        ch.level = s_.in(1, 100);
        fails(kChecks, 10);         // virtual +0x4c (the PcZoneClient)
        ch.class_id = s_.odds(20) ? 8 : static_cast<uint8_t>(s_.in(1, 16));
        if (s_.odds(2)) ch.class_id = s_.odds(50) ? 0 : 40;
        ch.is_local_pc = s_.odds(85);
    }

    void spell(Inputs& in) {
        auto& sp = in.spell;
        fails(kChecks, 4); // IsValidSpellIndex
        fails(kChecks, 4); // GetSpellByID found it
        sp.id = s_.odds(90) ? in.call.spell_id : s_.in(1, 9000);
        if (s_.odds(3)) sp.id = s_.odds(50) ? 0x3d6 : 0xad4;
        if ((focus_ & kRange) != 0 && s_.odds(10)) sp.id = 0x3d6;  // the NPC range of 10000
        if ((focus_ & kItems) != 0 && s_.odds(10)) sp.id = 0xad4;  // exempt from the SPA 32 cursor check
        sp.cast_time = static_cast<uint32_t>(in_or(70, 0, 8000, 0, 200));
        sp.mana = s_.in(0, 800);
        if (!s_.odds(70)) sp.endurance = s_.in(1, 500);
        if (s_.odds(80)) sp.hp_cost = s_.in(-100, 100);
        else sp.hp_cost = s_.awkward();
        if (s_.odds(30)) sp.hp_cost = s_.in(-1, 1) - in.caster.hp_max; // costing about all the caster's hit points
        if (!s_.odds(50)) sp.group = s_.odds(85) ? s_.in(1, 5000) : -1;
        sp.subgroup = s_.in(0, 20);
        sp.ranks = s_.in(0, 4);
        s_.odds(85); // the rank's spell found
        sp.in_combat = fails(kChecks, 20) ? 0 : 1;
        sp.out_of_combat = fails(kChecks, 20) ? 0 : 1;
        sp.fast_regen_only = fails(kChecks, 15) ? 1 : 0;
        sp.not_standing = flag(15);
        sp.is_skill = flag(30);
        if (s_.odds(50)) sp.kind = 1;
        else sp.kind = s_.odds(80) ? 0 : 2;
        sp.travel = s_.odds(70) ? 3 : static_cast<uint8_t>(s_.in(0, 4));
        sp.sneak = fails(kSanctuary, 25) ? 1 : 0;
        sp.no_self = fails(kBody, 15) ? 1 : 0;
        sp.pcnpc_only = fails(kTarget, 30) ? s_.in(1, 3) : 0;
        sp.association = s_.in(0, 50);
        const int32_t level = in.character.level;
        if (fails(kItems, 25)) sp.class_level = static_cast<uint8_t>(level + s_.in(1, 20));
        else sp.class_level = static_cast<uint8_t>(s_.in(0, level));
        if (s_.odds(2)) sp.class_level = s_.odds(50) ? 255 : 0;
    }

    // The effect slots: filler, then the SPAs CastSpell tests (25, 27, 32, 91, 113, 152), each mostly in the
    // files about it.
    void effects(Inputs& in) {
        static constexpr int32_t kFiller[] = {0, 1, 10, 3, 79, 40, 312, 254};
        for (int32_t& spa : in.spell.spa) spa = s_.odds(50) ? 254 : s_.one_of(kFiller);
        struct Planted {
            uint32_t focus;
            int32_t percent;
            int32_t spa;
        };
        static constexpr Planted kPlanted[] = {
            {kItems, 25, 25}, {kItems, 25, 32}, {kTarget | kRange, 40, 27},
            {kBody, 50, 91},  {kTarget, 25, 113}, {kTarget, 20, 152},
        };
        for (const Planted& p : kPlanted) {
            if (!fails(p.focus, p.percent)) continue;
            const auto slot = static_cast<std::size_t>(s_.in(0, 11));
            in.spell.spa[slot] = p.spa;
        }
    }

    void others(Inputs& in) {
        if (s_.odds(10)) in.first_target = FirstTarget::None;
        else in.first_target = s_.odds(20) ? FirstTarget::Caster : FirstTarget::Target;
        OtherSpawn& t = in.target;
        if (!s_.odds(45)) t.type = s_.odds(80) ? 1 : static_cast<uint8_t>(s_.in(2, 3));
        t.mercenary = flag_unless(80);
        t.summoned = flag(50);
        if (!s_.odds(60)) t.master = static_cast<uint32_t>(s_.in(1, 5000));
        t.has_character = !s_.odds(30);
        t.id = spawn_id();
        const float rise = static_cast<float>(s_.in(-60, 60));
        t.z = bits_of(float_of(in.caster.z) + rise);
        t.height = height();
        if (s_.odds(40)) { // around the 39.99 height difference
            const float diff = s_.odds(50) ? 39.99f : -39.99f;
            t.z = near(float_of(in.caster.z) + diff + float_of(t.height) - float_of(in.caster.height));
        }
        s_.odds(70); // the pet found
        in.pet.type = flag(80);
        in.pet.summoned = flag(70);
        in.pet.id = spawn_id();
        s_.odds(70); // the owner found
        in.owner.type = flag_unless(70);
        in.owner.id = spawn_id();
    }

    void globals(Inputs& in) {
        auto& g = in.world;
        g.spa25_block = fails(kItems, 15) ? 1 : 0;
        g.zone_type = s_.odds(30) ? 4 : s_.in(0, 5);
        g.free_target = s_.odds(70) ? 0 : 1;
        g.quiet = fails(kSend, 15) ? 1 : 0;
        g.demo = fails(kSend, 10) ? 1 : 0;
        g.connected = !fails(kSend, 15);
        g.now = s_.bits();
    }

    // The mocks' answers. The replay answers from the record, so most are only drawn; the buffs, the item's
    // charges and attunement and the distance stay, as fakes or values the record does not hold.
    void answers(Inputs& in) {
        if (fails(kChecks, 10)) s_.odds(60); // IsOkToTransact, twice
        const uint32_t per_cache[] = {kChecks, kSanctuary, kTarget};
        for (uint32_t focus : per_cache) {
            if (fails(focus, 12) && s_.odds(80)) s_.in(1, 3); // the buff cache
            if (fails(focus, 8)) s_.odds(80);                 // the AA cache
            if (fails(focus, 8)) s_.odds(80);                 // the item cache
        }
        s_.odds(40); // in combat
        s_.odds(60); // resting
        if (!s_.odds(50) && s_.odds(85)) s_.in(1, 600); // the item's recast time
        s_.in(1000, 100000);                             // FastTime
        if (fails(kChecks, 30)) s_.in(1, 500);           // the item's recast timer: after FastTime
        else s_.in(0, 500);                              // or before it
        in.max_effects = s_.in(0, kBuffs);
        for (std::size_t k = 0; k < kBuffs; ++k) {
            in.buff_active[k] = flag(70);
            in.buff_kind[k] = s_.in(0, 3);
        }
        if (!fails(kItems, 8) && !s_.odds(20)) s_.in(0, 30); // the item type
        for (int32_t percent : {80, 80, 70, 30}) s_.odds(percent); // casting spell valid / found, bard casting / main
        if (fails(kItems, 15)) s_.in(1, 10); // the item's required level: above the level
        else s_.in(0, 10);                   // or at most it
        s_.odds(30); // attuneable
        in.attuned = fails(kItems, 30) ? 0 : 1;
        if (fails(kItems, 15)) in.charges = 0;
        else in.charges = s_.odds(85) ? s_.in(1, 10) : -1;
        if (!s_.odds(30) && !s_.odds(40)) s_.in(0, 7); // the effect type
        if (s_.odds(70)) s_.in(0, 0x16);              // the worn slot
        else s_.odds(50);
        fails(kItems, 20); // CanUseItem
        fails(kItems, 20); // an item on the cursor
        fails(kBody, 30);  // an item in slot 21
        fails(kTarget, 25); // CanSee(position)
        fails(kTarget, 25); // CanSee
        fails(kTarget, 40); // not on the same deity
        for (int k = 0; k < 2; ++k) { // the current mana, then endurance: below the cost or not
            if (fails(kTarget, 20)) s_.in(1, 50);
            else s_.in(0, 50);
        }
        focus_range_ = modifier(60, -50, 300); // the focus's range modifier, which ranges() aims with
        s_.odds(80);       // the target has a PcClient
        fails(kBody, 40);  // the target type 12 check
        s_.odds(60);       // the caster in the group
        fails(kBody, 40);  // the target in the group
        s_.odds(50);       // the owner in the group
        if (!fails(kBody, 45)) s_.in(0, 0x22); // a property bit
        if (s_.odds(20)) { // more properties
            s_.bits();
            s_.bits();
        }
        s_.odds(60); // the item's base type
        if (s_.odds(60)) s_.in(2, 9000); // the item's spell
        else s_.in(-1, 1);
        fails(kRange | kBody, 15); // the spell manager allows it
        modifier(50, -3000, 1000); // the casting time modifier
        modifier(50, -3000, 500);  // the focus's
        if (s_.odds(10)) s_.in(-1, 1); // the focus cancelling the cast time
        s_.in(1, 200000);              // the item id
        if (s_.odds(40)) s_.in(0, 100); // the item's cast time
        else s_.in(101, 10000);
        if (s_.odds(10)) s_.odds(50);
        s_.in(-1, 30);     // the item's recast type
        s_.bits();         // the spell CRC
        fails(kSend, 15);  // Send's answer
    }

    // The range, the minimum range and the distance PlayerDistance answers, mostly near the limits the range
    // checks compare with.
    void ranges(Inputs& in) {
        static constexpr float kRanges[] = {0.0f, 10.0f, 100.0f, 200.0f, 250.5f, 500.0f, 416.0f, 20.0f};
        auto& sp = in.spell;
        sp.range = s_.odds(85) ? bits_of(s_.one_of(kRanges)) : any_float();
        if (s_.odds(60)) sp.min_range = 0;
        else sp.min_range = s_.odds(85) ? bits_of(static_cast<float>(s_.in(1, 60))) : any_float();
        const float range = float_of(sp.range);
        const float min_range = float_of(sp.min_range);
        float effective = range + static_cast<float>(focus_range_);
        if (effective > range * 5.0f) effective = range * 5.0f;
        const float npc_range = range * 1.2f < 500.0f ? 500.0f : range * 1.2f;
        float aim = effective;
        if (in.caster.type == 1) aim = s_.odds(50) ? range * 1.2f : npc_range;
        switch (s_.in(0, 4)) {
        case 0:
        case 1: in.distance = near(aim); break;
        case 2: in.distance = near(min_range); break;
        case 3: in.distance = bits_of(static_cast<float>(s_.in(0, 4000)) * 0.25f); break;
        default: in.distance = any_float(); break;
        }
        // Target type 45 measures to the position argument: put it at that distance on the caster's y.
        if (sp.target_type == 45 && in.call.positioned && s_.odds(70)) {
            const float toward = s_.odds(70) ? aim : min_range;
            const float d = float_of(near(toward));
            in.call.position = {float_of(in.caster.y) + d, float_of(in.caster.x), float_of(in.caster.z)};
        }
    }

    Stream& s_;
    uint32_t focus_;
    int32_t focus_range_ = 0;
};

// ---------------------------------------------------------------- the world of one record

constexpr uint32_t kSentinel = 0xeeeeeeeeu; // write-only caster fields: reported only when CastSpell wrote them

struct World {
    cs::Spawn caster, target, pet, owner, other;
    cs::Spell spell, rank_spell, casting_spell;
    std::array<cs::Spell, kBuffs> buff_spell{};
    std::array<cs::Affect, kBuffs> buffs{};
    cs::Item item, other_item;
    cs::State st;
    cs::Args args;
};

cs::Spawn other_spawn(const OtherSpawn& o) {
    cs::Spawn s{};
    s.type = o.type;
    s.spawn_id = o.id;
    s.stand_state = 0;
    s.mercenary = o.mercenary;
    s.summoned = o.summoned;
    s.master_id = o.master;
    s.has_character = o.has_character;
    s.z = o.z;
    s.height = o.height;
    return s;
}

cs::Spawn caster_spawn(const Inputs& in) {
    const auto& c = in.caster;
    cs::Spawn s{};
    s.type = c.type;
    s.spawn_id = c.id;
    s.stand_state = c.stand;
    s.master_id = c.master;
    s.mercenary = c.mercenary;
    s.hide_mode = c.hide;
    s.hp_max = c.hp_max;
    s.floor_height = c.floor;
    s.ceiling = c.ceiling;
    s.height = c.height;
    s.x = c.x;
    s.y = c.y;
    s.z = c.z;
    // Everything CastSpell may write starts as a sentinel; the casting spell id is an input.
    s.gem_eta.fill(kSentinel);
    s.cooldown_eta = kSentinel;
    cs::CastingData& cd = s.casting;
    cd.eta = kSentinel;
    cd.spell_id = c.casting;
    cd.slot = 0xee;
    cd.item_location.fill(kSentinel);
    cd.item_cast_type = kSentinel;
    cd.item_id = kSentinel;
    cd.y = kSentinel;
    cd.x = kSentinel;
    cd.target_id = kSentinel;
    cd.free_target = 0xee;
    cd.target_position.fill(kSentinel);
    return s;
}

// The spell, its next rank and the spell being cast are the same record but for the rank's id.
cs::Spell spell_of(const Inputs& in) {
    const auto& sp = in.spell;
    cs::Spell s{};
    s.id = sp.id;
    s.range = sp.range;
    s.min_range = sp.min_range;
    s.cast_time = sp.cast_time;
    s.mana = sp.mana;
    s.endurance = sp.endurance;
    s.base0 = sp.hp_cost;
    s.no_self = sp.no_self;
    s.pcnpc_only = sp.pcnpc_only;
    s.group = sp.group;
    s.subgroup = sp.subgroup;
    s.rank = sp.ranks;
    s.req_association = sp.association;
    s.can_cast_in_combat = sp.in_combat;
    s.can_cast_out_of_combat = sp.out_of_combat;
    s.only_during_fast_regen = sp.fast_regen_only;
    s.cast_not_standing = sp.not_standing;
    s.is_skill = sp.is_skill;
    s.spell_type = sp.kind;
    s.target_type = sp.target_type;
    s.travel_type = sp.travel;
    s.sneak = sp.sneak;
    for (std::size_t cls = 1; cls <= 35; ++cls) s.class_level[cls] = sp.class_level;
    for (std::size_t k = 0; k < sp.spa.size(); ++k) s.spa[k] = sp.spa[k];
    return s;
}

// A buff's spell: every slot SPA 3, with Sanctuary (312) where its kind says.
cs::Spell buff_spell_of(int32_t kind, std::size_t k) {
    cs::Spell s{};
    s.spa.fill(3);
    if (kind == 1) {
        s.spa[k % 12] = 312;
    } else if (kind == 2) {
        s.spa[k % 6] = 254;
        s.spa[k % 6 + 1] = 312;
    }
    return s;
}

void build(World& w, const Inputs& in) {
    w.caster = caster_spawn(in);
    w.target = other_spawn(in.target);
    w.pet = other_spawn(in.pet);
    w.owner = other_spawn(in.owner);
    OtherSpawn bystander;
    bystander.id = 1;
    w.other = other_spawn(bystander);
    w.spell = spell_of(in);
    w.rank_spell = w.spell;
    w.rank_spell.id = in.spell.id + 1;
    w.casting_spell = w.spell;
    for (std::size_t k = 0; k < kBuffs; ++k) {
        w.buffs[k].active = in.buff_active[k];
        w.buffs[k].spell_id = 20000 + static_cast<int32_t>(k);
        w.buff_spell[k] = buff_spell_of(in.buff_kind[k], k);
    }
    w.item.charges = in.charges;
    w.item.attuned = in.attuned;
    w.other_item = cs::Item{};

    w.st = cs::State{};
    cs::State& st = w.st;
    st.caster = in.caster.exists ? &w.caster : nullptr;
    st.local_player = in.caster.is_local_player ? &w.caster : &w.other;
    switch (in.first_target) {
    case FirstTarget::None: st.target = nullptr; break;
    case FirstTarget::Caster: st.target = &w.caster; break;
    case FirstTarget::Target: st.target = &w.target; break;
    }
    st.is_local_pc = in.character.is_local_pc;
    st.effects_cached = in.character.effects_cached;
    st.stunned = in.character.stunned;
    st.state_104 = in.character.state;
    st.skip_cost = in.character.skip_cost;
    st.class_id = in.character.class_id;
    st.local_block = in.world.local_block;
    st.auto_stand = in.world.auto_stand;
    st.spa25_block = in.world.spa25_block;
    st.zone_type = in.world.zone_type;
    st.free_target_tracker = in.world.free_target;
    st.quiet = in.world.quiet;
    st.demo = in.world.demo;
    st.connected = in.world.connected;
    st.now = in.world.now;

    w.args = cs::Args{};
    cs::Args& a = w.args;
    a.slot = in.call.slot;
    a.spell_id = in.call.spell_id;
    a.item = in.call.item ? &w.item : nullptr;
    a.item_location = in.call.item_location;
    a.item_cast_type = in.call.item_cast_type;
    a.send_packet = in.call.send_packet;
    if (in.call.positioned)
        a.position = std::array<uint32_t, 3>{bits_of(in.call.position[0]), bits_of(in.call.position[1]),
                                             bits_of(in.call.position[2])};
}

// The record's "in" against the regenerated inputs: if they differ, the stream is out of step.
std::string check_inputs(const Json& rec, const Inputs& in) {
    // A float as the record writes it; a NaN is only checked as a NaN (the writer prints its text form).
    auto same_float = [](const Json& j, uint32_t bits) {
        const float v = float_of(bits);
        return v != v || hailstone::test::f32_bits(j) == bits;
    };
    const auto& a = in.call;
    const bool args_ok = rec["slot"].i64() == a.slot && rec["spell_id_arg"].i32() == a.spell_id &&
                         rec["has_item"].truthy() == a.item && rec["item_cast_type"].i32() == a.item_cast_type &&
                         rec["send_packet"].i64() == a.send_packet;
    if (!args_ok) return "regenerated arguments differ";
    for (std::size_t k = 0; k < 3; ++k)
        if (rec["item_location"][k].u32() != a.item_location[k]) return "regenerated item location differs";
    if (rec["position"].is_null() == a.positioned) return "regenerated position flag differs";
    for (std::size_t k = 0; a.positioned && k < 3; ++k)
        if (!same_float(rec["position"][k], bits_of(a.position[k]))) return "regenerated position differs";

    const Json& c = rec["caster"];
    const auto& me = in.caster;
    if (c.is_null() == me.exists) return "regenerated caster differs";
    if (me.exists) {
        const bool ok = c["spawn_id"].u32() == me.id && c["stand_state"].i64() == me.stand &&
                        c["hp_max"].i32() == me.hp_max && same_float(c["floor_height"], me.floor) &&
                        same_float(c["field_11b4"], me.ceiling) && same_float(c["avatar_height"], me.height) &&
                        same_float(c["y"], me.y) && same_float(c["x"], me.x) && same_float(c["z"], me.z);
        if (!ok) return "regenerated caster fields differ";
    }

    const Json& s = rec["spell"];
    const auto& sp = in.spell;
    const bool spell_ok = s["id"].i32() == sp.id && same_float(s["range"], sp.range) &&
                          same_float(s["min_range"], sp.min_range) && s["cast_time"].u32() == sp.cast_time &&
                          s["mana"].i32() == sp.mana && s["target_type"].i64() == sp.target_type &&
                          s["field_1e8"].i32() == sp.association;
    if (!spell_ok) return "regenerated spell differs";

    const Json& t = rec["target"];
    if (t["spawn_id"].u32() != in.target.id || !same_float(t["z"], in.target.z) ||
        !same_float(t["avatar_height"], in.target.height))
        return "regenerated target differs";
    if (rec["now"].u32() != in.world.now || rec["buffs"].items.size() != static_cast<std::size_t>(in.max_effects))
        return "regenerated globals differ";
    return "";
}

// ---------------------------------------------------------------- the replay

class Replay final : public cs::Queries {
public:
    Replay(World& w, const Json& calls, const Inputs& in) : w_(w), calls_(calls), in_(in) {}
    std::string error;
    std::size_t used() const { return next_; }

    std::string role(const cs::Spawn* p) const {
        if (!p) return "none";
        if (p == &w_.caster) return "caster";
        if (p == &w_.target) return "target";
        if (p == &w_.pet) return "pet";
        if (p == &w_.owner) return "owner";
        if (p == &w_.other) return "local_player";
        return "other";
    }
    std::string spell_role(const cs::Spell* p) const {
        if (!p) return "none";
        if (p == &w_.spell) return "spell";
        if (p == &w_.rank_spell) return "rank_spell";
        if (p == &w_.casting_spell) return "casting_spell";
        for (const cs::Spell& b : w_.buff_spell)
            if (p == &b) return "buff_spell";
        return "other";
    }

    bool is_valid_spell_id(int32_t id) override {
        auto* c = take("is_valid_spell_id");
        arg(c, "id", id);
        return ret(c) != 0;
    }
    const cs::Spell* spell_by_id(int32_t id) override {
        auto* c = take("get_spell_by_id");
        arg(c, "id", id);
        return spell_named(c, id);
    }
    bool character_virtual_90() override { return ret(take("character_virtual_90")) != 0; }
    int32_t character_rank(int32_t a) override {
        auto* c = take("character_virtual_00");
        arg(c, "arg", a);
        return static_cast<int32_t>(ret(c));
    }
    const cs::Spell* spell_by_group_and_rank(int32_t group, int32_t sub, int32_t rank, int32_t a4) override {
        auto* c = take("get_spell_by_group_and_rank");
        arg(c, "group", group);
        arg(c, "subgroup", sub);
        arg(c, "rank", rank);
        arg(c, "arg4", a4);
        return spell_named(c, 0);
    }
    bool ok_to_transact() override { return ret(take("is_ok_to_transact")) != 0; }
    void pending_request_report(int32_t reason) override { arg(take("pending_request_report"), "reason", reason); }
    void dsp_chat(uint32_t eqstr, int32_t color, uint8_t a3, uint8_t a4) override {
        auto* c = take("dsp_chat");
        arg(c, "eqstr", eqstr);
        arg(c, "color", color);
        arg(c, "arg3", a3);
        arg(c, "arg4", a4);
    }
    void change_position(cs::Spawn& s, uint8_t state) override {
        auto* c = take("change_position");
        arg(c, "state", state);
        arg_s(c, "spawn", role(&s));
        if (in_.caster.stand_change_sticks) s.stand_state = state;
    }
    void message(uint32_t eqstr, int32_t color, int32_t a4) override {
        auto* c = take("token_string_to_client");
        arg(c, "eqstr", eqstr);
        arg(c, "color", color);
        arg(c, "arg4", a4);
    }
    void recache_spell_effects() override { take("recache_spell_effects"); }
    int32_t cached_buff_effect(int32_t spa, int32_t sub) override { return spa_call("cached_buff_effect", spa, sub); }
    int32_t cached_aa_effect(int32_t spa, int32_t sub) override { return spa_call("cached_aa_effect", spa, sub); }
    int32_t cached_item_effect(int32_t spa, int32_t sub) override { return spa_call("cached_item_effect", spa, sub); }
    cs::Handle pc_zone() override { return named(take("character_pc_zone"), "pc_zone") ? 1 : 0; }
    bool in_combat() override { return ret(take("is_in_combat")) != 0; }
    bool resting_for_fast_regen() override { return ret(take("is_resting_for_fast_regen")) != 0; }
    int32_t item_spell_recast_time(int32_t i) override { return indexed("item_spell_recast_time", i); }
    int32_t item_recast_timer() override { return static_cast<int32_t>(ret(take("get_item_recast_timer"))); }
    int32_t fast_time() override { return static_cast<int32_t>(ret(take("fast_time"))); }
    void create_target_indicator(uint8_t slot, const cs::Spell& spell, int32_t ict) override {
        auto* c = take("create_target_indicator");
        arg(c, "slot", slot);
        arg_s(c, "spell", spell_role(&spell));
        arg(c, "item_cast_type", ict);
    }
    uint8_t max_effects() override { return static_cast<uint8_t>(ret(take("get_max_effects"))); }
    cs::Affect* effect(int32_t index) override {
        arg(take("get_effect"), "index", index);
        return &w_.buffs[static_cast<std::size_t>(index >= 0 && index < kBuffs ? index : kBuffs - 1)];
    }
    void remove_affect(cs::Affect& e, int32_t a2, int32_t a3) override {
        auto* c = take("remove_affect");
        arg(c, "buff_index", &e - w_.buffs.data());
        arg(c, "arg2", a2);
        arg(c, "arg3", a3);
        e.active = 0;
    }
    void make_me_visible(int32_t a1, int32_t a2) override {
        auto* c = take("make_me_visible");
        arg(c, "arg1", a1);
        arg(c, "arg2", a2);
    }
    int32_t item_type() override { return static_cast<int32_t>(ret(take("item_type"))); }
    int32_t bard_cast_bard(const cs::Spell* spell, uint8_t class_id) override {
        auto* c = take("bard_cast_bard");
        arg_s(c, "spell", spell_role(spell));
        arg(c, "class", class_id);
        return static_cast<int32_t>(ret(c));
    }
    int32_t item_spell_required_level(int32_t i) override { return indexed("item_spell_required_level", i); }
    int32_t character_level() override { return static_cast<int32_t>(ret(take("character_level"))); }
    int32_t item_is_attuneable() override { return static_cast<int32_t>(ret(take("item_is_attuneable"))); }
    int32_t item_spell_effect_type(int32_t i) override { return indexed("item_spell_effect_type", i); }
    const cs::Item* inventory_item(int32_t slot) override {
        auto* c = take("inventory_get_item");
        arg(c, "slot", slot);
        return item_named(c);
    }
    bool can_use_item(int32_t a2, uint8_t a3) override {
        auto* c = take("can_use_item");
        arg(c, "arg2", a2);
        arg(c, "arg3", a3);
        return ret(c) != 0;
    }
    const cs::Item* pc_item(int32_t slot) override {
        auto* c = take("pc_get_item");
        arg(c, "slot", slot);
        return item_named(c);
    }
    int32_t character_virtual_34() override { return static_cast<int32_t>(ret(take("character_virtual_34"))); }
    cs::Spawn* pet_spawn() override { return spawn_named(take("get_pet_spawn")); }
    cs::Spawn* spawn_by_id(uint32_t id) override {
        auto* c = take("get_spawn_by_id");
        arg(c, "id", static_cast<int32_t>(id));
        return spawn_named(c);
    }
    bool can_see_position(cs::Spawn& s, const std::array<uint32_t, 3>&) override {
        auto* c = take("can_see_position");
        arg_s(c, "spawn", role(&s));
        return ret(c) != 0;
    }
    bool can_see(cs::Spawn& s, cs::Spawn& other) override {
        auto* c = take("can_see");
        arg_s(c, "spawn", role(&s));
        arg_s(c, "other", role(&other));
        return ret(c) != 0;
    }
    bool spawn_virtual_dc(cs::Spawn& s) override {
        auto* c = take("spawn_virtual_dc");
        arg_s(c, "spawn", role(&s));
        return ret(c) != 0;
    }
    int32_t not_on_same_deity(cs::Spawn& s, cs::Spawn& caster, const cs::Spell& spell) override {
        auto* c = take("not_on_same_deity");
        arg_s(c, "spawn", role(&s));
        arg_s(c, "caster", role(&caster));
        arg_s(c, "spell", spell_role(&spell));
        return static_cast<int32_t>(ret(c));
    }
    int32_t cur_mana(int32_t a) override {
        auto* c = take("cur_mana");
        arg(c, "arg", a);
        return static_cast<int32_t>(ret(c));
    }
    int32_t cur_endurance(int32_t a) override {
        auto* c = take("cur_endurance");
        arg(c, "arg", a);
        return static_cast<int32_t>(ret(c));
    }
    bool spell_allowed(cs::Spawn* t, int32_t association) override {
        auto* c = take("spell_manager_virtual_08");
        arg_s(c, "spawn", role(t));
        arg(c, "value", association);
        return ret(c) != 0;
    }
    int32_t focus_range_modifier(const cs::Spell& spell) override {
        auto* c = take("focus_range_modifier");
        arg_s(c, "spell", spell_role(&spell));
        return static_cast<int32_t>(ret(c));
    }
    uint32_t player_distance(cs::Spawn& from, cs::Spawn& to, uint32_t extra) override {
        auto* c = take("player_distance");
        arg_s(c, "from", role(&from));
        arg_s(c, "to", role(&to));
        arg(c, "extra_bits", static_cast<int32_t>(extra));
        return in_.distance;
    }
    cs::Handle spawn_character_pc(cs::Spawn& s) override {
        auto* c = take("spawn_character_pc");
        arg_s(c, "spawn", role(&s));
        return named(c, "pc") ? 2 : 0;
    }
    bool target_type_12_check(cs::Spawn& s) override {
        auto* c = take("target_type_12_check");
        arg_s(c, "spawn", role(&s));
        return ret(c) != 0;
    }
    bool is_self_or_group_member(cs::Handle, const cs::Spawn* who) override {
        auto* c = take("is_self_or_group_member");
        arg_s(c, "who", who ? role(who) : "caster_character");
        return ret(c) != 0;
    }
    bool has_property(cs::Spawn& s, int32_t p, int32_t a2, int32_t a3) override {
        auto* c = take("has_property");
        arg_s(c, "spawn", role(&s));
        arg(c, "property", p);
        arg(c, "arg2", a2);
        arg(c, "arg3", a3);
        return ret(c) != 0;
    }
    int32_t item_base_type() override { return static_cast<int32_t>(ret(take("item_base_type"))); }
    int32_t item_spell_id(int32_t i) override { return indexed("item_spell_id", i); }
    int32_t largest_cached_effect(int32_t spa, int32_t sub) override { return spa_call("largest_cached_effect", spa, sub); }
    int32_t casting_time_modifier(const cs::Spell& spell) override {
        auto* c = take("casting_time_modifier");
        arg_s(c, "spell", spell_role(&spell));
        return static_cast<int32_t>(ret(c));
    }
    int32_t focus_casting_time_modifier(const cs::Spell& spell, int32_t a3) override {
        auto* c = take("focus_casting_time_modifier");
        arg_s(c, "spell", spell_role(&spell));
        arg(c, "arg3", a3);
        return static_cast<int32_t>(ret(c));
    }
    void start_casting(int32_t id, uint16_t spawn_id, uint32_t t) override {
        auto* c = take("start_casting");
        arg(c, "spell_id", id);
        arg(c, "spawn_id", spawn_id);
        arg(c, "cast_time", static_cast<int32_t>(t));
    }
    void launch_spell_data_reset() override { arg_s(take("launch_spell_data_reset"), "object", "caster_casting_data"); }
    int32_t item_id() override { return static_cast<int32_t>(ret(take("item_id"))); }
    int32_t item_spell_cast_time(int32_t i) override { return indexed("item_spell_cast_time", i); }
    int32_t item_spell_recast_type(int32_t i) override { return indexed("item_spell_recast_type", i); }
    void set_item_recast_timer(int32_t time, int32_t type) override {
        auto* c = take("set_item_recast_timer");
        arg(c, "time", time);
        arg(c, "type", type);
    }
    uint32_t spell_crc(int32_t id) override {
        auto* c = take("spell_crc");
        arg(c, "spell_id", id);
        return static_cast<uint32_t>(ret(c));
    }
    void hton(uint32_t opcode, int32_t a2) override {
        auto* c = take("hton");
        arg(c, "opcode", opcode);
        arg(c, "arg2", a2);
    }
    bool send(int32_t channel, const cs::Packet&, int32_t length) override {
        auto* c = take("send");
        arg(c, "channel", channel);
        arg(c, "length", length);
        return ret(c) != 0;
    }
    void operator_delete(int32_t p) override { arg(take("operator_delete"), "pointer", p); }
    void close_connection() override { take("close_connection"); }
    void debug_log(uint32_t opcode) override { arg(take("debug_log"), "opcode", opcode); }
    void push_interact_reason(int32_t r) override { arg(take("push_interact_reason"), "reason", r); }
    void push_pending_reason(int32_t r) override { arg(take("push_pending_reason"), "reason", r); }

private:
    const Json* take(const char* fn) {
        if (!error.empty()) return nullptr;
        if (next_ >= calls_.items.size()) {
            error = "call " + std::to_string(next_) + ": extra " + fn;
            return nullptr;
        }
        const Json& c = calls_[next_++];
        if (c["fn"].text != fn) {
            error = "call " + std::to_string(next_ - 1) + ": expected " + c["fn"].text + ", got " + fn;
            return nullptr;
        }
        return &c;
    }
    void arg(const Json* c, const char* k, int64_t v) {
        if (c && error.empty() && (*c)[k].i64() != v)
            error = "call " + std::to_string(next_ - 1) + " " + (*c)["fn"].text + ": " + k + " " +
                    std::to_string((*c)[k].i64()) + ", got " + std::to_string(v);
    }
    void arg_s(const Json* c, const char* k, const std::string& v) {
        if (c && error.empty() && (*c)[k].text != v)
            error = "call " + std::to_string(next_ - 1) + " " + (*c)["fn"].text + ": " + k + " " + (*c)[k].text +
                    ", got " + v;
    }
    static int64_t ret(const Json* c) { return c ? (*c)["ret"].i64() : 0; }
    static bool named(const Json* c, const char* v) { return c && (*c)["ret"].text == v; }
    int32_t spa_call(const char* fn, int32_t spa, int32_t sub) {
        auto* c = take(fn);
        arg(c, "spa", spa);
        arg(c, "arg2", sub);
        return static_cast<int32_t>(ret(c));
    }
    int32_t indexed(const char* fn, int32_t i) {
        auto* c = take(fn);
        arg(c, "index", i);
        return static_cast<int32_t>(ret(c));
    }
    const cs::Spell* spell_named(const Json* c, int32_t id) {
        if (!c) return nullptr;
        const std::string& r = (*c)["ret"].text;
        if (r == "spell") return &w_.spell;
        if (r == "rank_spell") return &w_.rank_spell;
        if (r == "casting_spell") return &w_.casting_spell;
        if (r == "buff_spell" && id >= 20000 && id < 20000 + kBuffs) return &w_.buff_spell[static_cast<std::size_t>(id - 20000)];
        return nullptr;
    }
    const cs::Item* item_named(const Json* c) {
        if (!c) return nullptr;
        const std::string& r = (*c)["ret"].text;
        if (r == "cast_item") return &w_.item;
        if (r == "other_item") return &w_.other_item;
        return nullptr;
    }
    cs::Spawn* spawn_named(const Json* c) {
        if (!c) return nullptr;
        const std::string& r = (*c)["ret"].text;
        if (r == "caster") return &w_.caster;
        if (r == "target") return &w_.target;
        if (r == "pet") return &w_.pet;
        if (r == "owner") return &w_.owner;
        if (r == "local_player") return &w_.other;
        return nullptr;
    }

    World& w_;
    const Json& calls_;
    const Inputs& in_;
    std::size_t next_ = 0;
};

// ---------------------------------------------------------------- the outputs

std::string check_outputs(const World& w, const Inputs& in, const Replay& r, const Json& out, uint8_t value,
                          const std::optional<cs::Packet>& sent) {
    if (out["value"].i64() != value) return "value " + std::to_string(out["value"].i64()) + ", got " + std::to_string(value);
    if (out["target"].text != r.role(w.st.target)) return "target " + out["target"].text + ", got " + r.role(w.st.target);
    // The caster fields written.
    std::map<std::string, int64_t> got;
    const cs::Spawn& c = w.caster;
    for (std::size_t k = 0; k < 13; ++k)
        if (c.gem_eta[k] != kSentinel) got["gem_eta_" + std::to_string(k)] = static_cast<int32_t>(c.gem_eta[k]);
    auto put = [&](const char* key, uint32_t v) {
        if (v != kSentinel) got[key] = static_cast<int32_t>(v);
    };
    put("cooldown_eta", c.cooldown_eta);
    put("casting_eta", c.casting.eta);
    if (c.casting.spell_id != in.caster.casting) got["casting_spell_id"] = c.casting.spell_id;
    if (c.casting.slot != 0xee) got["casting_slot"] = c.casting.slot;
    put("casting_item_location_0", c.casting.item_location[0]);
    put("casting_item_location_1", c.casting.item_location[1]);
    put("casting_item_location_2", c.casting.item_location[2]);
    put("casting_item_cast_type", c.casting.item_cast_type);
    put("casting_item_id", c.casting.item_id);
    if (c.casting.y != kSentinel) got["casting_y"] = c.casting.y;
    if (c.casting.x != kSentinel) got["casting_x"] = c.casting.x;
    put("casting_target_id", c.casting.target_id);
    if (c.casting.free_target != 0xee) got["casting_free_target"] = c.casting.free_target;
    if (c.casting.target_position[0] != kSentinel) {
        got["casting_target_y"] = c.casting.target_position[0];
        got["casting_target_x"] = c.casting.target_position[1];
        got["casting_target_z"] = c.casting.target_position[2];
    }
    std::map<std::string, int64_t> want;
    for (const auto& [k, v] : out["caster_written"].fields) {
        bool is_float = k == "casting_y" || k == "casting_x" || k.rfind("casting_target_", 0) == 0;
        if (k == "casting_target_id") is_float = false;
        want[k] = is_float ? static_cast<int64_t>(hailstone::test::f32_bits(v)) : v.i64();
    }
    if (got != want) {
        std::string d = "caster fields:";
        for (const auto& [k, v] : want)
            if (!got.count(k) || got[k] != v) d += " " + k + "=" + std::to_string(v) + "/" + (got.count(k) ? std::to_string(got[k]) : "unset");
        for (const auto& [k, v] : got)
            if (!want.count(k)) d += " extra " + k + "=" + std::to_string(v);
        return d;
    }
    if (out.has("character_468") != w.st.character_468.has_value() ||
        (w.st.character_468 && out["character_468"].i64() != *w.st.character_468))
        return "character +0x468";
    if (out.has("last_beneficial") != w.st.last_beneficial.has_value() ||
        (w.st.last_beneficial && out["last_beneficial"].i64() != *w.st.last_beneficial))
        return "0xde0880";
    if (out["currently_interacting"].i64() != w.st.currently_interacting || out["item_pending"].i64() != w.st.item_pending ||
        out["packet_count_delta"].i64() != w.st.packet_count)
        return "request counters";
    const Json& p = out["packet"];
    if (p.is_null() != !sent.has_value()) return p.is_null() ? "a packet the original did not send" : "no packet";
    if (!sent) return "";
    if (p["slot"].i64() != sent->slot || p["spell_id"].i32() != sent->spell_id || p["target_id"].u32() != sent->target_id ||
        p["crc"].u32() != sent->crc || p["has_position"].i64() != sent->has_position ||
        p["position_set"].truthy() != sent->position_set)
        return "packet fields";
    for (std::size_t k = 0; k < 3; ++k)
        if (p["item_location"][k].u32() != sent->item_location[k]) return "packet item location";
    if (sent->position_set) {
        const char* keys[] = {"y", "x", "z"};
        for (std::size_t k = 0; k < 3; ++k)
            if (hailstone::test::f32_bits(p[keys[k]]) != sent->position[k]) return std::string("packet position ") + keys[k];
    }
    // The bytes the client sends for these fields: net::encode(CastSpell) against the record's payload.
    std::array<uint8_t, 0x2c> want_bytes{};
    auto put32 = [&](std::size_t at, uint32_t v) {
        for (std::size_t i = 0; i < 4; ++i) want_bytes[at + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    put32(0, p["slot"].u32());
    put32(4, p["spell_id"].u32());
    for (std::size_t k = 0; k < 3; ++k) put32(8 + 4 * k, p["item_location"][k].u32());
    put32(0x14, p["target_id"].u32());
    put32(0x18, p["crc"].u32());
    want_bytes[0x1c] = static_cast<uint8_t>(p["has_position"].i64());
    if (p["position_set"].truthy()) {
        put32(0x20, hailstone::test::f32_bits(p["y"]));
        put32(0x24, hailstone::test::f32_bits(p["x"]));
        put32(0x28, hailstone::test::f32_bits(p["z"]));
    }
    hailstone::net::CastSpell n = hailstone::net::cast_spell_packet(*sent);
    hailstone::net::AppPacket ap = hailstone::net::encode(n);
    if (ap.opcode != 0x1287 || ap.data.size() != want_bytes.size()) return "encoded OP_CastSpell size";
    for (std::size_t i = 0; i < want_bytes.size(); ++i) {
        if (i >= 0x1d && i < 0x20) continue; // never written by the original
        if (i >= 0x20 && !p["position_set"].truthy()) continue;
        if (std::to_integer<uint8_t>(ap.data[i]) != want_bytes[i]) return "encoded OP_CastSpell byte +" + std::to_string(i);
    }
    return "";
}

std::string run_record(const Json& rec, const Inputs& in) {
    if (std::string why = check_inputs(rec["in"], in); !why.empty()) return why;
    const Json& out = rec["out"];
    World w;
    build(w, in);
    w.st.precision = rec["precision"].text == "x87-24" ? FpuPrecision::Single24 : FpuPrecision::Double53;
    Replay r(w, out["calls"], in);
    std::optional<cs::Packet> sent;
    const uint8_t value = cs::cast_spell(w.st, w.args, r, &sent);
    if (!r.error.empty()) return r.error;
    if (r.used() != out["calls"].items.size())
        return "missing call " + std::to_string(r.used()) + ": " + out["calls"][r.used()]["fn"].text;
    return check_outputs(w, in, r, out, value, sent);
}

// One stream per file: each record's inputs are the next draws.
void check_file(const char* file, uint32_t focus, uint32_t seed) {
    Stream stream(seed);
    Generator gen(stream, focus);
    hailstone::test::each_vector(file, [&](const Json& rec) { return run_record(rec, gen.next()); });
}

} // namespace

TEST_CASE("CastSpell_Checks: the early checks") { check_file("CastSpell_Checks.jsonl", kChecks, 0xca51); }
TEST_CASE("CastSpell_Buffs: Sanctuary, invisibility and sneak attacks") { check_file("CastSpell_Buffs.jsonl", kSanctuary, 0xca52); }
TEST_CASE("CastSpell_Items: item casts, songs, level, SPA 25 / 32") { check_file("CastSpell_Items.jsonl", kItems, 0xca53); }
TEST_CASE("CastSpell_Target: retargets, line of sight, mount, deity, bards, mana") { check_file("CastSpell_Target.jsonl", kTarget, 0xca54); }
TEST_CASE("CastSpell_Range: the target-type switch and its ranges") { check_file("CastSpell_Range.jsonl", kRange, 0xca55); }
TEST_CASE("CastSpell_BodyType: body types, pets, corpses, group, self") { check_file("CastSpell_BodyType.jsonl", kBody, 0xca56); }
TEST_CASE("CastSpell_Send: cast time, CastingData, timers and OP_CastSpell") { check_file("CastSpell_Send.jsonl", kSend, 0xca57); }
