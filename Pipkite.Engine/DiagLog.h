#pragma once
// The detailed log (Settings > Keep a detailed log), off by default: the messages Pipkite receives, what it read and
// decided, the orders and MetaTrader's replies, Telegram and update events and what the user did in the window.
// Lines are queued and written by a background thread, so the signal path never waits on the disk. One file per day
// in <data>\logs, deleted after the message retention days and kept under a total size.
// Never pass passwords, login codes or keys: redacted() hides them in JSON.

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace sr::engine::diag {

struct Limits {
    std::uint64_t maxFileBytes = 20ull * 1024 * 1024;  // a busy day continues in pipkite-<day>-2.log
    std::uint64_t maxTotalBytes = 100ull * 1024 * 1024;
    int keepDays = 30;
};

// Starts writing to dir (created when needed); called again, it only updates the limits.
void start(const std::wstring& dir, const Limits& limits = {});
void stop();  // writes what is queued, then stops
bool on();    // check it before building an expensive line
void write(std::string_view area, std::string_view text);  // one line; nothing happens when the log is off
void flush(unsigned timeoutMs = 5000);  // returns once everything written so far is in the file

// Deletes log files older than keepDays, then the oldest while all of them pass maxTotalBytes (also while off).
void prune(const std::wstring& dir, const Limits& limits);
void removeFiles(const std::wstring& dir);  // Erase local history: the log holds message texts too

nlohmann::json redacted(const nlohmann::json& j);  // values of secret-looking keys replaced by "<hidden>"
std::string dump(const nlohmann::json& j, std::size_t max = 4000);  // one line, never throws on bad UTF-8
std::string quoted(std::string_view text);  // the whole text as one JSON string: quotes and newlines escaped

}  // namespace sr::engine::diag
