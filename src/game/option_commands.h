#pragma once

// The original's client option commands as rules, without the client around them (the option, status,
// setting, screen, /usercolor, hot button, window, world and /shadows commands). The connection
// (net/connection/game_connection_options.cpp) and the app (app/client_app_commands.cpp) say what they decide and keep the
// settings. Strings are eqstr ids of eqstr_us.txt unless they are the exe's own text, which is given here.

#include <array>
#include <cstdint>
#include <ctime>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hailstone::game {

// ---------------------------------------------------------------- shared word tests

// IsNumericString @0x4ddd90: an optional + or -, then digits only to the end; empty is not numeric (a lone sign is).
bool is_numeric_string(std::string_view text);
// Utf8StrNICmp(a, b, n) == 0: the first n characters without case, a string's end counting as a character.
bool starts_no_case(std::string_view a, std::string_view b, std::size_t n);
// StringCompareNoCase / __stricmp == 0.
bool same_no_case(std::string_view a, std::string_view b);
// _atoi: leading spaces, a sign, digits.
std::int32_t eq_atoi(std::string_view text);
// A file name a command may look up in its own folder (/copylayout's ini): not empty, not "." or "..", and no
// '/', '\\', ':' (a drive or a stream) or NUL, so it names no other folder, absolute or relative.
bool bare_file_name(std::string_view name);

// The "off" (Utf8StrNICmp, 3 characters) / "on" (2) / anything else toggles shape of /secondslog, /invwinlabels,
// /dynamiclights and /log. `off` and `on` are eqstr 12946 / 12947 (the exe's "off" / "on" for the first two: the
// same text).
enum class Switch : std::uint8_t { Off, On, Toggle };
Switch off_on_toggle(std::string_view args, std::string_view off, std::string_view on);

// ---------------------------------------------------------------- /quit, /exit, /safelock

inline constexpr std::uint32_t kStrInterfaceLocked = 12095; // "Interface locked.  Please use /safelock to unlock it."

// CEverQuest::InterpretCmd @0x51fce0 while the interface is locked (the safe-lock password 0xe15dac is set): a ';' or
// '[' line, and any line whose first word does not start with the first four characters of "/safelock" (eqstr 14193,
// CXStr::CompareN with case), is refused with 12095.
bool locked_line_allowed(std::string_view line, std::string_view safelock_word);

// Cmd_safelock @0x4ead80: the password (at most 0x1f characters). Returns the lines to show; `password` changes.
std::vector<std::uint32_t> safelock_command(std::string& password, std::string_view args);

// __ExecuteCmd @0x4d7230's key commands that need control of the player: its flag is clear while the interface is
// locked, the PcBase's +0xfc is above 2 or the player is frozen (StandState 0x66), and set again by the switches
// 0xddf654 / 0xddf790. Clear, these commands do nothing (no message), their releases included (the movement keys
// stay held as they were). The rest still act while locked: the window toggles (but the Actions window's four keys
// and the inventory, spell book, skills, potion belt, bandolier, bag, combat ability, find and extended target
// windows), HAIL, TELL, REPLY, the camera's pitch, zoom and clip keys, the extended target keys and the chat keys.
// Which ones: the case each command has in ExecuteCmd_KeyCmdToCase @0x4db5f8 and whether that case tests the flag.
bool key_command_needs_control(int command);

// __ProcessMouseEvents @0x539e60 while the interface is locked: no mouse reaches the windows or the world (wheel and
// mouse look included). ProcessMouseInput, which reads the buttons, is not called then, so the button flags
// (0xddf73c / 0xddf73d, left and right) keep what they had when the lock began; while either is set, every frame
// prints 12095 (colour 0x111). ResetClientStateForZone clears the flags, CDisplay::StartWorldDisplay the lock.
inline constexpr std::int32_t kLockedColor = 0x111;

// ---------------------------------------------------------------- /charinfo

// Cmd_charinfo @0x4df330: "You are currently bound in: " (the exe's text) and the full names of the bind points 0..4
// that are set: the first appended as it is, each later one as ", name" unless the one before it has the same zone
// (that earlier one counts even when unset: an unset bind 0 before a set bind 1 gives ": , name").
std::string charinfo_line(std::span<const std::uint32_t> bind_zones, const std::function<std::string(std::uint32_t)>& full_zone_name);

// ---------------------------------------------------------------- /log, /secondslog

inline constexpr std::uint32_t kStrLogOn = 13221, kStrLogOff = 13222; // "Logging to '%1.txt' is now *ON*." / *OFF*
struct LogCommand {
    bool on = false;
    std::string name; // the log's name: "eqlog", or the second word when starting with one
};
// Cmd_log @0x4fc230: the name goes back to "eqlog"; "off" stops, "on" starts, anything else toggles; starting takes
// the second word as the name when there is one.
LogCommand log_command(std::string_view args, std::string_view off, std::string_view on, bool logging);
// __ctime32's text without its newline, in local time: "Www Mmm dd hh:mm:ss yyyy".
std::string ctime_text(std::time_t t);
// CEverQuest::OutputTextToLog @0x5130c0: "[<ctime>] text\n" (ctime's newline turned into "] ").
std::string log_line(std::time_t now, std::string_view text);

// ---------------------------------------------------------------- /shownames

inline constexpr std::uint32_t kStrShowNamesUsage = 13298, kStrShowNamesOff = 13293;
struct ShowNames {
    bool usage = false; // no word: 13298 alone, nothing changes
    bool off = false;   // names hidden (ShowNamesLevel 0, PCNames 0)
    int level = 0;      // 1..6 (__ShowNames)
    std::uint32_t eqstr = 0;
};
// Cmd_shownames @0x4eb0f0: "off" (3 characters) hides the names (13293); a level 1..6 (anything else counts as 4)
// shows them at that level (13294..13297, 8874, 8875). Colour 0x111.
ShowNames shownames_command(std::string_view args, std::string_view off);

// ---------------------------------------------------------------- /usercolor

inline constexpr int kUserColorUsage = 0; // the help pages of usercolor_help_lines: 0 the usage, 1..7 page1..page7
struct UserColor {
    std::optional<int> page; // a help page to show (and nothing else)
    bool reset = false;      // "<index> reset": the default colour (__SaveColors)
    std::int32_t index = -1, red = -1, green = -1, blue = -1; // Set: index 0..99 (SetUserDefinedColor(index - 1))
};
// Cmd_usercolor @0x4e9b60: no argument, or anything out of range, gives the usage page; "help" / "page1".."page7" a
// page; "<index> reset" or "<index> <r> <g> <b>" (each only when numeric) sets a colour. The index is 0..99 and each
// channel 0..255 (out of range: the usage). Colour 0x111 throughout.
UserColor usercolor_command(std::string_view args);
std::span<const char* const> usercolor_help_lines(int page);

// ---------------------------------------------------------------- /hotbutton

inline constexpr std::uint32_t kStrHotButtonUsage = 13278; // 13278..13281
inline constexpr std::uint32_t kStrHotButtonMade = 13282, kStrHotButtonFailed = 13283;
struct HotButtonCommand {
    bool usage = false;
    std::string name, line;
    std::int32_t color = 0;
};
// Cmd_hotbutton @0x4f79b0: "<name> [colour] <text>": the second word is the colour when it is all digits (an empty
// one too: 0), else it starts the text. The social is stored by 0x4ea9f0(name, text, colour, -1, 1).
HotButtonCommand hotbutton_command(std::string_view args);

// ---------------------------------------------------------------- /viewport

inline constexpr std::uint32_t kStrViewportUsage = 15008;
struct Viewport {
    // 0xddf620: x, y, right, bottom, then the x / y / w / h percentages of the screen (all 0: not scaled).
    std::array<std::int32_t, 8> v{0, 0, 0, 0, 0, 0, 0, 0};
    bool scaled() const { return v[4] || v[5] || v[6] || v[7]; }
};
// Cmd_viewport @0x4eb380: "reset" anywhere makes it the whole screen at 100%; "<x> <y> <w> <h> [scale]" sets it (x,
// y at least 0, w, h at least 1, clamped to the screen); nullopt: anything else (the current one is shown).
std::optional<Viewport> viewport_command(std::string_view args, std::int32_t screen_w, std::int32_t screen_h);
// "x=%d y=%d w=%d h=%d scale=%d" (scale 1 when any percentage is set).
std::string viewport_line(const Viewport& v);
// The ViewPort section's keys and values (X Y W H XScale YScale WScale HScale).
std::array<std::pair<const char*, std::int32_t>, 8> viewport_ini(const Viewport& v);
// The 3D view's rectangle on a screen_w x screen_h screen from a kept viewport (CEverQuest::ResetUIForResolution
// @0x4eb860, the ViewPort section read back): each of x, y, w, h is the screen's percentage when its scale is set,
// else the kept value; then w outside 1..screen_w is screen_w, x below 0 is 0 and x + w past the screen moves x back
// (y and h alike).
struct ViewRect {
    std::int32_t x = 0, y = 0, w = 0, h = 0;
};
ViewRect viewport_rect(const Viewport& v, std::int32_t screen_w, std::int32_t screen_h);
// display.viewport's text ("x y right bottom xscale yscale wscale hscale", Viewport::v) and back (nullopt: none kept,
// the whole screen).
std::string viewport_text(const Viewport& v);
std::optional<Viewport> parse_viewport(std::string_view text);

// ---------------------------------------------------------------- /particledensity

inline constexpr const char* kParticleDensityUsage = "Please choose either OFF, LOW, MED, or HIGH.";
// Cmd_particledensity @0x4ea740: 0..3, or OFF / LOW / MED / HIG(H) by their first 3 characters; nullopt: the usage.
std::optional<int> particle_density(std::string_view args);
// "Particle Cloud Density is currently: %s" with the argument as typed.
std::string particle_density_line(std::string_view args);
// A density level's value (Cmd_particledensity_Set @0x489cd0, and the Options window's density combos, SetDensity):
// 0, 0.25, 0.5, 1 for levels 0..3 (anything else 1). /particledensity sets EnvironmentParticleDensity with it (the
// effect manager's virtual +0xa4; saved under [Defaults]).
float particle_density_value(int level);
// The combo choice SyncDisplayPage shows for a density (DensityChoice): <= 0 -> 0, <= 0.25 -> 1, <= 0.5 -> 2, else
// (NaN too) 3.
int particle_density_choice(float density);

// ---------------------------------------------------------------- /shadows

// The shadow clip plane (eqclient.ini [Options] ShadowClipPlane, 0..100, default 50; /shadows on <n>, the advanced
// display window's slider) as the distance CDisplay_SetShadowClipPlane gives the shadows: n x 5 + 10 units.
inline constexpr std::int32_t kDefaultShadowClipPlane = 50;
inline float shadow_clip_distance(std::int32_t plane) { return static_cast<float>(plane * 5 + 10); }

// ---------------------------------------------------------------- tracking

inline constexpr std::uint32_t kStrRangersOnly = 13155;
// Cmd_trackfilter @0x4f4cd0's options: the first one or two letters (lower case) or the whole name pick a con bit.
struct TrackFilterChoice {
    std::uint32_t bit = 0;
    std::uint32_t name = 0; // eqstr of the colour's name
};
using EqStrFn = std::function<std::string(std::uint32_t id)>;
std::optional<TrackFilterChoice> trackfilter_choice(std::string_view args, const EqStrFn& eqstr);
// Cmd_tracksort @0x4f4930's options in order (0 normal .. 6 reverse consider): the first letter in either case ('R'
// and a second letter for the reversed ones), else the whole name; the message and the ini value.
struct TrackSortChoice {
    std::uint32_t type = 0;
    std::uint32_t message = 0;
    const char* ini = "";
};
std::optional<TrackSortChoice> tracksort_choice(std::string_view args, const EqStrFn& eqstr);
// The ini values loadOptions reads back (CEverQuest's constructor): TrackPlayers T/t/1 on, G/g/2 group, else off
// (missing: on); TrackPets / TrackMercs T/t/1 (missing: on); TrackSortType by its first letter (N/n/0 0, A/a/1 1,
// D/d/2 2, C/c/3 3, R/r with A / D / C 4 / 5 / 6, '4'..'6'; missing or anything else 0).
std::int32_t track_players_value(const std::optional<std::string>& text);
bool track_toggle_value(const std::optional<std::string>& text);
std::uint32_t track_sort_value(const std::optional<std::string>& text);

// ---------------------------------------------------------------- sound and mouse

inline constexpr std::uint32_t kStrMouseSpeedUsage = 13165, kStrMouseSpeed = 13167; // + 13166 after the usage
inline constexpr std::uint32_t kStrMixAheadUsage = 7528, kStrMixAhead = 7529;
// Cmd_mixahead @0x4f5230: 1..100 (atoi).
std::optional<std::int32_t> mixahead_value(std::string_view args);

// ---------------------------------------------------------------- /netstats

// Cmd_netstats @0x4e6a70's lines (UdpConnection::GetStats): blank, "\nPing Times" (colour 0xf), the six ping times
// ("&NBSP;&NBSP;&NBSP;master=%ld" ...), blank, "\nPacket Loss" (0xf), sent / received as (1 - success) * 100 with
// "%0.2f%%\n", blank; the other lines colour 0xa.
struct NetStats {
    std::int32_t master = 0, average = 0, low = 0, high = 0, last = 0, reliable_average = 0;
    float sent_success = 1.0f, received_success = 1.0f; // GetStats +0xb0 / +0xb4 (1 until the first counts)
};
struct ChatOut {
    std::string text;
    std::uint32_t color = 0;
};
std::vector<ChatOut> netstats_lines(const NetStats& s);

} // namespace hailstone::game
