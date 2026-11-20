#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace hailstone::log {

enum class Level { Debug, Info, Warn, Error };

void set_level(Level level);
// Whether a line at `level` is written at the level set now.
bool enabled(Level level);
// A non-C++ call site (Lua or a native plugin). Empty fields and a zero line are displayed as "unknown" / "?".
// Names are views only for the duration of write_at(); the writer does not retain them.
struct Location {
    std::string_view file;
    std::uint_least32_t line = 0;
    std::string_view function;
};
void write_at(Level level, std::string_view message, Location site);
// Direct C++ callers also capture their own location, not the logging implementation's location.
void write(Level level, std::string_view message, std::source_location site = std::source_location::current());
// Also writes every line to `path` from now on (the client's logs/hailstone.log), as well as to stdout and stderr.
// The earlier runs' files are kept as <name>.1<ext> to <name>.3<ext> (hailstone.1.log is the last run's). A program
// that runs while another holds `path` (a second client; <name>.lock beside it is the lock) leaves it alone and
// writes <name>.<process id><ext> instead; such files are removed at a later open, but for the newest 5 and those
// written in the last day. A run's file stops at a size limit (100 MiB), with a last line saying so; the lines after
// it go to stdout and stderr only. False when the file cannot be created: the log then goes to stdout and stderr
// only.
bool open_file(const std::filesystem::path& path);
// The size at which the file stops (0: the default, 100 MiB); for tests.
void set_file_limit(std::uint64_t bytes);
// The file the log writes to (empty when none).
std::filesystem::path file_path();
void close_file();
// The last lines written (at most 16, each cut to 199 characters), oldest first, one per line, into `out` (always
// NUL-terminated); the length written. It takes no lock and allocates nothing, so a crash handler may call it
// (platform/crash_log.h); a line written meanwhile may come out mixed.
std::size_t copy_recent_lines(char* out, std::size_t size);
// The message as write() puts it: a line break ("\n" or "\r\n") goes on over a line indented two spaces, and the
// other control characters (C0 but tab, and DEL) are written \r or \xHH, so a server's text with a line break or a
// NUL in it cannot make a "[level] ..." line of its own or cut one short. UTF-8 is left as it is, and so is a
// backslash. Without a control character it is `message` itself (`scratch` is not touched, so
// nothing is allocated); else the escaped text, in `scratch`.
std::string_view escape_controls(std::string_view message, std::string& scratch);
// Replace the username component of standard Unix/macOS/Windows home paths with
// "redacted". A message without such a path is returned without copying it.
// Called after escape_controls() and before any logging sink, including the
// crash-report ring. `scratch` must not own `message`'s storage.
std::string_view redact_home_paths(std::string_view message, std::string& scratch);

// std::format_string checks formatting arguments at compile time. Its wrapper also captures the call site's
// std::source_location in its defaulted constructor argument, so existing log::info("{}", value) calls keep
// their spelling and their call sites. Do not put source_location::current() inside info()/warn(): that would
// report this header instead of the caller.
template <typename... Args> struct LocatedFormat {
    std::format_string<Args...> format;
    std::source_location site;

    template <std::size_t N>
    consteval LocatedFormat(const char (&fmt)[N], std::source_location where = std::source_location::current())
        : format(fmt), site(where) {}
};

// Debug messages are not formatted when disabled, including their format arguments.
template <typename... Args>
void debug(LocatedFormat<std::type_identity_t<Args>...> fmt, Args&&... args) {
    if (!enabled(Level::Debug)) return;
    write(Level::Debug, std::format(fmt.format, std::forward<Args>(args)...), fmt.site);
}
template <typename... Args>
void info(LocatedFormat<std::type_identity_t<Args>...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt.format, std::forward<Args>(args)...), fmt.site);
}
template <typename... Args>
void warn(LocatedFormat<std::type_identity_t<Args>...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt.format, std::forward<Args>(args)...), fmt.site);
}
template <typename... Args>
void error(LocatedFormat<std::type_identity_t<Args>...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt.format, std::forward<Args>(args)...), fmt.site);
}

} // namespace hailstone::log
