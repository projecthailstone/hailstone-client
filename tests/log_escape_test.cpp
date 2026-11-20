// core/log.h escape_controls: a message's control characters cannot make a log entry of their own or cut one short.
#include <doctest/doctest.h>

#include <format>
#include <string>
#include <string_view>

#include "core/log.h"

using hailstone::log::escape_controls;

TEST_CASE("log: control characters are escaped, the rest left as it is") {
    std::string scratch;
    // The common case is the message itself, with nothing allocated.
    const std::string_view plain = "zone: entered qeynos \xe2\x80\x94 C:\\EQ\\eqgame\tok";
    CHECK(escape_controls(plain, scratch).data() == plain.data());
    CHECK(scratch.empty());

    using namespace std::string_view_literals;
    // A line break goes on indented, so the next line cannot pass for an entry of its own.
    CHECK(escape_controls("web: refused http://a\n[info] chat: forged"sv, scratch) ==
          "web: refused http://a\n  [info] chat: forged");
    CHECK(escape_controls("a\r\nb\rc\n"sv, scratch) == "a\n  b\\rc\n  ");
    CHECK(escape_controls("lua: x.lua:3: oops\nstack traceback:\n\t[C]: in ?"sv, scratch) ==
          "lua: x.lua:3: oops\n  stack traceback:\n  \t[C]: in ?");
    CHECK(escape_controls("cut\0here"sv, scratch) == "cut\\x00here");
    CHECK(escape_controls("\x1b[31mred\x7f"sv, scratch) == "\\x1b[31mred\\x7f");
    CHECK(escape_controls("\xc3\xa9t\xc3\xa9\x01"sv, scratch) == "\xc3\xa9t\xc3\xa9\\x01"); // UTF-8 stays
}

TEST_CASE("log: home usernames are removed without changing surrounding log content") {
    using hailstone::log::redact_home_paths;
    std::string scratch;
    const std::string_view plain = "log: a relative/path is untouched";
    CHECK(redact_home_paths(plain, scratch).data() == plain.data());
    CHECK(scratch.empty());

    CHECK(redact_home_paths("data folder /home/Alice/.local/share/hailstone", scratch) ==
          "data folder /home/redacted/.local/share/hailstone");
    scratch.clear();
    CHECK(redact_home_paths("user /home/Bob, mac /Users/Carol/Library, win C:\\Users\\Dave\\AppData", scratch) ==
          "user /home/redacted, mac /Users/redacted/Library, win C:\\Users\\redacted\\AppData");
    scratch.clear();
    CHECK(redact_home_paths("file /var/home/Eve/.config and /home/Frank", scratch) ==
          "file /var/home/redacted/.config and /home/redacted");
    scratch.clear();
    CHECK(redact_home_paths("/home/../not-an-account and /home//empty", scratch) ==
          "/home/../not-an-account and /home//empty");
}

TEST_CASE("log: redaction happens before crash-report lines are captured") {
    hailstone::log::write_at(hailstone::log::Level::Info,
                             "data folder /home/SecretUser/.local/share/hailstone-client/",
                             {"/home/Builder/source/game_connection.cpp", 123, "start"});
    char recent[16 * 200 + 1];
    const std::size_t count = hailstone::log::copy_recent_lines(recent, sizeof recent);
    const std::string_view logged(recent, count);
    CHECK(logged.find("[info] [game_connection.cpp:123, start] data folder "
                      "/home/redacted/.local/share/hailstone-client/") != std::string_view::npos);
    CHECK(logged.find("SecretUser") == std::string_view::npos);
    CHECK(logged.find("Builder") == std::string_view::npos);
}

TEST_CASE("log: a message with a line break goes on indented in the crash report's recent lines") {
    using namespace std::string_view_literals;
    hailstone::log::write(hailstone::log::Level::Error, "log_escape_test: one\ntwo\0three"sv);
    char recent[16 * 200 + 1];
    const std::size_t n = hailstone::log::copy_recent_lines(recent, sizeof recent);
    const std::string_view lines(recent, n);
    CHECK(lines.find("[error] [log_escape_test.cpp:") != std::string_view::npos);
    CHECK(lines.find("] log_escape_test: one\n  two\\x00three\n") != std::string_view::npos);
}

namespace {
void logged_from_wrapper(int n) {
    hailstone::log::warn("wrapper: value={:#06x}", n);
}

std::string recent() {
    char lines[16 * 200 + 1];
    const std::size_t n = hailstone::log::copy_recent_lines(lines, sizeof lines);
    return {lines, n};
}
} // namespace

TEST_CASE("log: C++ formatted call sites and logger wrappers retain source and message") {
    const auto at = __LINE__ + 1;
    hailstone::log::info("chat: You crush {} for {} points of damage.", "orc centurion", 1867);
    const auto written = recent();
    CHECK(written.find(std::format("[info] [log_escape_test.cpp:{}, ", at)) != std::string::npos);
    CHECK(written.find("] chat: You crush orc centurion for 1867 points of damage.\n") != std::string::npos);
    logged_from_wrapper(42);
    CHECK(recent().find("[warn] [log_escape_test.cpp:") != std::string::npos);
    CHECK(recent().find(", logged_from_wrapper] wrapper: value=0x002a\n") != std::string::npos);
    const auto callback = [] { hailstone::log::info("lambda site"); };
    callback();
    CHECK(recent().find("[info] [log_escape_test.cpp:") != std::string::npos);
    const std::string lambda_line = recent();
    const bool named_lambda = lambda_line.find(", <lambda()>] lambda site\n") != std::string::npos;
    const bool named_operator = lambda_line.find(", operator()] lambda site\n") != std::string::npos;
    const bool has_correct_call_site = named_lambda || named_operator;
    CHECK(has_correct_call_site);
}

TEST_CASE("log: source-less entries, direct writes and path basenames") {
    using hailstone::log::Level;
    hailstone::log::write_at(Level::Warn, "zone codec: unavailable", {});
    CHECK(recent().find("[warn] [unknown:?, ?] zone codec: unavailable\n") != std::string::npos);
    hailstone::log::write_at(Level::Error, "plugin example: bad state", {"C:\\plugins\\example.cpp", 93, "run"});
    CHECK(recent().find("[error] [example.cpp:93, run] plugin example: bad state\n") != std::string::npos);
    const auto at = __LINE__ + 1;
    hailstone::log::write(Level::Info, "raw call");
    CHECK(recent().find(std::format("[info] [log_escape_test.cpp:{}, ", at)) != std::string::npos);
    CHECK(recent().find("] raw call\n") != std::string::npos);
}
