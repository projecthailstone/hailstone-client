#pragma once

// Protocol-independent gameplay values extracted from the RoF2 packet declarations.
// Application codecs translate these values to their own wire layouts.
// Serial lengths, packet opcodes and binary read/write remain in protocol/rof2/.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hailstone::net {

struct LoginInfo {
    std::string account_id;       // login-server account id, as decimal text
    std::string session_key;      // key from OP_PlayEverquestResponse
    bool zoning = false;          // 1 when re-authenticating during a zone change
    std::string custom_files_key; // NUL-terminated at +64; world checks it when the
                                  // World:CustomFilesKey rule is set (empty otherwise)
};

// Character selection to enter the world; the codec owns home/tutorial wire flags.
struct EnterWorld {
    std::string name;
    std::uint32_t tutorial = 0;
    bool return_home = false;
};

// OP_World_Client_CRC1/2/3 (2056 bytes): checksum and size of a file, then 256
// random dword indices into it and the dwords found there.
struct FileChecksum {
    std::uint32_t checksum = 0; // eq_crc32 of the whole file
    std::uint32_t size = 0;
    std::array<std::uint32_t, 256> index{};
    std::array<std::uint32_t, 256> value{};

    // EQEmu stores (and, when World:EnableChecksumVerification is on, compares)
    // checksum | size << 32.
    std::uint64_t server_value() const { return checksum | (static_cast<std::uint64_t>(size) << 32); }
};

enum class ChecksumKind : std::uint8_t {
    Exe,       // CRC1: eqgame.exe
    SkillCaps, // CRC2: Resources/SkillCaps.txt
    BaseData,  // CRC3: Resources/BaseData.txt
};

// Input data, not the sampled on-wire CRC record. A future codec can use a
// different proof algorithm without exposing its layout to GameConnection.
struct ClientFileProofSource {
    ChecksumKind kind = ChecksumKind::Exe;
    std::vector<std::byte> contents;
    std::uint32_t seed = 0;
};

// OP_ApproveName, client → world (80 bytes). World reads name/race/class only.
struct NameApproval {
    std::string name;
    std::uint32_t race = 0;
    std::uint32_t class_ = 0;
    std::uint32_t deity = 0;
    std::uint32_t flag = 0; // the creation window's tutorial flag
};

// OP_CharacterCreate, client → world (96 bytes, CharCreate_Struct). Sent after
// a successful OP_ApproveName; the name is the one approved. On success world
// answers with a fresh OP_SendCharInfo, on failure with OP_ApproveName = 0.
struct CharCreate {
    std::uint32_t gender = 0, race = 0, class_ = 0, deity = 0, start_zone = 0;
    std::uint32_t hair_color = 0, beard = 0, beard_color = 0, hair_style = 0, face = 0;
    std::uint32_t eye_color1 = 0, eye_color2 = 0;
    std::uint32_t drakkin_heritage = 0, drakkin_tattoo = 0, drakkin_details = 0;
    std::uint32_t str = 0, sta = 0, agi = 0, dex = 0, wis = 0, int_ = 0, cha = 0;
    std::uint32_t tutorial = 0;
};

// OP_CharacterCreateRequest: the client sends it empty (make_empty); world
// answers with the combination and stat tables it validates OP_CharacterCreate
// against (world/client.cpp HandleCharacterCreateRequestPacket; DB tables
// char_create_point_allocations and char_create_combinations).
struct CharCreateAllocation {
    std::uint32_t index = 0;
    // Order STR, DEX, AGI, STA, INT, WIS, CHA (as CheckCharCreateInfoSoF uses them).
    std::array<std::uint32_t, 7> base{};
    std::array<std::uint32_t, 7> points{}; // default allocation; their sum is the free-point budget
};

struct CharCreateCombo {
    std::uint32_t expansion_required = 0, race = 0, class_ = 0, deity = 0, allocation_index = 0, zone = 0;
};

// 0x7f2d00: a combination is offered when the account owns every expansion it requires (world's
// OP_ExpansionInfo bits, GameConnection::world_expansions).
constexpr bool char_create_allowed(const CharCreateCombo& c, std::uint32_t owned) {
    return (c.expansion_required & owned) == c.expansion_required;
}

struct CharCreateData {
    std::vector<CharCreateAllocation> allocations;
    std::vector<CharCreateCombo> combos;
};

// Validate a character's class/race/deity/zone combination and stat budget
// against the server-provided character creation tables.
std::optional<CharCreateAllocation> validate_char_create(const CharCreateData& data, const CharCreate& cc);

// Build the default character attributes for a permitted creation combo.
std::optional<CharCreate> default_char_create(const CharCreateData& data, const CharCreateCombo& combo,
                                              std::uint32_t gender);

// OP_DeleteCharacter: the name with its terminator.
struct DeleteCharacter {
    std::string name;
};

// OP_RandomNameGenerator (72 bytes both ways; world fills in name).
struct RandomName {
    std::uint32_t race = 0;
    std::uint32_t gender = 0;
    std::string name;
};

// OP_LogServer (560 bytes).
struct LogServer {
    std::int32_t pvp = 0;       // +4 dword, World:PVPSettings: 0xdcee70 and 0xddfd2c (0 none, 1 Rallos Zek,
                                // 2 Tallon / Vallon Zek, 4 Sullon Zek, 6 Discord)
    std::uint8_t enable_fv = 0; // +8 byte: 0xdcee74
    // +0x18 dword: 0xddfd3c (WorldAuthenticationHandler::OnRoutePacket @0x562f63), the switch Cmd_spellscribe and
    // Cmd_betabuff test (EQEmu leaves it 0).
    std::int32_t spell_scribe = 0;
    std::string world_short_name; // +36, char[32]
};

// OP_SendMembership (RoF2: 116 bytes; rof2_structs.h Membership_Struct). The original reads it into
// FreeToPlayClient::Instance() (GameFeatureRestrictions_ReadEntry @0x7dbaa0): the level at +0, races, classes, then a
// counted array of which it keeps at most 22 entries (@0x57b600 stops at its capacity); the rest is not read.
// Handled at character select and in game (WorldAuthenticationHandler, HandleWorldMessage).
inline constexpr std::size_t kMembershipEntryCap = 22;

struct Membership {
    std::uint32_t level = 0;   // 0 free, 1 silver, 2 gold (EQEmu: always 2)
    std::uint32_t races = 0;   // available race bits
    std::uint32_t classes = 0; // available class bits
    std::vector<std::int32_t> entries; // at most kMembershipEntryCap (EQEmu sends 25)
};

// OP_SendMembershipDetails (rof2_structs.h Membership_Details_Struct), GameFeatureRestrictions_UnSerialize @0x7dbe90:
// u32 count, count x {u32 level (0..2), u32 feature, i32 value}; then two counted lists of {u32 purchase id,
// u32 bits} (races, classes; @0x7dbb90), then two strings (u32 length + text: the upgrade URLs).
struct MembershipSetting {
    std::uint32_t level = 0;
    std::uint32_t feature = 0;
    std::int32_t value = 0;
};

struct MembershipDetails {
    std::vector<MembershipSetting> settings;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> races, classes; // {purchase id, bits}
    std::string url, url2;
};

// OP_GuildsList: 64 zero bytes, u32 count, then {u32 id, cstring name}.
struct GuildEntry {
    std::uint32_t id = 0;
    std::string name;
};

// Character-selection preview. The RoF2 codec owns the variable-length
// name prefix and version-specific fixed binary tail.
struct CharSelectEquip {
    std::uint32_t material = 0, unknown1 = 0, elite_material = 0, hero_forge_model = 0, material2 = 0;
    std::uint32_t color = 0; // Tint_Struct: B, G, R, use-tint
};

struct CharacterPreview {
    std::string name;
    std::uint8_t class_ = 0;
    std::uint32_t race = 0;
    std::uint8_t level = 0;
    std::uint8_t shroud_class = 0;
    std::uint32_t shroud_race = 0;
    std::uint16_t zone = 0;
    std::uint16_t instance = 0;
    std::uint8_t gender = 0;
    std::uint8_t face = 0;
    std::array<CharSelectEquip, 9> equip{}; // head, chest, arms, wrist, hands, legs, feet, primary, secondary
    std::uint8_t texture = 0xff, helm = 0xff;
    std::uint32_t drakkin_tattoo = 0, drakkin_details = 0;
    std::uint32_t deity = 0;
    std::uint32_t primary_id_file = 0, secondary_id_file = 0;
    std::uint8_t hair_color = 0, beard_color = 0, eye_color1 = 0, eye_color2 = 0, hair_style = 0, beard = 0;
    std::uint8_t go_home = 0, tutorial = 0;
    std::uint32_t drakkin_heritage = 0;
    std::uint8_t unknown1 = 0;
    std::uint8_t enabled = 0;
    std::uint32_t last_login = 0;
    std::uint8_t unknown2 = 0;
};


// OP_SetChatServer / OP_SetChatServer2: "host,port,server.Character,<type><key>".
struct ChatServerInfo {
    std::string host;
    std::uint16_t port = 0;
    std::string server_short_name;
    std::string character;
    char connection_type = 0; // EQ::versions::ucs* letter
    std::string mail_key;     // 8 hex digits
};

// OP_ZoneServerInfo (130 bytes): ip[128] text, u16 port (little-endian).
struct ZoneServerInfo {
    std::string host;
    std::uint16_t port = 0;
};


} // namespace hailstone::net
