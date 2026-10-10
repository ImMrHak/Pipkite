// End-to-end integration: launches the real Pipkite.Engine.exe and the simulated MT5 terminal
// (which drives the real MT5BridgeHost.dll + signed bridge implementation), then acts as the UI over the
// authenticated pipe. Runs in an isolated data directory and pipe instance so it never touches a real
// installation. Skipped when the binaries are not built next to the test runner.

#include <windows.h>

#include <condition_variable>
#include <filesystem>
#include <future>
#include <map>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

#include "Pipkite.Core/Uuid.h"
#include "Pipkite.Protocol/Crypto.h"
#include "Pipkite.Protocol/Envelope.h"
#include "Pipkite.Protocol/NamedPipe.h"
#include "Pipkite.Protocol/Platform.h"
#include "TestFramework.h"

using namespace sr;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct Proc {
    PROCESS_INFORMATION pi{};
    bool start(const std::wstring& cmd) {
        STARTUPINFOW si{sizeof si};
        std::wstring c = cmd;
        return CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
    }
    void kill() {
        if (pi.hProcess) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            pi = {};
        }
    }
    bool waitExit(unsigned ms) { return pi.hProcess && WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0; }
    ~Proc() { kill(); }
};

class UiClient {
public:
    bool connect() {
        std::string err;
        conn_ = ipc::connectPipe(platform::pipeName(L"ui"), 10000, &err);
        if (!conn_) return false;
        auto secret = platform::loadSecret(L"ui");
        if (!secret) return false;
        reader_ = std::thread([this] {
            conn_->readLoop([this](std::string&& f) {
                auto e = ipc::parseEnvelope(f);
                if (!e) return;
                std::lock_guard lock(mu_);
                if (e->type == ipc::msg::RESPONSE) responses_[e->id] = e->payload;
                else if (e->type == ipc::msg::EVENT && e->payload.value("topic", "") == "snapshot") snapshot_ = e->payload["data"];
                else if (e->type == ipc::msg::CHALLENGE || e->type == ipc::msg::WELCOME) handshake_.push_back(*e);
                cv_.notify_all();
            });
        });
        const std::string nonceU = crypto::toHex(crypto::randomBytes(16));
        ipc::Envelope hello;
        hello.type = ipc::msg::HELLO;
        hello.payload = {{"nonce", nonceU}};
        conn_->send(hello.serialize());
        ipc::Envelope ch;
        if (!waitHandshake(ch)) return false;
        const std::string nonceE = ch.payload.value("nonce", "");
        if (crypto::toHex(crypto::hmacSha256(*secret, "engine|" + nonceU + "|" + nonceE)) != ch.payload.value("proof", "")) return false;  // authenticate the engine
        ipc::Envelope auth;
        auth.type = ipc::msg::AUTH;
        auth.payload = {{"proof", crypto::toHex(crypto::hmacSha256(*secret, "ui|" + nonceE + "|" + nonceU))}};
        conn_->send(auth.serialize());
        ipc::Envelope w;
        return waitHandshake(w) && w.type == ipc::msg::WELCOME;
    }

    json call(const std::string& method, const json& params = json::object(), unsigned timeoutMs = 20000) {
        ipc::Envelope r;
        r.type = ipc::msg::REQUEST;
        r.id = newUuid();
        r.payload = {{"method", method}, {"params", params}};
        conn_->send(r.serialize());
        std::unique_lock lock(mu_);
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return responses_.count(r.id) > 0; })) throw srtest::Failure{"timeout calling " + method};
        json resp = responses_[r.id];
        if (!resp.value("ok", false)) throw srtest::Failure{method + " failed: " + resp.value("error", "")};
        return resp["result"];
    }

    json snapshot() {
        std::lock_guard lock(mu_);
        return snapshot_;
    }

    template <typename Pred>
    bool waitFor(Pred p, unsigned ms) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            json s = call("getSnapshot");
            if (p(s)) return true;
            Sleep(150);
        }
        return false;
    }

    ~UiClient() {
        if (conn_) conn_->close();
        if (reader_.joinable()) reader_.join();
    }

private:
    bool waitHandshake(ipc::Envelope& out) {
        std::unique_lock lock(mu_);
        if (!cv_.wait_for(lock, std::chrono::seconds(10), [&] { return !handshake_.empty(); })) return false;
        out = handshake_.front();
        handshake_.erase(handshake_.begin());
        return true;
    }
    std::shared_ptr<ipc::PipeConnection> conn_;
    std::thread reader_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::map<std::string, json> responses_;
    std::vector<ipc::Envelope> handshake_;
    json snapshot_;
};

std::string historyStateOf(UiClient& ui, const std::string& chatId, const std::string& messageId) {
    auto page = ui.call("history.page", {{"limit", 50}});
    for (auto& r : page)
        if (r.value("chatId", "") == chatId && r.value("messageId", "") == messageId) return r.value("state", "");
    return "";
}

}  // namespace

TEST(e2e_engine_bridge_fake_terminal_full_flow) {
    const fs::path bin = platform::exeDir();
    if (!fs::exists(bin / L"Pipkite.Engine.exe") || !fs::exists(bin / L"Pipkite.FakeTerminal.exe") || !fs::exists(bin / L"trusted_keys")) {
        std::printf("        (skipped: engine/fake terminal/signed modules not present in %s)\n", platform::narrow(bin.wstring()).c_str());
        return;
    }
    const fs::path data = fs::temp_directory_path() / (L"PipkiteE2E-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(data);
    fs::create_directories(data);
    SetEnvironmentVariableW(L"PIPKITE_DATA_DIR", data.c_str());
    SetEnvironmentVariableW(L"PIPKITE_INSTANCE", (L"e2e" + std::to_wstring(GetCurrentProcessId())).c_str());

    Proc engine;
    CHECK(engine.start(L"\"" + (bin / L"Pipkite.Engine.exe").wstring() + L"\""));
    Sleep(300);
    UiClient ui;
    bool connected = false;
    for (int i = 0; i < 40 && !connected; ++i) {
        connected = ui.connect();
        if (!connected) Sleep(250);
    }
    CHECK(connected);

    // no preview mode anymore: always automatic, paused until a terminal is paired and reconciled
    json s0 = ui.call("getSnapshot");
    CHECK_EQ(s0.value("mode", ""), std::string("automatic"));
    CHECK(s0.value("paused", false));

    // Configure one simulated source and symbol limits.
    const std::string chat = "-1000000000001";
    ui.call("sources.save", {{"sources", json::array({{{"chatId", chat}, {"title", "Demo (simulated)"}, {"chatType", "channel"}, {"template", "gold_parser_native"},
                                                        {"entryMode", "wait_for_range"}, {"aiMode", "off"}}})}});
    json policy = ui.call("policy.get");
    policy["symbolLimits"] = {{"XAUUSD", {{"maxSpreadPrice", 0.5}, {"maxEntryDeviationPrice", 0.5}, {"maxLots", 1.0}}}};
    policy["accountLimits"]["maxOpenPositions"] = 100;
    policy["accountLimits"]["maxTotalLots"] = 10.0;
    ui.call("policy.save", {{"policy", policy}});

    // Sandbox: parse only, nothing submitted.
    json sb = ui.call("parser.sandbox", {{"text", "#GOLD BUY 4300 4295\nTP1 4305\nTP2 4310\nSL 4287"}});
    CHECK_EQ(sb["signal"].value("status", ""), std::string("Valid"));

    // Start the simulated terminal and approve pairing.
    Proc term;
    CHECK(term.start(L"\"" + (bin / L"Pipkite.FakeTerminal.exe").wstring() + L"\" --login 90000001"));
    CHECK(ui.waitFor([](const json& s) { return !s["bridge"]["pairingPending"].is_null(); }, 15000));
    ui.call("mt5.approvePairing", {{"confirmLogin", "90000001"}});
    CHECK(ui.waitFor([](const json& s) { return s.value("reconciled", false) && s["bridge"].value("paired", false) && !s.value("paused", true); }, 15000));
    json snap = ui.call("getSnapshot");
    CHECK(snap["account"].value("simulated", false));  // simulated terminal is always labeled

    // Automatic requires typing the destination account number.
    bool refused = false;
    try {
        ui.call("engine.setMode", {{"mode", "automatic"}, {"confirmLogin", "123"}});
    } catch (const srtest::Failure&) {
        refused = true;
    }
    CHECK(refused);
    ui.call("engine.setMode", {{"mode", "automatic"}, {"confirmLogin", "90000001"}});

    // Build a signal around the live simulated ask so the range is executable now.
    json st = ui.call("mt5.status");
    double ask = 0;
    for (auto& sym : st["symbols"])
        if (sym.value("name", "") == "XAUUSD") ask = sym["quote"].value("ask", 0.0);
    CHECK(ask > 0);
    char text[512];
    std::snprintf(text, sizeof text, "GOLD BUY %.2f %.2f\nTP1 %.2f\nTP2 %.2f\nSL %.2f", ask + 2.0, ask - 2.0, ask + 25, ask + 40, ask - 20);
    json inj = ui.call("sim.inject", {{"chatId", chat}, {"text", std::string(text)}});
    const std::string msgId = inj.value("messageId", "");

    // Command must reach Filled through the durable journal; exactly one command for one plan.
    bool filled = false;
    json detail;
    for (int i = 0; i < 80 && !filled; ++i) {
        Sleep(150);
        detail = ui.call("history.detail", {{"chatId", chat}, {"messageId", msgId}, {"revision", 0}});
        if (detail.contains("commands") && !detail["commands"].empty()) filled = detail["commands"][0].value("state", "") == "Filled";
    }
    if (!filled) std::printf("        detail: %s\n", detail.dump().substr(0, 800).c_str());
    CHECK(filled);
    CHECK_EQ(detail["commands"].size(), std::size_t(1));
    CHECK(detail["commands"][0]["events"].size() >= 4);  // Prepared, Dispatching, ReceivedByEA, Submitted, Filled
    CHECK_EQ(detail.value("state", ""), std::string("Dispatched"));

    // Latency sample: 20 more executable signals, then report the engine's own measured percentiles.
    for (int i = 0; i < 20; ++i) {
        json st2 = ui.call("mt5.status");
        double a2 = 0;
        for (auto& sym : st2["symbols"])
            if (sym.value("name", "") == "XAUUSD") a2 = sym["quote"].value("ask", 0.0);
        char t2[256];
        std::snprintf(t2, sizeof t2, "GOLD BUY %.2f %.2f\nTP1 %.2f\nSL %.2f", a2 + 2.0, a2 - 2.0, a2 + 25, a2 - 20);
        ui.call("sim.inject", {{"chatId", chat}, {"text", std::string(t2)}});
        Sleep(250);
    }
    Sleep(1500);
    json m = ui.call("getSnapshot")["metrics"];
    for (const char* k : {"parseUs", "decisionMs", "journalMs", "journalWaitMs", "journalExecMs", "journalBodyMs", "journalFlushMs", "e2eMs", "brokerMs"})
        std::printf("        latency %-11s p50 %8.3f  p95 %8.3f  max %8.3f  n=%d\n", k, m[k].value("p50", 0.0), m[k].value("p95", 0.0), m[k].value("max", 0.0), m[k].value("n", 0));

    // Commentary never becomes an order.
    json c = ui.call("sim.inject", {{"chatId", chat}, {"text", "TP1 HIT"}});
    Sleep(400);
    CHECK_EQ(historyStateOf(ui, chat, c.value("messageId", "")), std::string("Rejected"));

    // Pause rejects new entries; positions untouched.
    ui.call("engine.pause");
    json p = ui.call("sim.inject", {{"chatId", chat}, {"text", std::string(text)}});
    Sleep(400);
    CHECK_EQ(historyStateOf(ui, chat, p.value("messageId", "")), std::string("Rejected"));
    ui.call("engine.resume");

    // Profit accounting sees the opening commission deal (copier-owned).
    json prof = ui.call("profit.summary");
    CHECK(prof["copier"].value("deals", 0) >= 1);

    // Missing heartbeat inhibits dispatch: kill the terminal and confirm the blocked reason.
    term.kill();
    CHECK(ui.waitFor([](const json& s) { return !s.value("dispatchBlockedReason", "").empty(); }, 8000));

    // Settings round-trip (regression: the handler once iterated a destroyed temporary and saw garbage keys).
    json values = json::object();
    values["ui.theme"] = "light";
    values["ui.notifications"] = "false";
    json saveParams = json::object();
    saveParams["values"] = values;
    ui.call("settings.save", saveParams);
    json got = ui.call("settings.get");
    CHECK(got.value("ui.theme", "") == "light");
    CHECK(got.value("ui.notifications", "") == "false");

    // Backup while running, then orderly shutdown.
    json bk = ui.call("data.backup", {{"path", platform::narrow((data / L"e2e.srbackup").wstring())}, {"password", ""}, {"scope", "full"}});
    CHECK(bk.value("bytes", 0) > 0);
    ui.call("engine.shutdown");
    CHECK(engine.waitExit(10000));
    SetEnvironmentVariableW(L"PIPKITE_DATA_DIR", nullptr);
    SetEnvironmentVariableW(L"PIPKITE_INSTANCE", nullptr);
    std::error_code ec;
    fs::remove_all(data, ec);
}

// Demo seeding for manual UI walkthroughs: connects to an ALREADY RUNNING isolated instance (same
// PIPKITE_DATA_DIR / PIPKITE_INSTANCE), configures a simulated source, approves the simulated
// terminal, enables Automatic and injects sample signals. Runs only when PIPKITE_DEMO_SEED=1.
TEST(demo_seed_running_instance) {
    wchar_t flag[8];
    if (GetEnvironmentVariableW(L"PIPKITE_DEMO_SEED", flag, 8) == 0) return;
    UiClient ui;
    bool connected = false;
    for (int i = 0; i < 40 && !connected; ++i) {  // the engine may still be warming up when we attach
        connected = ui.connect();
        if (!connected) Sleep(250);
    }
    CHECK(connected);
    wchar_t theme[16];
    if (GetEnvironmentVariableW(L"PIPKITE_DEMO_THEME", theme, 16) > 0) {  // only switch the UI theme, then stop
        json values = json::object();
        values["ui.theme"] = platform::narrow(theme);
        json params = json::object();
        params["values"] = values;
        ui.call("settings.save", params);
        return;
    }
    const std::string chat = "-1000000000001";
    ui.call("sources.save", {{"sources", json::array({{{"chatId", chat}, {"title", "Demo channel (simulated)"}, {"chatType", "channel"}, {"template", "gold_parser_native"},
                                                        {"entryMode", "wait_for_range"}, {"aiMode", "advisory"}},
                                                       {{"chatId", "-1000000000002"}, {"title", "Forex VIP (simulated)"}, {"chatType", "supergroup"}, {"template", "gold_parser_native"},
                                                        {"entryMode", "immediate_within_tolerance"}, {"aiMode", "off"}}})}});
    json policy = ui.call("policy.get");
    policy["symbolLimits"] = {{"XAUUSD", {{"maxSpreadPrice", 0.5}, {"maxEntryDeviationPrice", 0.5}, {"maxLots", 1.0}}},
                              {"EURUSD", {{"maxSpreadPrice", 0.0002}, {"maxEntryDeviationPrice", 0.0005}, {"maxLots", 1.0}}}};
    ui.call("policy.save", {{"policy", policy}});
    CHECK(ui.waitFor([](const json& s) { return !s["bridge"]["pairingPending"].is_null() || s["bridge"].value("paired", false); }, 20000));
    json s0 = ui.call("getSnapshot");
    if (!s0["bridge"].value("paired", false)) ui.call("mt5.approvePairing", {{"confirmLogin", "90000001"}});
    CHECK(ui.waitFor([](const json& s) { return s.value("reconciled", false) && !s.value("paused", true); }, 20000));
    ui.call("engine.setMode", {{"mode", "automatic"}, {"confirmLogin", "90000001"}});
    json st = ui.call("mt5.status");
    double ask = 0;
    for (auto& sym : st["symbols"])
        if (sym.value("name", "") == "XAUUSD") ask = sym["quote"].value("ask", 0.0);
    char a[256], b[256], c[256];
    std::snprintf(a, sizeof a, "#GOLD BUY %.2f %.2f\nTP1 %.2f\nTP2 %.2f\nTP3 %.2f\nSL %.2f", ask + 2, ask - 2, ask + 10, ask + 20, ask + 30, ask - 15);
    std::snprintf(b, sizeof b, "GOLD (XAUUSD) Sell NOW\nEntry %.2f-%.2f\nTP1 %.2f\nTP2 %.2f\nSL %.2f", ask + 30, ask + 34, ask + 20, ask + 10, ask + 45);
    std::snprintf(c, sizeof c, "GOLD BUY %.2f\nSL %.2f", ask, ask - 10);
    const char* samples[] = {a, "TP1 HIT \xE2\x9C\x85 +100 pips", b, c, "buy or sell?", "Move SL to BE"};
    for (const char* text : samples) {
        ui.call("sim.inject", {{"chatId", chat}, {"text", std::string(text)}});
        Sleep(700);
    }
    ui.call("sim.inject", {{"chatId", "-1000000000002"}, {"text", "EURUSD SELL NOW 1.0850\nSL 1.0880\nTP 1.0800"}});
}
