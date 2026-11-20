#pragma once

// Bug reports saved for the player to post (the Discord bug report forum): bug-reports/hailstone-bug-<date>.zip from
// the bug report window (/bugreport) and bug-reports/hailstone-crash-<date>.zip, made on the start after a crash
// from the crash handler's logs/crash-<date>.txt (platform/crash_log.h). Each holds report.txt (the player's words,
// the build, the system, the character and where it stood), summary.md (a forum post to paste), the crash report,
// the logs, hailstone.ini and the screenshots (the one taken when the window opened, then the player's own from the
// minutes before), cut to fit Discord's upload limit: what did not fit is named in report.txt. Every text in it
// has the player's account names and home folder taken out (redact_private()), and the logs the words of every chat
// line (redact_chat(): tells, guild and group chat), since the report is posted in public.

#include <cstddef>
#include <ctime>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hailstone::app {

// Under Discord's 10 MiB upload limit for an account without Nitro, with room to spare.
inline constexpr std::size_t kBugReportLimit = 9'500'000;
// The player's screenshots this long before the report go in.
inline constexpr std::time_t kBugReportScreenshotWindow = 15 * 60;
// The folder keeps this many reports; older ones are deleted.
inline constexpr std::size_t kBugReportsKept = 30;

struct BugReport {
    std::filesystem::path file;       // the zip to write (again); empty: a new one in the folder
    bool crash = false;
    std::time_t time = 0;
    std::string category;             // "Crash" for a crash
    std::vector<std::string> flags;   // the window's ticked boxes ("Can be repeated", ...)
    std::string text;                 // the player's words
    // Labelled facts, in order ("Build", "hailstone 0.1.0 (f6a07d7)"); an empty value is left out.
    std::vector<std::pair<std::string, std::string>> facts;
    std::vector<std::filesystem::path> crash_files;  // crash-<date>.txt (and a Windows .dmp)
    std::vector<std::filesystem::path> logs;         // most wanted first
    std::vector<std::filesystem::path> settings;     // hailstone.ini ...: password, token and secret lines blanked
    std::filesystem::path capture;                   // the screenshot taken when the window opened (may be empty)
    // That screenshot's bytes, read at Send: when set, `capture` is not read (the file is the next window's to
    // delete and write again while this report is still being saved).
    std::optional<std::string> capture_png;
    std::filesystem::path screenshots;               // the player's screenshots folder (may be empty)
    // Taken out of every text in the archive (redact_private()): the player's login account names, and the home
    // folder (on Linux, its name is the player's system account name).
    std::vector<std::string> accounts;
    std::filesystem::path home;
};

struct SavedBugReport {
    std::filesystem::path zip;
    std::size_t size = 0;
    std::string summary;                 // summary.md
    std::vector<std::string> left_out;   // what did not fit
};

// Writes the report into `dir` (created), or over report.file, keeping it within `limit` bytes; nullopt when it
// cannot be written.
std::optional<SavedBugReport> save_bug_report(const BugReport& report, const std::filesystem::path& dir,
                                              std::size_t limit = kBugReportLimit);

// save_bug_report() on a thread of its own, for a caller that must not wait (the bug report window): `report` is
// already complete, and nothing of the caller's is touched.
std::future<std::optional<SavedBugReport>> save_bug_report_async(BugReport report, std::filesystem::path dir,
                                                                 std::size_t limit = kBugReportLimit);

// The pieces, for tests.
// "hailstone-bug-YYYYMMDD-HHMMSS.zip" ("hailstone-crash-..." for a crash), local time; -<n> for n > 1.
std::string bug_report_file_name(bool crash, const std::tm& local, int n = 1);
// report.txt.
std::string bug_report_text(const BugReport& report, const std::vector<std::string>& included,
                            const std::vector<std::string>& left_out);
// summary.md: a forum post (a title line, then the facts and the player's words), at most 1900 characters so it
// fits in one Discord message.
std::string bug_report_summary(const BugReport& report, std::string_view zip_name);
// An ini file with the values of keys naming a password, token, secret or session blanked.
std::string scrub_settings(std::string_view text);
// `text` with each of `accounts` (any case, as a whole word) written <account>, the folder `home` (any case, either
// separator) written ~, and home's own name, as a folder name elsewhere (/run/media/<name>/), written <user>.
std::string redact_private(std::string_view text, const std::vector<std::string>& accounts,
                           const std::filesystem::path& home);
// `text` (a log) with the words of each chat line ("[info] [file:line, function] chat: ...") taken out, the line itself kept.
std::string redact_chat(std::string_view text);
// A crash report (platform/crash_log.cpp) with redact_chat() on its last log lines only: the stack written after
// them ("\nstack:\n" and the frames, no level's tag) is never taken for a chat line's continuation.
std::string redact_crash_report(std::string_view text);
// Deletes all but the newest `keep` hailstone-*.zip in `dir`.
void prune_bug_reports(const std::filesystem::path& dir, std::size_t keep = kBugReportsKept);
// The crash reports in `logs` not bundled yet (crash-*.txt, oldest first).
std::vector<std::filesystem::path> unreported_crashes(const std::filesystem::path& logs);
// Marks a crash report bundled: crash-<date>.txt becomes crash-<date>.reported.txt (and its .dmp the same).
void mark_crash_reported(const std::filesystem::path& crash);

} // namespace hailstone::app
