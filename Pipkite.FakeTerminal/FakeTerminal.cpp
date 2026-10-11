// test-only MT5 terminal stand-in, used by the end-to-end tests and for screenshots. never shipped.
// loads the real MT5BridgeHost.dll and talks to it exactly like the EA, with an in-memory broker.
// 
// usage: Pipkite.FakeTerminal.exe [--login N] [--server NAME] [--netting] [--reject-all] [--seconds N]
//        [--no-heartbeat-after S]

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using InitFn = int (*)(const wchar_t*, long long, const wchar_t*);
using RecvFn = int (*)(unsigned char*, int);
using SendFn = int (*)(const unsigned char*, int);
using StatusFn = int (*)();
using StatusTextFn = int (*)(wchar_t*, int);
using JournalAppendFn = int (*)(const wchar_t*, const wchar_t*, const wchar_t*);
using JournalStateFn = int (*)(const wchar_t*, wchar_t*, int);
using NowFn = long long (*)();
using ShutdownFn = void (*)();

struct Api {
    InitFn init;
    RecvFn recv;
    SendFn send;
    StatusFn status;
    StatusTextFn statusText;
    JournalAppendFn journalAppend;
    JournalStateFn journalState;
    NowFn now;
    ShutdownFn shutdown;
} api;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string fmt(double v, int digits = 5) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", digits, v);
    return b;
}

std::int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

// EA line format helpers
using Kv = std::map<std::string, std::string>;

Kv parseKv(const std::string& text) {
    Kv kv;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

double num(const Kv& kv, const std::string& k, double d = 0) {
    auto it = kv.find(k);
    if (it == kv.end() || it->second.empty()) return d;
    return std::atof(it->second.c_str());
}

std::string str(const Kv& kv, const std::string& k) {
    auto it = kv.find(k);
    return it == kv.end() ? std::string{} : it->second;
}

struct Out {
    std::string text;
    explicit Out(const std::string& type) { text = "type=" + type + "\n"; }
    Out& put(const std::string& k, const std::string& v) {
        text += "payload." + k + "=" + v + "\n";
        return *this;
    }
    Out& put(const std::string& k, double v, int d = 5) { return put(k, fmt(v, d)); }
    Out& puti(const std::string& k, long long v) { return put(k, std::to_string(v)); }
    bool send() {
        for (int i = 0; i < 50; ++i) {
            int r = api.send(reinterpret_cast<const unsigned char*>(text.data()), (int)text.size());
            if (r == 1) return true;
            if (r < 0) return false;
            Sleep(2);  // outbound queue full: retry (trade events are never dropped by the EA)
        }
        return false;
    }
};

// in-memory broker
struct Sym {
    std::string name;
    int digits;
    double tick, tickValue, contract, bid, spread, vol;
};

struct Position {
    std::uint64_t ticket;
    std::string commandId, symbol;
    bool buy;
    double volume, open, sl, tp;
};

struct Pending {
    std::uint64_t ticket;
    std::string commandId, symbol;
    bool buy, limit;
    double volume, price, sl, tp;
    std::int64_t expiryMs;
};

struct Sim {
    long long login = 90000001;
    std::string server = "Demo-Server";
    std::string terminalId = "7F3A9C2E5B1D4086A2E9C4F1B8D3E605";
    bool netting = false, rejectAll = false, paused = false;
    double balance = 10000, commissionPerLot = 7.0;
    std::map<std::string, Sym> syms;
    std::vector<Position> positions;
    std::vector<Pending> pendings;
    std::uint64_t nextTicket = 5000001;
    std::mt19937_64 rng{42};
    std::set<std::string> seenCommands;

    Sim() {
        syms["XAUUSD"] = {"XAUUSD", 2, 0.01, 1.0, 100, 4297.00, 0.25, 0.35};
        syms["EURUSD"] = {"EURUSD", 5, 0.00001, 1.0, 100000, 1.08500, 0.00008, 0.00006};
        syms["XAGUSD"] = {"XAGUSD", 3, 0.001, 5.0, 5000, 31.200, 0.020, 0.010};
        syms["GBPUSD"] = {"GBPUSD", 5, 0.00001, 1.0, 100000, 1.27000, 0.00010, 0.00007};
    }

    double ask(const Sym& s) const { return s.bid + s.spread; }

    void step() {
        std::normal_distribution<double> n(0, 1);
        for (auto& [k, s] : syms) {
            s.bid += n(rng) * s.vol;
            s.bid = std::round(s.bid / s.tick) * s.tick;
        }
    }

    double floating() const {
        double f = 0;
        for (auto& p : positions) {
            const Sym& s = syms.at(p.symbol);
            const double px = p.buy ? s.bid : ask(s);
            f += (p.buy ? px - p.open : p.open - px) / s.tick * s.tickValue * p.volume;
        }
        return f;
    }
};

Sim sim;

std::string accountKey() { return sim.terminalId + "|" + std::to_string(sim.login) + "|" + sim.server; }

void sendAccount() {
    double lots = 0;
    std::set<std::string> symbols;
    for (auto& p : sim.positions) {
        lots += p.volume;
        symbols.insert(p.symbol);
    }
    Out o("ACCOUNT_SNAPSHOT");
    o.put("account.terminalId", sim.terminalId).puti("account.login", sim.login).put("account.server", sim.server);
    o.put("currency", "USD").put("balance", sim.balance, 2).put("equity", sim.balance + sim.floating(), 2).put("margin", lots * 900, 2);
    o.put("freeMargin", sim.balance + sim.floating() - lots * 900, 2).put("marginMode", sim.netting ? "netting" : "hedging");
    o.put("tradeAllowed", "true").put("connected", "true");
    o.puti("copierOpenPositions", (long long)sim.positions.size()).puti("copierPendingOrders", (long long)sim.pendings.size()).put("copierOpenLots", lots, 2);
    int i = 0;
    for (auto& s : symbols) o.put("copierSymbols." + std::to_string(i++), s);
    if (sim.netting) {
        i = 0;
        for (auto& s : symbols) o.put("netExposureSymbols." + std::to_string(i++), s);
    }
    o.send();
}

void sendSymbols() {
    Out o("SYMBOL_SNAPSHOT");
    int i = 0;
    for (auto& [k, s] : sim.syms) {
        const std::string p = "symbols." + std::to_string(i++) + ".";
        o.put(p + "name", s.name).puti(p + "digits", s.digits).put(p + "point", s.tick, 6).put(p + "tickSize", s.tick, 6).put(p + "tickValue", s.tickValue, 4);
        o.put(p + "contractSize", s.contract, 0).put(p + "volumeMin", 0.01, 2).put(p + "volumeMax", 50.0, 2).put(p + "volumeStep", 0.01, 2);
        o.puti(p + "stopsLevel", 0).puti(p + "freezeLevel", 0).put(p + "tradeAllowed", "true").put(p + "sessionOpen", "true").puti(p + "fillingModes", 3).puti(p + "expirationModes", 15);
        o.put(p + "marginPerLot", 900.0, 2);
    }
    o.send();
}

void sendQuotes() {
    Out o("QUOTE");
    int i = 0;
    for (auto& [k, s] : sim.syms) {
        const std::string p = "quotes." + std::to_string(i++) + ".";
        o.put(p + "symbol", s.name).put(p + "bid", s.bid, s.digits).put(p + "ask", sim.ask(s), s.digits).puti(p + "brokerTimeMs", nowMs());
    }
    o.send();
}

void sendStatus(const std::string& id, const std::string& state, const std::string& reason, double filled = 0, std::uint64_t ticket = 0, long long sendUs = 0) {
    Out o("COMMAND_STATUS");
    o.put("commandId", id).put("state", state).put("reason", reason).put("filledVolume", filled, 2);
    if (ticket) o.put("orderTicket", std::to_string(ticket)).put("requestId", std::to_string(ticket));
    if (sendUs) o.puti("sendUs", sendUs);
    o.send();
}

void sendDeal(std::uint64_t order, std::uint64_t position, const std::string& symbol, bool buy, int entry, double volume, double price, double profit, double commission,
              const std::string& commandId) {
    Out o("TRADE_EVENT");
    o.put("kind", "deal");
    o.put("deal.ticket", std::to_string(sim.nextTicket++)).put("deal.order", std::to_string(order)).put("deal.positionId", std::to_string(position));
    o.puti("deal.timeMs", nowMs()).put("deal.symbol", symbol).puti("deal.type", buy ? 0 : 1).puti("deal.entry", entry).put("deal.volume", volume, 2);
    o.put("deal.price", price, 5).put("deal.profit", profit, 2).put("deal.commission", commission, 2).put("deal.swap", 0.0, 2).put("deal.fee", 0.0, 2);
    o.puti("deal.magic", 20260930).put("deal.commandId", commandId);
    o.send();
}

void execute(const Kv& kv) {
    const std::string id = str(kv, "payload.commandId");
    wchar_t st[64];
    // Deduplicate by command ID using the durable host journal.
    if (api.journalState(widen(id).c_str(), st, 64) > 0 || sim.seenCommands.count(id)) {
        sendStatus(id, "Rejected", "duplicate command ignored (already journaled)");
        return;
    }
    sim.seenCommands.insert(id);
    sendStatus(id, "ReceivedByEA", "");
    auto reject = [&](const std::string& why) {
        api.journalAppend(widen(id).c_str(), L"Rejected", widen(why).c_str());
        sendStatus(id, "Rejected", why);
    };
    const std::string acct = str(kv, "payload.account.terminalId") + "|" + str(kv, "payload.account.login") + "|" + str(kv, "payload.account.server");
    if (acct != accountKey()) return reject("account mismatch: command bound to another login/server");
    if (sim.paused) return reject("EA paused: unsubmitted command rejected");
    if (sim.rejectAll) return reject("broker rejection (fault injection)");
    if (nowMs() > (std::int64_t)num(kv, "payload.deadlineMs") + 1000) return reject("deadline passed before submission");
    auto it = sim.syms.find(str(kv, "payload.symbol"));
    if (it == sim.syms.end()) return reject("symbol not available");
    Sym& s = it->second;
    const bool buy = str(kv, "payload.side") == "BUY";
    const double px = buy ? sim.ask(s) : s.bid;
    const double spread = sim.ask(s) - s.bid;
    if (spread > num(kv, "payload.maxSpreadPrice") + 1e-9) return reject("spread above cap at EA revalidation");
    const std::string kind = str(kv, "payload.orderKind");
    const double lo = num(kv, "payload.entryLow"), hi = num(kv, "payload.entryHigh");
    if (kind == "MARKET" && lo > 0 && (px < lo - 1e-9 || px > hi + 1e-9)) return reject("price left the permitted range at EA revalidation (not chased)");
    const double volume = num(kv, "payload.volume");
    if (volume < 0.01) return reject("volume below minimum");
    // Durable acceptance record BEFORE broker submission.
    if (!api.journalAppend(widen(id).c_str(), L"Accepted", L"")) return reject("journal failure: not submitted");
    const long long sendUs = api.now();
    const std::uint64_t ticket = sim.nextTicket++;
    sendStatus(id, "Submitted", "OrderSendAsync accepted", 0, ticket, sendUs);
    if (kind == "MARKET") {
        const double commission = -sim.commissionPerLot * volume / 2;
        sim.positions.push_back({ticket, id, s.name, buy, volume, px, num(kv, "payload.stopLoss"), num(kv, "payload.takeProfit")});
        sim.balance += commission;
        sendDeal(ticket, ticket, s.name, buy, 0, volume, px, 0, commission, id);
        api.journalAppend(widen(id).c_str(), L"Filled", L"");
        sendStatus(id, "Filled", "filled at " + fmt(px, s.digits) + "", volume, ticket);
    } else {
        sim.pendings.push_back({ticket, id, s.name, buy, kind == "LIMIT", volume, num(kv, "payload.pendingPrice"), num(kv, "payload.stopLoss"), num(kv, "payload.takeProfit"),
                                (std::int64_t)num(kv, "payload.pendingExpiryMs")});
        api.journalAppend(widen(id).c_str(), L"Working", L"");
        sendStatus(id, "Working", "pending order placed", 0, ticket);
    }
    sendAccount();
}

void closePosition(std::size_t i, const std::string& why) {
    Position p = sim.positions[i];
    const Sym& s = sim.syms[p.symbol];
    const double px = p.buy ? s.bid : sim.ask(s);
    const double profit = (p.buy ? px - p.open : p.open - px) / s.tick * s.tickValue * p.volume;
    const double commission = -sim.commissionPerLot * p.volume / 2;
    sim.balance += profit + commission;
    sim.positions.erase(sim.positions.begin() + (long long)i);
    sendDeal(p.ticket, p.ticket, p.symbol, !p.buy, 1, p.volume, px, profit, commission, p.commandId);
    std::printf("  closed %s %s %.2f @ %s (%s) P/L %.2f\n", p.buy ? "BUY" : "SELL", p.symbol.c_str(), p.volume, fmt(px, s.digits).c_str(), why.c_str(), profit);
}

void manageBroker() {
    for (std::size_t i = 0; i < sim.positions.size();) {
        Position& p = sim.positions[i];
        const Sym& s = sim.syms[p.symbol];
        const double px = p.buy ? s.bid : sim.ask(s);
        const bool slHit = p.sl > 0 && (p.buy ? px <= p.sl : px >= p.sl);
        const bool tpHit = p.tp > 0 && (p.buy ? px >= p.tp : px <= p.tp);
        if (slHit || tpHit) closePosition(i, slHit ? "SL" : "TP");
        else ++i;
    }
    for (std::size_t i = 0; i < sim.pendings.size();) {
        Pending& o = sim.pendings[i];
        const Sym& s = sim.syms[o.symbol];
        const double px = o.buy ? sim.ask(s) : s.bid;
        const bool trigger = o.limit ? (o.buy ? px <= o.price : px >= o.price) : (o.buy ? px >= o.price : px <= o.price);
        if (nowMs() > o.expiryMs) {
            sendStatus(o.commandId, "Expired", "pending order expired", 0, o.ticket);
            sim.pendings.erase(sim.pendings.begin() + (long long)i);
            continue;
        }
        if (trigger) {
            sim.positions.push_back({o.ticket, o.commandId, o.symbol, o.buy, o.volume, px, o.sl, o.tp});
            sendDeal(o.ticket, o.ticket, o.symbol, o.buy, 0, o.volume, px, 0, -sim.commissionPerLot * o.volume / 2, o.commandId);
            sendStatus(o.commandId, "Filled", "pending triggered", o.volume, o.ticket);
            sim.pendings.erase(sim.pendings.begin() + (long long)i);
            continue;
        }
        ++i;
    }
}

void reconcile(const Kv& kv) {
    Out o("RECONCILE_REPORT");
    int n = (int)num(kv, "payload.commands.count", 0);
    for (int i = 0; i < n; ++i) {
        const std::string id = str(kv, "payload.commands." + std::to_string(i) + ".commandId");
        wchar_t st[64] = {};
        const int len = api.journalState(widen(id).c_str(), st, 64);
        std::string state;
        for (int k = 0; k < len; ++k) state.push_back((char)st[k]);
        const std::string p = "commands." + std::to_string(i) + ".";
        o.put(p + "commandId", id).put(p + "journaled", len > 0 ? "true" : "false").put(p + "state", len > 0 ? state : "Unknown");
    }
    o.send();
}

void handle(const std::string& text) {
    Kv kv = parseKv(text);
    const std::string type = str(kv, "type");
    if (type == "SNAPSHOT_REQUEST") {
        sendAccount();
        sendSymbols();
        sendQuotes();
    } else if (type == "RECONCILE_REQUEST") {
        reconcile(kv);
    } else if (type == "EXECUTE") {
        execute(kv);
    } else if (type == "PAUSE") {
        sim.paused = str(kv, "payload.paused") == "true";
        std::printf("  EA pause=%s\n", sim.paused ? "on" : "off");
    } else if (type == "MANAGE") {
        const std::string action = str(kv, "payload.action");
        if (action == "close_positions")
            while (!sim.positions.empty()) closePosition(0, "user close");
        if (action == "cancel_pending") {
            for (auto& o : sim.pendings) sendStatus(o.commandId, "Canceled", "canceled by user", 0, o.ticket);
            sim.pendings.clear();
        }
        sendAccount();
    } else if (type == "WELCOME" || type == "ERROR") {
        std::printf("  %s\n", text.c_str());
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    int seconds = 0, heartbeatStopAfter = -1, pollMs = 10;  // 10 ms mirrors the EA timer default
    bool emulateTimer = false;  // --emulate-mt5-timer: poll with Sleep like an EA timer instead of waiting on the inbound event
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--login" && i + 1 < argc) sim.login = _wtoi64(argv[++i]);
        else if (a == L"--server" && i + 1 < argc) {
            std::wstring w = argv[++i];
            int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
            sim.server.assign((size_t)n, ' ');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), sim.server.data(), n, nullptr, nullptr);
        } else if (a == L"--netting") sim.netting = true;
        else if (a == L"--reject-all") sim.rejectAll = true;
        else if (a == L"--seconds" && i + 1 < argc) seconds = _wtoi(argv[++i]);
        else if (a == L"--emulate-mt5-timer") emulateTimer = true;
        else if (a == L"--poll-ms" && i + 1 < argc) pollMs = (std::max)(1, _wtoi(argv[++i]));
        else if (a == L"--no-heartbeat-after" && i + 1 < argc) heartbeatStopAfter = _wtoi(argv[++i]);
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    HMODULE h = LoadLibraryW((dir + L"\\MT5BridgeHost.dll").c_str());
    if (!h) {
        std::printf("MT5BridgeHost.dll not found next to the harness\n");
        return 1;
    }
    api.init = (InitFn)GetProcAddress(h, "BridgeInitialize");
    api.recv = (RecvFn)GetProcAddress(h, "BridgeTryReceive");
    api.send = (SendFn)GetProcAddress(h, "BridgeTrySend");
    api.status = (StatusFn)GetProcAddress(h, "BridgeConnectionStatus");
    api.statusText = (StatusTextFn)GetProcAddress(h, "BridgeStatusText");
    api.journalAppend = (JournalAppendFn)GetProcAddress(h, "BridgeJournalAppend");
    api.journalState = (JournalStateFn)GetProcAddress(h, "BridgeJournalState");
    api.now = (NowFn)GetProcAddress(h, "BridgeNowMicros");
    api.shutdown = (ShutdownFn)GetProcAddress(h, "BridgeShutdown");
    if (!api.init || !api.recv || !api.send || !api.status || !api.journalAppend || !api.journalState || !api.now || !api.shutdown) {
        std::printf("MT5BridgeHost.dll does not export the expected ABI\n");
        return 1;
    }
    const int rc = api.init(widen(sim.terminalId).c_str(), sim.login, widen(sim.server).c_str());
    wchar_t text[512];
    api.statusText(text, 512);
    std::printf("test MT5 terminal: login %lld @ %s (init=%d) %ls\n", sim.login, sim.server.c_str(), rc, text);
    if (rc < 0) return 1;
    using EventFn = void* (*)();
    auto inboundEventFn = (EventFn)GetProcAddress(h, "BridgeInboundEvent");
    HANDLE inbound = (!emulateTimer && inboundEventFn) ? (HANDLE)inboundEventFn() : nullptr;

    std::vector<unsigned char> buf(64 * 1024);
    auto start = std::chrono::steady_clock::now();
    std::int64_t lastQuote = 0, lastHeartbeat = 0, lastAccount = 0, lastStep = 0;
    int lastStatus = -99;
    bool quotesDirty = true;
    while (true) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (seconds > 0 && elapsed >= seconds) break;
        // Bounded batch per timer tick, like the EA's 15 ms OnTimer.
        for (int i = 0; i < 16; ++i) {
            int n = api.recv(buf.data(), (int)buf.size());
            if (n <= 0) break;
            handle(std::string(reinterpret_cast<char*>(buf.data()), (size_t)n));
        }
        const std::int64_t now = nowMs();
        const int status = api.status();
        if (status != lastStatus) {
            api.statusText(text, 512);
            std::printf("bridge status %d: %ls\n", status, text);
            lastStatus = status;
        }
        if (now - lastStep >= 100) {
            sim.step();
            quotesDirty = true;
            manageBroker();
            lastStep = now;
        }
        if (status >= 2) {
            if (quotesDirty && now - lastQuote >= 20) {  // changed-only, like the EA
                quotesDirty = false;
                sendQuotes();
                lastQuote = now;
            }
            if (now - lastHeartbeat >= 1000 && (heartbeatStopAfter < 0 || elapsed < heartbeatStopAfter)) {
                Out("HEARTBEAT").puti("eaTimeMs", now).send();
                lastHeartbeat = now;
            }
            if (now - lastAccount >= 2000) {
                sendAccount();
                lastAccount = now;
            }
        }
        if (inbound) WaitForSingleObject(inbound, (DWORD)pollMs);  // wake as soon as a command is queued
        else Sleep(pollMs);
    }
    api.shutdown();
    return 0;
}
