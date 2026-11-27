// The detailed log: one line per event, nothing written while off, secrets hidden, a new file when one is full, and
// old or excess log files deleted without touching anything else in the folder.
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Pipkite.Engine/DiagLog.h"
#include "TestFramework.h"

using namespace sr::engine;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const wchar_t* name) {
    fs::path p = fs::temp_directory_path() / (std::wstring(L"pk-diag-") + name + L"-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

std::vector<fs::path> logFiles(const fs::path& dir) {
    std::vector<fs::path> out;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == L".log") out.push_back(e.path());
    return out;
}

std::string readAll(const std::vector<fs::path>& files) {
    std::string all;
    for (auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        all += ss.str();
    }
    return all;
}

}  // namespace

TEST(diag_log_writes_one_line_per_event_and_nothing_while_off) {
    const fs::path dir = freshDir(L"lines");
    diag::write("message", "before the log is on");
    diag::start(dir.wstring());
    CHECK(diag::on());
    diag::write("message", "chat=-100 text=" + diag::quoted("XAUUSD BUY 4000\nSL 3990\nTP \"4010\""));
    diag::write("error", "two\nlines");
    diag::flush();
    diag::stop();
    CHECK(!diag::on());
    diag::write("message", "after the log is off");

    const std::string all = readAll(logFiles(dir));
    CHECK(all.find("detailed log on") != std::string::npos);
    CHECK(all.find(R"(text="XAUUSD BUY 4000\nSL 3990\nTP \"4010\"")") != std::string::npos);  // the whole message, on one line
    CHECK(all.find("two lines") != std::string::npos);
    CHECK(all.find("detailed log off") != std::string::npos);
    CHECK(all.find("before the log is on") == std::string::npos);
    CHECK(all.find("after the log is off") == std::string::npos);
    int lines = 0;
    for (char c : all) lines += c == '\n';
    CHECK_EQ(lines, 4);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(diag_log_hides_passwords_codes_and_keys) {
    const json params = {{"key", "PK-123"},
                         {"password", "hunter2"},
                         {"code", "12345"},
                         {"phone", "+15550100"},
                         {"apiHash", "0123456789abcdef0123456789abcdef"},
                         {"values", {{"license.key", "PK-456"}, {"telegram.apiHash", "abc"}, {"ui.theme", "dark"}, {"license.key2", "x"}}},
                         {"backupPassword", "p"},
                         {"text", "XAUUSD BUY 4000"},
                         {"chatId", "-100"},
                         {"cleared", {{"license.key", ""}}}};
    const std::string out = diag::dump(diag::redacted(params));
    for (const char* secret : {"PK-123", "hunter2", "12345", "+15550100", "0123456789abcdef", "PK-456", "\"abc\"", "\"p\""}) CHECK(out.find(secret) == std::string::npos);
    CHECK(out.find("XAUUSD BUY 4000") != std::string::npos);  // what support needs stays
    CHECK(out.find("\"ui.theme\":\"dark\"") != std::string::npos);
    CHECK(out.find("\"license.key2\":\"x\"") != std::string::npos);
    CHECK(out.find("\"cleared\":{\"license.key\":\"\"}") != std::string::npos);  // a cleared key shows as cleared
    CHECK(out.find("<hidden>") != std::string::npos);
    // long payloads are cut on a character boundary; bad UTF-8 never throws
    const std::string clipped = diag::dump(json(std::string(10, 'a') + "\xC3\xA9" + std::string(10, 'b')), 12);
    CHECK(clipped.find("more bytes)") != std::string::npos);
    CHECK(diag::quoted("bad \xFF byte").find("bad") != std::string::npos);
}

TEST(diag_log_continues_in_a_new_file_and_deletes_old_ones) {
    const fs::path dir = freshDir(L"rotate");
    {
        std::ofstream(dir / L"pipkite-2020-01-01.log") << "old\n";
        std::ofstream(dir / L"notes.txt") << "not a log\n";
    }
    fs::last_write_time(dir / L"pipkite-2020-01-01.log", fs::file_time_type::clock::now() - std::chrono::hours(24 * 40));
    fs::last_write_time(dir / L"notes.txt", fs::file_time_type::clock::now() - std::chrono::hours(24 * 40));
    diag::Limits limits;
    limits.maxFileBytes = 2048;
    limits.maxTotalBytes = 6 * 1024;
    limits.keepDays = 30;
    diag::start(dir.wstring(), limits);
    for (int i = 0; i < 200; ++i) {
        diag::write("message", std::string(80, 'x') + " " + std::to_string(i));
        if (i % 20 == 0) diag::flush();  // several batches, so the size check runs between them
    }
    diag::flush();
    diag::stop();

    CHECK(!fs::exists(dir / L"pipkite-2020-01-01.log"));  // past the retention days
    CHECK(fs::exists(dir / L"notes.txt"));               // never touches other files
    const auto files = logFiles(dir);
    CHECK(files.size() >= 2);  // a full file continues in pipkite-<day>-2.log
    std::uint64_t total = 0;
    for (auto& f : files) total += fs::file_size(f);
    CHECK(total <= limits.maxTotalBytes + limits.maxFileBytes + 200);  // the oldest parts go first; the open one may pass by a line
    CHECK(readAll(files).find(" 199\n") != std::string::npos);  // the newest lines are kept

    diag::removeFiles(dir.wstring());  // Erase local history
    CHECK(logFiles(dir).empty());
    CHECK(fs::exists(dir / L"notes.txt"));
    std::error_code ec;
    fs::remove_all(dir, ec);
}
