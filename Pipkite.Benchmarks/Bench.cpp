// benchmarks, prints a markdown report (p50/p95/p99/max).
// usage: Pipkite.Benchmarks.exe [--out report.md]

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "Pipkite.Core/Clock.h"
#include "Pipkite.Core/ParserBuilder.h"
#include "Pipkite.Core/Planner.h"
#include "Pipkite.Core/Uuid.h"
#include "Pipkite.Protocol/Framing.h"
#include "Pipkite.Protocol/NamedPipe.h"
#include "Pipkite.Protocol/Platform.h"
#include "Pipkite.Storage/Store.h"

using namespace sr;
using Clock = std::chrono::steady_clock;

namespace {

struct Stats {
    double p50, p95, p99, max, mean;
    std::size_t n;
};

Stats stats(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    auto pct = [&](double p) { return v[std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size())))]; };
    double sum = 0;
    for (double x : v) sum += x;
    return {pct(0.50), pct(0.95), pct(0.99), v.back(), sum / static_cast<double>(v.size()), v.size()};
}

std::string row(const char* name, const char* unit, const Stats& s, const std::string& target) {
    char buf[512];
    std::snprintf(buf, sizeof buf, "| %s | %s | %.3f | %.3f | %.3f | %.3f | %zu | %s |\n", name, unit, s.p50, s.p95, s.p99, s.max, s.n, target.c_str());
    return buf;
}

std::string cpuName() {
    HKEY k;
    char name[256] = "unknown";
    DWORD size = sizeof name;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExA(k, "ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(name), &size);
        RegCloseKey(k);
    }
    return name;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring out;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::wstring(argv[i]) == L"--out") out = argv[i + 1];
    std::ostringstream md;
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    MEMORYSTATUSEX mem{sizeof mem};
    GlobalMemoryStatusEx(&mem);
    md << "# Pipkite 1.0.0 benchmark report\n\n";
    md << "- CPU: " << cpuName() << " (" << si.dwNumberOfProcessors << " logical processors)\n";
    md << "- RAM: " << mem.ullTotalPhys / (1024 * 1024 * 1024) << " GB\n";
#ifdef NDEBUG
    md << "- Build: Release x64\n";
#else
    md << "- Build: Debug x64 (unoptimized; Release numbers are lower)\n";
#endif
    md << "- Durability: SQLite WAL, synchronous=FULL (pre-submission commits are flushed)\n\n";
    md << "| Metric | Unit | p50 | p95 | p99 | max | n | Specification target |\n|---|---|---|---|---|---|---|---|\n";

    // parser
    auto compiled = CompiledTemplate::compile(starterGoldTemplate()).value();
    TemplateSignalParser parser(compiled);
    auto corpus = requiredSpecFixtures();
    std::vector<double> parseUs;
    const int kParse = 50000;
    auto t0 = Clock::now();
    for (int i = 0; i < kParse; ++i) {
        ParseContext ctx;
        auto a = Clock::now();
        auto s = parser.parse(corpus[i % corpus.size()].text, ctx);
        parseUs.push_back(std::chrono::duration<double, std::micro>(Clock::now() - a).count());
        (void)s;
    }
    const double parseSecs = std::chrono::duration<double>(Clock::now() - t0).count();
    auto ps = stats(parseUs);
    md << row("Normalize + parse (messages <= 4 KiB)", "us", ps, "p95 <= 1000 us, p99 <= 3000 us");

    // planner
    ExecutionPolicy policy;
    policy.executionMode = ExecutionMode::Automatic;
    policy.symbolLimits["XAUUSD"] = {0.5, 0.5, 0};
    SourceBinding b;
    b.chatId = -1;
    policy.sources.push_back(b);
    AccountSnapshot acc;
    acc.key = {"T", 1, "S"};
    acc.connected = acc.tradeAllowed = true;
    acc.equity = acc.balance = acc.freeMargin = 10000;
    SymbolSpec gold;
    gold.name = "XAUUSD";
    gold.tickSize = gold.point = 0.01;
    gold.tickValue = 1;
    gold.volumeMin = gold.volumeStep = 0.01;
    Quote q{"XAUUSD", 4297.0, 4297.3, 0, 0};
    ParseContext ctx;
    ctx.source = {-1, 1};
    SystemClock clock;
    ctx.publishedAtMs = clock.nowMs();
    Signal sig = parser.parse(corpus[0].text, ctx);
    q.receivedAtMs = clock.nowMs();
    ExecutionPlanner planner([] { return newUuid(); });
    std::vector<double> planUs;
    for (int i = 0; i < 20000; ++i) {
        PlanningInput in;
        in.signal = &sig;
        in.binding = &policy.sources[0];
        in.policy = &policy;
        in.account = &acc;
        in.symbol = &gold;
        in.quote = &q;
        in.nowMs = ctx.publishedAtMs + 100;
        auto a = Clock::now();
        auto d = planner.evaluate(in);
        planUs.push_back(std::chrono::duration<double, std::micro>(Clock::now() - a).count());
        (void)d;
    }
    md << row("Policy + planning decision", "us", stats(planUs), "(part of decision budget)");

    // durable journal
    auto dir = std::filesystem::temp_directory_path() / (L"PipkiteBench-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    {
        auto opened = db::Store::open(platform::narrow((dir / L"pipkite.db").wstring()), clock);
        auto store = std::move(opened.value());
        std::vector<double> commitMs;
        for (int i = 0; i < 300; ++i) {
            ExecutionPlan plan;
            plan.planId = newUuid();
            plan.account = acc.key;
            plan.brokerSymbol = "XAUUSD";
            plan.totalVolume = 0.01;
            plan.children = {{0, 0.01, 4305.0, 1}};
            Command c;
            c.commandId = newUuid();
            c.planId = plan.planId;
            c.action = "BUY";
            c.payloadJson = "{}";
            c.state = CommandState::Dispatching;  // engine hot path: one durable commit before sending
            auto a = Clock::now();
            store->recordPlan(plan, {c});
            commitMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - a).count());
        }
        md << row("Durable preparation (plan + command + Dispatching, one flushed commit)", "ms", stats(commitMs), "decision incl. durable prep p95 <= 10 ms");

        // Engine pattern: raw message admit + signal upsert (background) queued right before the order commit,
        // with the UI snapshot publisher reading statistics concurrently (as in the running engine).
        std::atomic<bool> readerStop{false};
        std::thread reader([&] {
            while (!readerStop) {
                store->stats();
                Sleep(150);
            }
        });
        std::vector<double> mixedMs;
        for (int i = 0; i < 300; ++i) {
            IncomingMessage msg;
            msg.chatId = -1;
            msg.messageId = 100000 + i;
            msg.text = corpus[0].text;
            Signal s2 = sig;
            s2.source.messageId = msg.messageId;
            ExecutionPlan plan;
            plan.planId = newUuid();
            plan.account = acc.key;
            plan.brokerSymbol = "XAUUSD";
            plan.totalVolume = 0.01;
            plan.children = {{0, 0.01, 4305.0, 1}};
            Command c;
            c.commandId = newUuid();
            c.planId = plan.planId;
            c.action = "BUY";
            c.payloadJson = std::string(900, 'p');  // realistic EXECUTE payload size
            c.state = CommandState::Dispatching;
            plan.parserVersion = std::string(40, 'v');
            auto a = Clock::now();
            store->admitRawMessage(msg);
            store->upsertSignal(s2, SignalState::Planned, nlohmann::json::object(), nlohmann::json::array());
            store->recordPlan(plan, {c});
            mixedMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - a).count());
            Sleep(40);  // realistic gap: the writer goes idle and checkpoints between signals
        }
        readerStop = true;
        reader.join();
        md << row("Engine hot path: admit + background signal write + order commit", "ms", stats(mixedMs), "decision incl. durable prep p95 <= 10 ms");
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    // framing
    std::string payload(900, 'x');
    std::vector<double> frameUs;
    for (int i = 0; i < 100000; ++i) {
        auto a = Clock::now();
        std::string f = ipc::encodeFrame(payload);
        ipc::FrameDecoder dec;
        dec.feed(f.data(), f.size());
        std::string outp;
        dec.next(outp);
        frameUs.push_back(std::chrono::duration<double, std::micro>(Clock::now() - a).count());
    }
    md << row("Frame encode + decode (1 KiB)", "us", stats(frameUs), "-");

    // named pipe round trip
    const std::wstring name = platform::pipeName(L"bench-" + std::to_wstring(GetCurrentProcessId()));
    ipc::PipeServer server(name, 2, [](std::shared_ptr<ipc::PipeConnection> c) { c->readLoop([&](std::string&& p) { c->send(p); }); });
    server.start();
    auto client = ipc::connectPipe(name, 3000);
    std::vector<double> rttUs;
    if (client) {
        std::mutex mu;
        std::condition_variable cv;
        int received = 0;
        std::thread reader([&] {
            client->readLoop([&](std::string&&) {
                std::lock_guard lock(mu);
                ++received;
                cv.notify_one();
            });
        });
        for (int i = 0; i < 2000; ++i) {
            auto a = Clock::now();
            client->send(payload);
            std::unique_lock lock(mu);
            cv.wait(lock, [&] { return received > i; });
            rttUs.push_back(std::chrono::duration<double, std::micro>(Clock::now() - a).count());
        }
        client->close();
        reader.join();
    }
    server.stop();
    if (!rttUs.empty()) md << row("Local named-pipe round trip (1 KiB)", "us", stats(rttUs), "-");

    char tp[256];
    std::snprintf(tp, sizeof tp, "\n**Offline parser throughput:** %.0f messages/second on the fixed fixture corpus (target >= 1,000/s; not a live order-rate target).\n", kParse / parseSecs);
    md << tp;
    md << "\nEnd-to-end TDLib-callback-to-EA-send latency is measured live by the engine (Dashboard > latency percentiles) using the shared QueryPerformanceCounter "
          "clock; broker/network latency is reported separately and is not a guaranteed service level.\n";
    std::string report = md.str();
    std::fwrite(report.data(), 1, report.size(), stdout);
    if (!out.empty()) platform::writeFileAtomic(out, report);
    return 0;
}
