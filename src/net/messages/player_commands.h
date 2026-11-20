#pragma once

// Protocol-independent player commands and notices. RoF2 owns their binary layouts.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hailstone::net {
// Gameplay command values shared by the connection, UI and protocol codecs.
inline constexpr std::uint32_t kHideCorpseNone = 0, kHideCorpseAll = 1, kHideCorpseAllButGroup = 2,
                               kHideCorpseLooted = 3, kHideCorpseThis = 4, kHideCorpseNpc = 5;
inline constexpr std::uint32_t kPrismaticDye = 32557;
inline constexpr std::uint32_t kBugRepeatable = 1, kBugCrash = 2, kBugTargetInfo = 4, kBugCharacterFlags = 8;
// Bug report categories are shared by the gameplay UI and the wire encoder.
inline constexpr int kBugCategories = 13;
const char* bug_category_name(std::uint32_t category);

struct DuelRequest {
    std::uint32_t first = 0;  // client → server: the target; server → client: the challenger
    std::uint32_t second = 0; // client → server: our id; server → client: the challenger again
};

struct DuelRefusal {
    std::uint32_t challenger = 0; // the receiver reads the refusing player at +4
    std::uint32_t refuser = 0;
    std::uint8_t kind = 0;
};

struct Surname {
    std::string name;
    std::uint32_t unknown64 = 0;
    std::string last_name;
};

struct LevelMessage {
    std::string title, text, yes, no;
    std::uint8_t buttons = 0, sound = 0;
    std::uint32_t duration = 0, popup_id = 0, negative_id = 0, close_id = 0, language = 0;
};

struct TitleEntry {
    std::uint32_t id = 0;
    std::string prefix, suffix;
};

struct TitleReply {
    bool suffix = false;
    std::string text;
    std::uint32_t spawn_id = 0;
};

struct BandolierPacket {
    std::uint32_t action = 0;
    std::uint8_t number = 0;
    std::string name;
};

struct NetworkControlNotice {
    enum class Kind : std::uint8_t { OfflineRefused, AttackLock, ChatBlock };
    Kind kind = Kind::OfflineRefused;
    bool enabled = false;
    std::string character;
};
struct UptimeNotice { std::uint32_t world_seconds = 0, zone_seconds = 0; };
struct DuelDeclineIntent {
    std::uint32_t partner = 0, player = 0;
    bool eqemu_response = false; // EQEmu requires its 12-byte decline, unlike the original 8-byte command
};
struct CorpseCommand {
    std::string corpse_name, player_name;
    std::uint32_t zone_id = 0;
    float y = 0, x = 0, z = 0;
};
struct CorpseDecayIntent { std::uint32_t corpse_id = 0, player_id = 0; };
struct GuildDeleteIntent { std::uint32_t guild_id = 0; std::string character; };
struct BandolierHoldIntent { std::uint8_t set = 0, slot = 0; bool shift = false; };
struct PotionBeltIntent { bool clear = false, hold = false, shift = false; std::uint32_t slot = 0, item_id = 0; };
struct BugReport {
    std::uint32_t category = 0;
    std::string reporter;
    float x = 0, y = 0, z = 0; // server coordinates
    std::uint32_t heading = 0, minutes_played = 0, target_id = 0, flags = 0;
    std::string target_name, text, system_info;
};

struct DyeIntent { std::array<std::uint32_t,9> tints{}; };
struct Feedback {
    std::string name;
    float y = 0, x = 0, z = 0;
    std::int32_t heading = 0;
    std::string text;
};
struct HideCorpseIntent { std::uint32_t action = 0, corpse = 0; };
struct SpellScribeIntent { std::string actor, target; };
struct RaidShieldIntent { std::vector<std::uint32_t> members; };
struct OfflineModeIntent { bool request = true; };

struct TitleSelection { bool suffix = false; std::uint32_t title_id = 0; };
struct PopupAnswer { std::uint32_t kind = 0; std::uint32_t id = 0; };
} // namespace hailstone::net
