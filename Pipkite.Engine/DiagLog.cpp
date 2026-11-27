#include "DiagLog.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "Pipkite.Core/Models.h"

namespace sr::engine::diag {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxQueued = 50000;  // a stuck disk drops lines (and says how many) instead of using up memory

struct Log {
    std::atomic<bool> on{false};
    std::mutex mu;  // queue, sequence numbers, writer state
    std::condition_variable wake, written;
    std::deque<std::string> queue;
    std::size_t dropped = 0;
    std::uint64_t queuedSeq = 0, writtenSeq = 0;
    bool stopping = false;
    std::thread writer;

    std::mutex fileMu;  // the open file; held by the writer while it writes a batch
    std::wstring dir;
    Limits limits;
    std::FILE* f = nullptr;
    std::wstring path;
    std::string day;  // YYYY-MM-DD of the open file
    std::uint64_t bytes = 0;

    ~Log() {
        {
            std::lock_guard lock(mu);
            on = false;
            stopping = true;
        }
        wake.notify_all();
        if (writer.joinable()) writer.join();
    }
};

Log& theLog() {
    static Log l;
    return l;
}

std::string stamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02u %02u:%02u:%02u.%03u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

std::string utcOffset() {
    SYSTEMTIME lt, ut;
    GetLocalTime(&lt);
    GetSystemTime(&ut);
    FILETIME lf, uf;
    SystemTimeToFileTime(&lt, &lf);
    SystemTimeToFileTime(&ut, &uf);
    auto ticks = [](const FILETIME& f) { return static_cast<long long>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime); };
    const long long mins = std::llround(static_cast<double>(ticks(lf) - ticks(uf)) / 600000000.0);  // 100 ns units
    char buf[16];
    std::snprintf(buf, sizeof buf, "UTC%c%02lld:%02lld", mins < 0 ? '-' : '+', std::llabs(mins) / 60, std::llabs(mins) % 60);
    return buf;
}

bool isLogFile(const fs::path& p) {
    const std::wstring n = p.filename().wstring();
    return n.rfind(L"pipkite-", 0) == 0 && p.extension() == L".log";
}

std::wstring fileFor(const std::wstring& dir, const std::string& day, int part) {
    return dir + L"\\pipkite-" + std::wstring(day.begin(), day.end()) + (part > 1 ? L"-" + std::to_wstring(part) : L"") + L".log";
}

void pruneLocked(const std::wstring& dir, const Limits& limits, const std::wstring& openPath) {
    struct File {
        fs::path p;
        fs::file_time_type at;
        std::uint64_t size;
    };
    std::vector<File> files;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::error_code fec;
        if (!e.is_regular_file(fec) || !isLogFile(e.path())) continue;
        files.push_back({e.path(), e.last_write_time(fec), e.file_size(fec)});
    }
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.at < b.at; });
    std::uint64_t total = 0;
    for (auto& f : files) total += f.size;
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24LL * std::max(1, limits.keepDays));
    for (auto& f : files) {  // oldest first
        if (!openPath.empty() && fs::equivalent(f.p, openPath, ec)) continue;
        if (f.at >= cutoff && total <= limits.maxTotalBytes) continue;
        std::error_code rec;
        if (fs::remove(f.p, rec)) total -= f.size;
    }
}

// writer thread, fileMu held: today's first file that isn't full, appended to after a restart
void openFor(Log& L, const std::string& day) {
    if (L.f) std::fclose(L.f);
    L.f = nullptr;
    std::error_code ec;
    fs::create_directories(L.dir, ec);
    const std::uint64_t max = std::max<std::uint64_t>(1024, L.limits.maxFileBytes);
    for (int part = 1;; ++part) {
        const std::wstring p = fileFor(L.dir, day, part);
        std::error_code sec;
        const std::uint64_t size = fs::exists(p, sec) ? fs::file_size(p, sec) : 0;
        if (sec || size < max) {
            L.path = p;
            L.bytes = sec ? 0 : size;
            break;
        }
    }
    L.f = _wfopen(L.path.c_str(), L"ab");
    L.day = day;
    if (L.f) pruneLocked(L.dir, L.limits, L.path);
}

void writeBatch(Log& L, std::deque<std::string>& batch) {
    std::lock_guard lock(L.fileMu);
    for (auto& line : batch) {
        const std::string day = line.substr(0, 10);
        if (!L.f || day != L.day || L.bytes >= L.limits.maxFileBytes) openFor(L, day);
        if (!L.f) continue;  // can't open the file now (locked, disk full): try again with the next line
        std::fwrite(line.data(), 1, line.size(), L.f);
        L.bytes += line.size();
    }
    if (L.f) std::fflush(L.f);  // in the file even if the engine crashes right after
}

void run(Log& L) {
    for (;;) {
        std::deque<std::string> batch;
        std::uint64_t seq = 0;
        {
            std::unique_lock lock(L.mu);
            L.wake.wait(lock, [&] { return L.stopping || !L.queue.empty(); });
            if (L.queue.empty()) break;  // stopping, and everything is written
            batch.swap(L.queue);
            if (L.dropped) batch.push_front(stamp() + " log      " + std::to_string(std::exchange(L.dropped, 0)) + " lines were dropped (the disk was too slow)\n");
            seq = L.queuedSeq;
        }
        writeBatch(L, batch);
        {
            std::lock_guard lock(L.mu);
            L.writtenSeq = seq;
        }
        L.written.notify_all();
    }
    std::lock_guard lock(L.fileMu);
    if (L.f) std::fclose(L.f);
    L.f = nullptr;
    L.path.clear();
    L.day.clear();
}

void enqueue(Log& L, std::string&& line) {
    {
        std::lock_guard lock(L.mu);
        if (!L.on.load()) return;  // stopped meanwhile
        if (L.queue.size() >= kMaxQueued) {
            ++L.dropped;
            return;
        }
        L.queue.push_back(std::move(line));
        ++L.queuedSeq;
    }
    L.wake.notify_one();
}

}  // namespace

void start(const std::wstring& dir, const Limits& limits) {
    Log& L = theLog();
    {
        std::lock_guard fl(L.fileMu);
        L.limits = limits;
        if (L.on.load() && L.dir == dir) return;
    }
    stop();  // another folder (tests): finish the current file first
    {
        std::lock_guard fl(L.fileMu);
        L.dir = dir;
    }
    {
        std::lock_guard lock(L.mu);
        L.stopping = false;
        L.on = true;
        L.writer = std::thread([&L] { run(L); });
    }
    write("log", std::string("detailed log on: ") + kProductName + " " + kProductVersion + ", times are local (" + utcOffset() + ")");
}

void stop() {
    Log& L = theLog();
    if (L.on.load()) write("log", "detailed log off");
    {
        std::lock_guard lock(L.mu);
        L.on = false;
        L.stopping = true;
    }
    L.wake.notify_all();
    if (L.writer.joinable()) L.writer.join();
}

bool on() { return theLog().on.load(std::memory_order_relaxed); }

void write(std::string_view area, std::string_view text) {
    Log& L = theLog();
    if (!L.on.load(std::memory_order_relaxed)) return;
    std::string line = stamp();
    line += ' ';
    line.append(area);
    if (area.size() < 8) line.append(8 - area.size(), ' ');
    line += ' ';
    const std::size_t from = line.size();
    line.append(text);
    for (std::size_t i = from; i < line.size(); ++i)  // one event per line
        if (line[i] == '\n' || line[i] == '\r') line[i] = ' ';
    line += '\n';
    enqueue(L, std::move(line));
}

void flush(unsigned timeoutMs) {
    Log& L = theLog();
    std::unique_lock lock(L.mu);
    const std::uint64_t target = L.queuedSeq;
    L.written.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return L.writtenSeq >= target || !L.writer.joinable(); });
}

void prune(const std::wstring& dir, const Limits& limits) {
    Log& L = theLog();
    std::lock_guard fl(L.fileMu);
    pruneLocked(dir, limits, L.path);
}

void removeFiles(const std::wstring& dir) {
    Log& L = theLog();
    std::lock_guard fl(L.fileMu);
    if (L.f) std::fclose(L.f);
    L.f = nullptr;  // the next line starts a new file
    L.day.clear();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::error_code rec;
        if (e.is_regular_file(rec) && isLogFile(e.path())) fs::remove(e.path(), rec);
    }
}

namespace {

bool secretName(std::string name) {
    if (auto dot = name.rfind('.'); dot != std::string::npos) name = name.substr(dot + 1);  // license.key -> key
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::set<std::string> exact{"code", "phone", "key", "apiid", "apihash", "proof", "nonce", "apikey", "licensekey", "authorization"};
    return exact.count(name) > 0 || name.find("password") != std::string::npos || name.find("secret") != std::string::npos || name.find("token") != std::string::npos;
}

}  // namespace

json redacted(const json& j) {
    if (j.is_object()) {
        json out = json::object();
        for (auto& [k, v] : j.items()) {
            const bool empty = v.is_null() || (v.is_string() && v.get_ref<const std::string&>().empty());
            out[k] = secretName(k) && !empty ? json("<hidden>") : redacted(v);  // an empty value stays visible: "it was cleared"
        }
        return out;
    }
    if (j.is_array()) {
        json out = json::array();
        for (auto& v : j) out.push_back(redacted(v));
        return out;
    }
    return j;
}

std::string dump(const json& j, std::size_t max) {
    std::string s = j.dump(-1, ' ', false, json::error_handler_t::replace);
    if (s.size() <= max) return s;
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;  // never split a UTF-8 character
    const std::size_t more = s.size() - cut;
    s.resize(cut);
    return s + "...(" + std::to_string(more) + " more bytes)";
}

std::string quoted(std::string_view text) { return dump(json(std::string(text)), std::string::npos); }

}  // namespace sr::engine::diag
