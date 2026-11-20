// RoF2 wire decoder fuzz tests, including the protocol extensions and game-side payload decoders.
// Inputs are deterministic truncations and mutations of documented packet layouts.
// -DHAILSTONE_SANITIZE=ON also detects out-of-bounds reads and undefined behavior.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <zlib.h>

#include "core/byte_io.h"
#include "rof2/fuzz_game_decoders.h"
#include "rof2/fuzz_achievements.h"
#include "fuzz_support.h"
#include "fuzz_patterns.h"
#include "net/protocol/rof2/packets/aa_packets.h"
#include "net/protocol/rof2/packets/achievement_packets.h"
#include "net/protocol/rof2/packets/ability_packets.h"
#include "net/protocol/rof2/packets/adventure_packets.h"
#include "net/protocol/rof2/packets/aggro_packets.h"
#include "net/protocol/rof2/packets/augment_packets.h"
#include "net/protocol/rof2/packets/evolving_packets.h"
#include "net/connection/chat_client.h"
#include "net/protocol/rof2/packets/door_packets.h"
#include "net/protocol/rof2/packets/dz_packets.h"
#include "net/protocol/rof2/packets/group_packets.h"
#include "net/protocol/rof2/packets/fellowship_packets.h"
#include "net/protocol/rof2/packets/guild_banner_packets.h"
#include "net/protocol/rof2/packets/guild_bank_packets.h"
#include "net/protocol/rof2/packets/item_packets.h"
#include "net/connection/login_client.h"
#include "net/protocol/login/codec.h"
#include "net/protocol/login/opcodes.h"
#include "net/protocol/rof2/packets/market_packets.h"
#include "net/protocol/rof2/packets/marketplace_packets.h"
#include "net/protocol/rof2/packets/merchant_packets.h"
#include "net/protocol/rof2/packets/movement_packets.h"
#include "net/protocol/rof2/packets/missile_packets.h"
#include "net/protocol/rof2/packets/zoning_packets.h"
#include "net/protocol/rof2/packets/notice_packets.h"
#include "net/protocol/extensions/hailstone/quest_npc_packets.h"
#include "net/protocol/extensions/hailstone/settings_packets.h"
#include "net/protocol/extensions/hailstone/zone_link_packets.h"
#include "net/protocol/extensions/hailstone/guild_packets.h"
#include "net/protocol/ucs/codec.h"
#include "net/protocol/rof2/opcode_map.h"
#include "net/protocol/rof2/packets/pet_packets.h"
#include "net/protocol/rof2/packets/real_estate_packets.h"
#include "net/protocol/rof2/packets/reward_packets.h"
#include "net/protocol/rof2/packets/player_packets.h"
#include "net/session/soe/session.h"
#include "net/protocol/extensions/hailstone/server_files_packets.h"
#include "net/session/soe/framing.h"
#include "net/protocol/rof2/packets/social_packets.h"
#include "net/protocol/rof2/packets/spell_packets.h"
#include "net/protocol/rof2/packets/task_packets.h"
#include "net/protocol/rof2/packets/shroud_packets.h"
#include "net/protocol/rof2/packets/trade_packets.h"
#include "net/protocol/rof2/packets/training_packets.h"
#include "net/protocol/rof2/packets/tribute_packets.h"
#include "net/protocol/rof2/packets/world_packets.h"
#include "net/protocol/rof2/packets/zone_packets.h"
#if defined(HAILSTONE_HAS_NMS)
#include "nms_packets.h"
#endif
#if defined(HAILSTONE_HAS_TRIUNE)
#include "triune_packets.h"
#include "triune_windows.h"
#endif
#include "platform/udp_socket.h"

using namespace hailstone;
using namespace hailstone::net;
using namespace hailstone::net::login_codec;
using fuzz::Bytes;
using fuzz::Rng;
using fuzz::Throws;

namespace {

struct Decoder {
    const char* name;
    std::function<void(const AppPacket&)> run;
};

const std::vector<Decoder>& decoders() {
    static const std::vector<Decoder> list = {
#define HAILSTONE_FUZZ_DECODER(fn) {#fn, [](const AppPacket& p) { (void)fn(p); }},
#define HAILSTONE_FUZZ_PLUGIN_DECODER(ns, fn) {#fn, [](const AppPacket& p) { (void)::ns::fn(p.data); }},
#define HAILSTONE_FUZZ_GAME_DECODER(fn) {#fn, [](const AppPacket& p) { ::hailstone::fuzz_game::feed(&::hailstone::game::fn, p.data); }},
#define HAILSTONE_FUZZ_ZONE_BIND_DECODER(fn) {#fn, [](const AppPacket& p) { ::hailstone::fuzz_game::feed(&::hailstone::net::rof2::fn, p.data); }},
#define HAILSTONE_FUZZ_MISSILE_DECODER(fn) {#fn, [](const AppPacket& p) { (void)::hailstone::net::rof2::fn(p.data); }},
#include "fuzz_decoders.inc"
#undef HAILSTONE_FUZZ_ZONE_BIND_DECODER
#undef HAILSTONE_FUZZ_MISSILE_DECODER
#undef HAILSTONE_FUZZ_GAME_DECODER
#undef HAILSTONE_FUZZ_PLUGIN_DECODER
#undef HAILSTONE_FUZZ_DECODER
    };
    return list;
}

// Every opcode value RoF2 defines, plus the low values the one-byte chat-server and login opcodes use.
std::vector<std::uint16_t> all_opcodes() {
    std::set<std::uint16_t> s;
    for (std::uint16_t i = 0; i < 0x40; ++i) s.insert(i);
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(rof2::Op::Count_); ++i) s.insert(rof2::to_wire(static_cast<rof2::Op>(i)));
    return {s.begin(), s.end()};
}

} // namespace

TEST_CASE("fuzz: the decoder list covers every decode_*(const AppPacket&) declaration") {
    fuzz::QuietLog quiet;
    std::set<std::string> listed;
    for (const auto& d : decoders()) listed.insert(d.name);
    CHECK(listed.size() >= 150);
    // The wire decoder tests live in tests/rof2, two directories below the repository root.
    std::string dir = std::string(__FILE__);
    dir = dir.substr(0, dir.rfind('/') == std::string::npos ? 0 : dir.rfind('/')) + "/../../src/net/";
    // Scan protocol-owned subdirectories as well: RoF2, login, UCS and
    // extension codecs no longer keep their packet headers at src/net/ root.
    // A decoder may take more parameters after the packet if they have defaults (decode_hailstone_guild's reply);
    // decode_with's body has none and is a helper, not a decoder.
    std::regex decl(R"(\b(decode_[a-z_0-9]+)\(const AppPacket& p(?:, [^,)=]+=[^,)]+)*\))");
    int scanned = 0;
    std::vector<std::filesystem::path> headers;
    std::error_code net_ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, net_ec))
        if (entry.is_regular_file() && entry.path().extension() == ".h") headers.push_back(entry.path());
    std::sort(headers.begin(), headers.end());
    for (const auto& h : headers) {
        std::ifstream f(h);
        if (!f) continue;
        ++scanned;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        for (std::sregex_iterator it(text.begin(), text.end(), decl), end; it != end; ++it)
            CHECK_MESSAGE(listed.count((*it)[1]) == 1, (*it)[1].str() << " is missing from tests/rof2/fuzz_decoders.inc");
    }
    // Run from the source tree (net_ec set when it is not: an installed test binary has nothing to compare), the
    // scan must have found the headers it is meant to check.
    if (!net_ec) CHECK(scanned >= 28);
    // The in-tree plugins' decoders take the bytes OnPacket hands them.
    std::regex plugin_decl(R"(\b(decode_[a-z_0-9]+)\(Bytes p\))");
    for (const char* h : {"../../plugins/nms/nms_packets", "../../plugins/projecttriune/triune_packets",
                          "../../plugins/projecttriune/triune_windows"}) {
        std::ifstream f(dir + h + ".h");
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        for (std::sregex_iterator it(text.begin(), text.end(), plugin_decl), end; it != end; ++it)
            CHECK_MESSAGE(listed.count((*it)[1]) == 1, (*it)[1].str() << " is missing from tests/rof2/fuzz_decoders.inc");
    }
    // The game-side decoders of server data over raw bytes.
    std::regex game_decl(R"(\b(decode_[a-z_0-9]+)\(std::span<const std::(?:byte|uint8_t)>)");
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir + "../game", ec)) {
        if (entry.path().extension() != ".h") continue;
        std::ifstream f(entry.path());
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        for (std::sregex_iterator it(text.begin(), text.end(), game_decl), end; it != end; ++it)
            CHECK_MESSAGE(listed.count((*it)[1]) == 1, (*it)[1].str() << " is missing from tests/rof2/fuzz_decoders.inc");
    }
}

TEST_CASE("fuzz: typed decoders never throw, hang or over-allocate on truncated and mutated packets") {
    fuzz::QuietLog quiet;
    const auto pats = fuzz::patterns();
    const auto lengths = fuzz::truncation_lengths(24000);
    for (const auto& dec : decoders()) {
        std::size_t failures = 0;
        auto run = [&](std::uint16_t opcode, const Bytes& data) {
            AppPacket p(opcode, data);
            if (!fuzz::run_case(dec.name, data, Throws::Never, [&](std::span<const std::byte>) { dec.run(p); })) ++failures;
        };
        for (const auto& pat : pats)
            for (auto n : lengths) run(0x1234, Bytes(pat.begin(), pat.begin() + static_cast<std::ptrdiff_t>(n)));
        Rng rng(0x5eed0000u + decoders().size());
        for (int i = 0; i < 300 && failures == 0; ++i) {
            Bytes base = fuzz::random_bytes(rng, rng.below(400));
            run(0x1234, fuzz::mutate(base, rng));
        }
        CHECK_MESSAGE(failures == 0, dec.name << " failed " << failures << " cases");
    }
}

TEST_CASE("fuzz: achievement variable layouts run with a received catalog and independent wire seeds") {
    fuzz::QuietLog quiet;
    namespace fixture = test::achievement_fixture;
    std::vector<Bytes> seeds;
    const auto add = [&](unsigned choice, const AppPacket& packet) {
        Bytes bytes{static_cast<std::byte>(choice)};
        bytes.insert(bytes.end(), packet.data.begin(), packet.data.end()); seeds.push_back(std::move(bytes));
    };
    add(0, fixture::primary()); add(1, fixture::comparison()); add(2, fixture::inspect());
    ByteWriter earned; earned.le<std::uint32_t>(77); earned.le<std::uint32_t>(900); earned.le<std::uint32_t>(3695);
    fixture::string(earned, fixture::metadata()); add(3, {kOpAchievementEarned, earned.take()});
    ByteWriter guild; guild.le<std::uint32_t>(3); guild.le<std::uint64_t>(0); guild.fixed_string("Peer", 64);
    guild.le<std::uint32_t>(0); guild.le<std::uint32_t>(900); fixture::string(guild, fixture::metadata());
    add(4, {rof2::to_wire(rof2::Op::GuildUpdate), guild.take()});
    Bytes metadata{std::byte{5}};
    for (const auto c : fixture::metadata()) metadata.push_back(static_cast<std::byte>(c));
    seeds.push_back(std::move(metadata)); add(6, fixture::catalog());
    (void)fuzz_achievements::catalog(); // Build the fixed context outside each allocation/timing guard.
    ByteWriter forged; forged.le(static_cast<std::uint32_t>(kMaxAchievementCatalogBytes));
    forged.le<std::uint16_t>(0); const auto forged_bytes = forged.take();
    fuzz::reset_largest_allocation();
    REQUIRE(fuzz::run_case("achievement forged inflate size", forged_bytes, Throws::Never,
        [&](std::span<const std::byte>) { CHECK_FALSE(decode_achievement_catalog({kOpAchievementDefinitions, forged_bytes})); }));
    CHECK(fuzz::largest_allocation() < (1u << 20)); // declared size alone cannot reserve 64 MiB
    Rng rng(0xac41e);
    std::size_t failures = 0;
    for (const auto& seed : seeds) {
        for (std::size_t n = 0; n <= seed.size(); ++n) {
            const Bytes prefix(seed.begin(), seed.begin() + static_cast<std::ptrdiff_t>(n));
            if (!fuzz::run_case("achievement catalog context", prefix, Throws::Never,
                [&](std::span<const std::byte> bytes) { fuzz_achievements::feed(bytes); })) ++failures;
        }
        for (int i = 0; i < 256 && failures == 0; ++i) {
            const auto bytes = fuzz::mutate(seed, rng);
            if (!fuzz::run_case("achievement catalog context", bytes, Throws::Never,
                [&](std::span<const std::byte> input) { fuzz_achievements::feed(input); })) ++failures;
        }
    }
    CHECK(failures == 0);
}

TEST_CASE("fuzz: decoders that look at the opcode, over every known opcode value") {
    fuzz::QuietLog quiet;
    const auto ops = all_opcodes();
    const auto pats = fuzz::patterns();
    for (const char* only : {"decode_chat_server_packet", "decode_duel_refusal", "decode_task_select", "decode_skill_timers",
                             "decode_move_item", "decode_trade_request", "decode_waypoint_list", "decode_server_auth_stats"}) {
        for (const auto& dec : decoders()) {
            if (std::string(dec.name) != only) continue;
            for (auto op : ops)
                for (std::size_t n : {0, 1, 4, 9, 12, 33, 64, 300})
                    for (const auto& pat : {pats[0], pats[1], pats[3]}) {
                        Bytes data(pat.begin(), pat.begin() + static_cast<std::ptrdiff_t>(n));
                        AppPacket p(op, data);
                        fuzz::run_case(dec.name, data, Throws::Never, [&](std::span<const std::byte>) { dec.run(p); });
                    }
        }
    }
}
