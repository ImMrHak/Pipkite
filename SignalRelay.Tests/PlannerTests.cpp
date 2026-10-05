#include "SignalRelay.Core/AiContract.h"
#include "SignalRelay.Core/Dedup.h"
#include "SignalRelay.Core/JsonModels.h"
#include "SignalRelay.Core/Planner.h"
#include "SignalRelay.Core/Policy.h"
#include "SignalRelay.Core/ProfitLedger.h"
#include "SignalRelay.Core/SignalParser.h"
#include "SignalRelay.Core/StateMachine.h"
#include "TestFramework.h"

using namespace sr;

namespace {

constexpr std::int64_t kNow = 1790000000000;

struct Fixture {
    ExecutionPolicy policy;
    SourceBinding binding;
    AccountSnapshot account;
    SymbolSpec gold;
    Quote quote;
    int ids = 0;
    ExecutionPlanner planner{[this] { return "plan-" + std::to_string(++ids); }};

    Fixture() {
        policy.executionMode = ExecutionMode::Automatic;
        policy.symbolLimits["XAUUSD"] = {0.50, 0.50, 0};
        binding.chatId = -1001234567890;
        binding.title = "Gold VIP";
        policy.sources.push_back(binding);
        account.key = {"T1", 5550001, "Demo-Server"};
        account.connected = true;
        account.tradeAllowed = true;
        account.equity = 10000;
        account.balance = 10000;
        account.freeMargin = 10000;
        gold.name = "XAUUSD";
        gold.digits = 2;
        gold.point = 0.01;
        gold.tickSize = 0.01;
        gold.tickValue = 1.0;  // 1 USD per 0.01 per lot (100 oz contract)
        gold.contractSize = 100;
        gold.volumeMin = 0.01;
        gold.volumeMax = 50;
        gold.volumeStep = 0.01;
        quote = {"XAUUSD", 4297.00, 4297.30, kNow, kNow};
    }

    Signal signal(const std::string& text) {
        static auto c = CompiledTemplate::compile(starterGoldTemplate()).value();
        ParseContext ctx;
        ctx.source = {binding.chatId, 42};
        ctx.publishedAtMs = kNow - 2000;
        ctx.receivedAtMs = kNow - 1990;
        return parseWithTemplate(text, *c, ctx);
    }

    Decision eval(const Signal& s, std::int64_t now = kNow) {
        PlanningInput in;
        in.signal = &s;
        in.binding = &binding;
        in.policy = &policy;
        in.account = &account;
        in.symbol = &gold;
        in.quote = &quote;
        in.nowMs = now;
        return planner.evaluate(in);
    }
};

const char* kBuyRange = "#GOLD BUY 4300 4295\nTP1 4305\nTP2 4310\nTP3 4315\nTP4 4320\nTP5 4325\nTP6 4330\nSL 4287";

}  // namespace

TEST(planner_wait_for_range_buy_uses_ask) {
    Fixture f;
    auto s = f.signal(kBuyRange);
    auto d = f.eval(s);
    CHECK_EQ(std::string(toString(d.kind)), std::string("Planned"));
    CHECK_EQ(d.plan->children.size(), std::size_t(1));
    CHECK_EQ(*d.plan->children[0].takeProfit, 4305.0);
    CHECK_EQ(d.plan->totalVolume, 0.01);
    CHECK_NEAR(d.plan->referencePrice, 4297.30, 1e-9);
    // Ask outside range -> waiting candidate (not a broker pending order), never chases.
    f.quote.ask = 4301.00;
    f.quote.bid = 4300.70;
    auto w = f.eval(s);
    CHECK_EQ(std::string(toString(w.kind)), std::string("Waiting"));
    CHECK_EQ(w.waitUntilMs, s.publishedAtMs + 120000);
    // After expiry it is rejected.
    auto e = f.eval(s, s.publishedAtMs + 121000);
    CHECK_EQ(std::string(toString(e.kind)), std::string("Rejected"));
}

TEST(planner_preview_mode_never_plans_dispatch) {
    Fixture f;
    f.policy.executionMode = ExecutionMode::Preview;
    auto d = f.eval(f.signal(kBuyRange));
    CHECK_EQ(std::string(toString(d.kind)), std::string("PreviewOnly"));
    CHECK(d.plan.has_value());
}

TEST(planner_sell_uses_bid_and_spread_cap) {
    Fixture f;
    auto s = f.signal("GOLD (XAUUSD) Sell NOW\nEntry 4302-4306\nTP1 4296\nTP2 4288\nSL 4316");
    f.quote = {"XAUUSD", 4303.00, 4303.30, kNow, kNow};
    auto d = f.eval(s);
    CHECK_EQ(std::string(toString(d.kind)), std::string("Planned"));
    CHECK_NEAR(d.plan->referencePrice, 4303.00, 1e-9);
    f.quote.ask = 4304.00;  // spread 1.00 > 0.50
    auto w = f.eval(s);
    CHECK_EQ(std::string(toString(w.kind)), std::string("Waiting"));
}

TEST(planner_risk_percent_rounds_down_and_rejects_min_lot) {
    Fixture f;
    f.policy.sizing.mode = SizingPolicy::Mode::RiskPercent;
    f.policy.sizing.riskPercent = 1.0;  // 100 USD
    auto s = f.signal(kBuyRange);
    auto d = f.eval(s);
    CHECK_EQ(std::string(toString(d.kind)), std::string("Planned"));
    // loss per lot = (4297.30-4287)/0.01*1 = 1030 USD -> 100/1030 = 0.097 -> 0.09
    CHECK_NEAR(d.plan->totalVolume, 0.09, 1e-9);
    CHECK(d.plan->plannedRiskMoney <= 100.0);
    f.account.equity = 500;  // budget 5 USD -> 0.0048 lots < min 0.01
    auto r = f.eval(s);
    CHECK_EQ(std::string(toString(r.kind)), std::string("Rejected"));
}

TEST(planner_split_exact_and_min_lot_rules) {
    Fixture f;
    f.policy.targets.mode = TargetPolicy::Mode::Split;
    f.policy.targets.allocations = {50, 30, 20};
    f.policy.sizing.lots = 0.10;
    auto s = f.signal(kBuyRange);
    auto d = f.eval(s);
    CHECK_EQ(std::string(toString(d.kind)), std::string("Planned"));
    double sum = 0;
    for (auto& c : d.plan->children) sum += c.volume;
    CHECK_NEAR(sum, 0.10, 1e-9);
    CHECK_EQ(d.plan->children.size(), std::size_t(3));
    CHECK_EQ(*d.plan->children[2].takeProfit, 4315.0);
    // 0.01 cannot be split across six targets: reject, never send 0.01 six times.
    f.policy.sizing.lots = 0.01;
    f.policy.targets.allocations = {20, 20, 15, 15, 15, 15};
    auto r = f.eval(s);
    CHECK_EQ(std::string(toString(r.kind)), std::string("Rejected"));
    f.policy.targets.singleTargetFallback = true;
    auto fb = f.eval(s);
    CHECK_EQ(std::string(toString(fb.kind)), std::string("Planned"));
    CHECK_EQ(fb.plan->children.size(), std::size_t(1));
}

TEST(planner_split_rejected_on_netting) {
    Fixture f;
    f.account.marginMode = MarginMode::Netting;
    f.policy.targets.mode = TargetPolicy::Mode::Split;
    f.policy.targets.allocations = {50, 50};
    f.policy.sizing.lots = 0.10;
    CHECK_EQ(std::string(toString(f.eval(f.signal(kBuyRange)).kind)), std::string("Rejected"));
    f.policy.targets.mode = TargetPolicy::Mode::Single;
    f.account.netExposureSymbols = {"XAUUSD"};
    CHECK_EQ(std::string(toString(f.eval(f.signal(kBuyRange)).kind)), std::string("Rejected"));
}

TEST(planner_symbol_mapping_is_explicit) {
    Fixture f;
    auto s = f.signal("USOIL BUY 70.10\nSL 69.50\nTP 71.00");
    CHECK_EQ(std::string(toString(s.status)), std::string("Valid"));
    auto d = f.eval(s);
    CHECK_EQ(std::string(toString(d.kind)), std::string("Rejected"));
    CHECK(d.reason.find("exact broker symbol") != std::string::npos);
}

TEST(planner_limits_and_account_checks) {
    Fixture f;
    auto s = f.signal(kBuyRange);
    f.account.copierOpenPositions = 5;
    CHECK_EQ(std::string(toString(f.eval(s).kind)), std::string("Rejected"));
    f.account.copierOpenPositions = 0;
    f.policy.account.maxDailyLoss = 100;
    f.account.copierDailyRealized = -150;
    CHECK_EQ(std::string(toString(f.eval(s).kind)), std::string("Rejected"));
    f.account.copierDailyRealized = 0;
    f.account.connected = false;
    CHECK_EQ(std::string(toString(f.eval(s).kind)), std::string("Rejected"));
    f.account.connected = true;
    f.quote.receivedAtMs = kNow - 10000;  // stale quote -> wait (range mode)
    CHECK_EQ(std::string(toString(f.eval(s).kind)), std::string("Waiting"));
    f.quote.receivedAtMs = kNow;
    f.policy.symbolLimits.clear();
    CHECK_EQ(std::string(toString(f.eval(s).kind)), std::string("Rejected"));
}

TEST(planner_immediate_and_pending_modes) {
    Fixture f;
    f.binding.entryMode = EntryMode::ImmediateWithinTolerance;
    f.binding.immediateTolerancePrice = 0.5;
    auto nowSig = f.signal("GOLD SELL NOW 4303\nSL 4316\nTP 4290");
    f.quote = {"XAUUSD", 4303.20, 4303.50, kNow, kNow};
    CHECK_EQ(std::string(toString(f.eval(nowSig).kind)), std::string("Planned"));
    f.quote = {"XAUUSD", 4305.00, 4305.30, kNow, kNow};
    CHECK_EQ(std::string(toString(f.eval(nowSig).kind)), std::string("Rejected"));
    auto old = f.eval(nowSig, nowSig.publishedAtMs + 31000);
    CHECK_EQ(std::string(toString(old.kind)), std::string("Rejected"));

    f.binding.entryMode = EntryMode::ExplicitPendingOnly;
    auto lim = f.signal("XAUUSD BUY LIMIT 4290\nSL 4280\nTP 4310");
    f.quote = {"XAUUSD", 4297.00, 4297.30, kNow, kNow};
    auto d = f.eval(lim);
    CHECK_EQ(std::string(toString(d.kind)), std::string("Planned"));
    CHECK(d.plan->kind == OrderKind::Limit);
    CHECK(d.plan->pendingExpiryMs > kNow);
    f.gold.expirationModes = 1;  // GTC only -> never leave a permanent order silently
    CHECK_EQ(std::string(toString(f.eval(lim).kind)), std::string("Rejected"));
    f.gold.expirationModes = 15;
    auto noKind = f.signal("XAUUSD BUY 4290\nSL 4280\nTP 4310");
    CHECK_EQ(std::string(toString(f.eval(noKind).kind)), std::string("Rejected"));  // never infer LIMIT vs STOP
}

TEST(planner_force_enter_when_price_slipped_just_past_the_range) {
    Fixture f;  // gold: digits 2, point 0.01 -> 1 pip = 0.01
    auto buy = f.signal(kBuyRange);  // BUY range [4295, 4300], SL 4287
    // Ask 0.02 above the band (2 pips). Default: wait.
    f.quote = {"XAUUSD", 4299.85, 4300.02, kNow, kNow};
    CHECK_EQ(std::string(toString(f.eval(buy).kind)), std::string("Waiting"));
    // Force-enter on, tolerance 2 pips -> take it at market.
    f.policy.forceEnterMissed = true;
    f.policy.forceEnterPips = 2.0;
    auto d = f.eval(buy);
    CHECK_EQ(std::string(toString(d.kind)), std::string("Planned"));
    CHECK(d.plan->kind == OrderKind::Market);
    CHECK(d.plan->maxDeviationPrice >= 0.02 - 1e-9);  // deviation widened to cover the deliberate slip
    // Just beyond the tolerance (0.05 past = 5 pips) still waits.
    f.quote = {"XAUUSD", 4299.85, 4300.05, kNow, kNow};
    CHECK_EQ(std::string(toString(f.eval(buy).kind)), std::string("Waiting"));
    // A better price (ask below the band) is never force-entered; it waits for the range.
    f.quote = {"XAUUSD", 4293.90, 4294.10, kNow, kNow};
    CHECK_EQ(std::string(toString(f.eval(buy).kind)), std::string("Waiting"));
}

TEST(planner_review_for_ambiguous_and_stops_never_widened) {
    Fixture f;
    auto amb = f.signal("GOLD BUY 4300 SELL 4310\nSL 4290\nTP 4320");
    CHECK_EQ(std::string(toString(f.eval(amb).kind)), std::string("Review"));
    f.gold.stopsLevelPoints = 2000;  // 20.00 minimum distance
    CHECK_EQ(std::string(toString(f.eval(f.signal(kBuyRange)).kind)), std::string("Rejected"));
}

TEST(sizing_helpers) {
    CHECK_EQ(toSteps(0.0299999999, 0.01), 2);
    CHECK_EQ(toSteps(0.03, 0.01), 3);
    CHECK_NEAR(roundVolumeDown(0.097, 0.01), 0.09, 1e-12);
    CHECK_NEAR(normalizeToTick(4297.304, 0.01), 4297.30, 1e-9);
    CHECK_NEAR(normalizeToTick(1.08507, 0.00005), 1.08505, 1e-12);
    SymbolSpec spec;
    spec.volumeMin = 0.01;
    spec.volumeStep = 0.01;
    auto r = splitVolume(0.07, {50, 50}, spec);
    CHECK(r.ok);
    CHECK_NEAR(r.volumes[0] + r.volumes[1], 0.07, 1e-12);
    CHECK_NEAR(r.volumes[0], 0.04, 1e-12);
}

TEST(state_machine_rules) {
    CHECK(canTransition(CommandState::Prepared, CommandState::Dispatching));
    CHECK(!canTransition(CommandState::Filled, CommandState::Rejected));
    CHECK(!canTransition(CommandState::Unknown, CommandState::Dispatching));  // never blindly retried
    CHECK(canTransition(CommandState::Unknown, CommandState::Filled));
    CHECK(canTransition(CommandState::PartiallyFilled, CommandState::PartiallyFilled));
    CHECK(canTransition(CommandState::Submitted, CommandState::Working));
    CHECK(canTransition(CommandState::Working, CommandState::Expired));
    CHECK(!crossedSubmissionBoundary(CommandState::Prepared));
    CHECK(crossedSubmissionBoundary(CommandState::Dispatching));
}

TEST(dedup_revision_and_semantic) {
    RevisionDeduplicator d(3);
    CHECK(d.firstSeen(1, 10, 0));
    CHECK(!d.firstSeen(1, 10, 0));
    CHECK(d.firstSeen(1, 10, 1700000000));  // an edit is a new revision
    Fixture f;
    SemanticDuplicateDetector sem;
    auto a = f.signal(kBuyRange);
    auto b = a;
    b.source = {-100999, 7};
    CHECK(!sem.check(a, kNow, 60).has_value());
    auto m = sem.check(b, kNow + 5000, 60);
    CHECK(m.has_value());
    CHECK_EQ(m->original.chatId, a.source.chatId);
    auto c = a;
    c.source = {-100998, 8};
    CHECK(!sem.check(c, kNow + 120000, 60).has_value());  // outside window
}

TEST(profit_ledger_accounting) {
    ProfitLedger L;
    auto deal = [](std::uint64_t t, int type, int entry, double profit, double comm, double swap, double fee, std::int64_t magic, const char* cmd) {
        Deal d;
        d.ticket = t;
        d.type = type;
        d.entry = entry;
        d.profit = profit;
        d.commission = comm;
        d.swap = swap;
        d.fee = fee;
        d.magic = magic;
        d.commandId = cmd;
        d.symbol = "XAUUSD";
        d.timeMs = static_cast<std::int64_t>(t) * 1000;
        return d;
    };
    CHECK(L.addDeal(deal(1, 2, 0, 5000, 0, 0, 0, 0, "")));                     // deposit
    CHECK(L.addDeal(deal(2, 0, 0, 0, -3.5, 0, 0, kCopierMagic, "c1")));       // opening commission
    CHECK(L.addDeal(deal(3, 1, 1, 40, -3.5, -1.2, -0.3, kCopierMagic, "c1"))); // partial close
    CHECK(L.addDeal(deal(4, 1, 1, 25, 0, 0, 0, kCopierMagic, "c1")));          // final close
    CHECK(L.addDeal(deal(5, 0, 0, -100, 0, 0, 0, 0, "")));                     // manual trade
    CHECK(!L.addDeal(deal(3, 1, 1, 40, -3.5, -1.2, -0.3, kCopierMagic, "c1"))); // replay ignored
    CHECK(L.addDeal(deal(6, 0, 1, 10, 0, 0, 0, kCopierMagic, "")));            // unresolved attribution
    ProfitFilter f;
    auto s = L.summarize(f);
    CHECK_NEAR(s.net, 40 - 3.5 - 3.5 - 1.2 - 0.3 + 25 + 10, 1e-9);
    CHECK_NEAR(s.deposits, 0, 1e-9);  // deposits are account cash flows, never copier profit
    CHECK_EQ(s.unresolvedCount, 1);
    ProfitFilter all;
    all.copierOnly = false;
    auto w = L.summarize(all);
    CHECK_NEAR(w.deposits, 5000, 1e-9);
    CHECK_NEAR(w.net, 40 - 3.5 - 3.5 - 1.2 - 0.3 + 25 - 100 + 10, 1e-9);
    auto curve = L.cumulativeRealized(f);
    CHECK(!curve.empty());
    CHECK_NEAR(curve.back().value, s.net, 1e-9);
}

TEST(ai_request_contains_only_the_signal) {
    Fixture f;
    auto s = f.signal(kBuyRange);
    auto req = buildAiRequest(s, "11111111-2222-4333-8444-555555555555");
    for (auto& [k, v] : req.items())
        CHECK(std::find(allowedAiRequestKeys().begin(), allowedAiRequestKeys().end(), k) != allowedAiRequestKeys().end());
    for (auto& [k, v] : req["signal"].items())
        CHECK(std::find(allowedAiSignalKeys().begin(), allowedAiSignalKeys().end(), k) != allowedAiSignalKeys().end());
    std::string dump = req.dump();
    CHECK(dump.find("1001234567890") == std::string::npos);  // no chat id
    CHECK(dump.find("5550001") == std::string::npos);        // no account login
}

TEST(ai_response_validation_and_gates) {
    const std::string id = "11111111-2222-4333-8444-555555555555";
    auto good = parseAiResponse(R"({"requestId":"11111111-2222-4333-8444-555555555555","verdict":"Good","reasons":["RR 1:2"],"modelVersion":"sr-review-1.0","score":0.8})", id);
    CHECK(good.verdict == AiVerdict::Good);
    CHECK(evaluateGate(good, 0.6, "review") == GateOutcome::Pass);
    auto low = good;
    low.score = 0.3;
    CHECK(evaluateGate(low, 0.6, "review") == GateOutcome::Review);
    auto bad = parseAiResponse(R"({"requestId":"x","verdict":"Good","reasons":[],"modelVersion":"m"})", id);
    CHECK(bad.verdict == AiVerdict::Unavailable);
    auto malformed = parseAiResponse("not json", id);
    CHECK(malformed.verdict == AiVerdict::Unavailable);
    CHECK(evaluateGate(malformed, 0.6, "review") == GateOutcome::Review);
    CHECK(evaluateGate(malformed, 0.6, "skip") == GateOutcome::Skip);
    auto outOfRange = parseAiResponse(R"({"requestId":"11111111-2222-4333-8444-555555555555","verdict":"Good","reasons":[],"modelVersion":"m","score":7})", id);
    CHECK(outOfRange.verdict == AiVerdict::Unavailable);
}

TEST(local_model_requires_a_real_model_file) {
    CHECK(!LocalLinearModel::load("{}").ok());
    auto m = LocalLinearModel::load(R"({"format":"signalrelay-linear-v1","modelVersion":"test-1","bias":-1.0,"weights":{"rr_first":1.2}})");
    CHECK(m.ok());
    Fixture f;
    auto a = m.value().assess(f.signal(kBuyRange));
    CHECK(a.score.has_value());
    CHECK(!a.scoreCalibrated);
}

TEST(policy_json_roundtrip_spec_example) {
    const char* spec = R"({
      "schemaVersion": 1, "executionMode": "preview",
      "source": {"chatId": "-1001234567890", "template": "gold_provider_v1", "entryMode": "wait_for_range"},
      "sizing": {"mode": "fixed_total_lots", "lots": 0.01},
      "targets": {"mode": "single", "selectedIndex": 1},
      "requireStopLoss": true, "maxImmediateAgeSeconds": 30, "rangeExpirySeconds": 120,
      "catchUpAfterReconnect": false, "symbolMap": {"GOLD": "XAUUSD"},
      "symbolLimits": {"XAUUSD": {"maxSpreadPrice": 0.50, "maxEntryDeviationPrice": 0.50}}
    })";
    auto p = ExecutionPolicy::fromJson(nlohmann::json::parse(spec));
    CHECK(p.ok());
    CHECK_EQ(p.value().sources.size(), std::size_t(1));
    CHECK_EQ(p.value().sources[0].chatId, -1001234567890LL);
    auto j = p.value().toJson();
    CHECK(j["sources"][0]["chatId"].is_string());  // int64 as string in UI-facing JSON
    auto back = ExecutionPolicy::fromJson(j);
    CHECK(back.ok());
    CHECK(back.value().toJson() == j);
    auto future = nlohmann::json::parse(spec);
    future["schemaVersion"] = 2;
    auto r = ExecutionPolicy::fromJson(future);
    CHECK(!r.ok());
    CHECK(r.error().message.find("migrate") != std::string::npos);
    auto badSplit = j;
    badSplit["targets"] = {{"mode", "split"}, {"allocations", {60, 30}}};
    CHECK(!ExecutionPolicy::fromJson(badSplit).ok());
}

TEST(plan_json_roundtrip) {
    Fixture f;
    auto d = f.eval(f.signal(kBuyRange));
    auto j = toJson(*d.plan);
    auto back = planFromJson(j);
    CHECK(back.ok());
    CHECK(toJson(back.value()) == j);
}
