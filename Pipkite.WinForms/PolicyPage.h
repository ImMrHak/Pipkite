#pragma once
// Execution policy: sizing, take-profit policy, entry timing, expiry, catch-up, account limits,
// symbol limits and AI settings. Values are validated by the engine; defaults are conservative product
// defaults, not trading recommendations.

#include "Pages.h"

namespace PipkiteUI {

public ref class PolicyPage : public PageBase {
public:
    PolicyPage() {
        FlowLayoutPanel^ stack = L::Stack();
        limitMap_ = gcnew Dictionary<String^, array<double>^>();
        catalog_ = gcnew List<String^>();

        // First on the page: a symbol without a max spread is never traded.
        FlowLayoutPanel^ sl = gcnew FlowLayoutPanel();
        sl->FlowDirection = FlowDirection::TopDown;
        sl->AutoSize = true;
        sl->Controls->Add(L::Wrap("Pipkite only trades a symbol that has a max spread. Pick a symbol, set its limits, then click Save policy.", nullptr, 680));
        TableLayoutPanel^ lf = L::Form2();
        symbol_ = L::Combo(200, gcnew array<String^>{});
        symbol_->MaxDropDownItems = 18;
        symbol_->AccessibleName = "Broker symbol";
        symbol_->SelectedIndexChanged += gcnew EventHandler(this, &PolicyPage::OnSymbolPicked);
        L::AddField(lf, "Broker symbol", symbol_, "Symbols from your MetaTrader. The ones you already set are listed first.");
        spread_ = L::Num(0, 100000, 0, 5, 0.01);
        L::AddField(lf, "Max spread (price units)", spread_, "Skip the trade if the spread is wider than this. For gold, 0.40 means 40 cents. 0 = this symbol is not traded.");
        deviation_ = L::Num(0, 100000, 0, 5, 0.01);
        L::AddField(lf, "Entry deviation (price units)", deviation_, "How far the price may be from the signal's entry and still be taken. For gold, 1.00 means $1.");
        symLots_ = L::Num(0, 1000, 0, 2, 0.01);
        L::AddField(lf, "Max lots (0 = none)", symLots_, "The biggest trade size on this symbol. 0 = no cap.");
        for each (NumericUpDown^ n in gcnew array<NumericUpDown^>{spread_, deviation_, symLots_}) n->ValueChanged += gcnew EventHandler(this, &PolicyPage::OnLimitEdited);
        sl->Controls->Add(lf);
        suggest_ = L::Wrap("", "muted", 500);
        useSuggest_ = L::Btn("Use suggested limits", nullptr, "Fill in the suggested limits for this symbol. Nothing is traded until you click Save policy.");
        useSuggest_->Click += gcnew EventHandler(this, &PolicyPage::OnUseSuggested);
        suggestRow_ = L::Row(suggest_, useSuggest_);
        suggestRow_->Visible = false;
        sl->Controls->Add(suggestRow_);
        symStatus_ = L::Text("", "muted");
        sl->Controls->Add(symStatus_);
        stack->Controls->Add(L::Group("Symbol limits", sl));

        TableLayoutPanel^ sz = L::Form2();
        sizingMode_ = L::Combo(220, gcnew array<String^>{"fixed_total_lots", "risk_percent"});
        L::AddField(sz, "Sizing mode", sizingMode_, "fixed_total_lots: a set total lot size per signal. risk_percent: size so the estimated loss at SL is about the risk % of equity (rounded down to the lot step; rejected if the broker minimum exceeds the budget).");
        lots_ = L::Num(0.01, 100, 0.01, 2, 0.01);
        L::AddField(sz, "Fixed total lots", lots_, "Total lot size sent per signal when sizing mode is fixed_total_lots.");
        risk_ = L::Num(0.01, 10, 0.5, 2, 0.05);
        L::AddField(sz, "Risk % per trade", risk_, "Approximate % of equity lost if the stop loss is hit (risk_percent mode). Gaps, slippage and fees can exceed the estimate.");
        messageLot_ = L::Check("Use the lot size written in the message");
        L::AddField(sz, "", messageLot_);
        stack->Controls->Add(L::Group("Position sizing", sz));

        TableLayoutPanel^ tp = L::Form2();
        targetMode_ = L::Combo(160, gcnew array<String^>{"single", "split"});
        L::AddField(tp, "Target mode", targetMode_, "single: one take-profit. split: spread the volume across several of the message's take-profits.");
        tpIndex_ = L::Num(1, 20, 1, 0, 1);
        L::AddField(tp, "Selected TP", tpIndex_, "Which take-profit to use, by its position in the message's TP list (1 = first).");
        allocations_ = L::Box(200);
        L::AddField(tp, "Split allocations %", allocations_, "When target mode is split: the percentage sent to each TP, e.g. 50,30,20.");
        fallback_ = L::Check("If the minimum lot prevents splitting, use the single selected target instead of rejecting");
        L::AddField(tp, "", fallback_);
        stack->Controls->Add(L::Group("Take profit", tp));

        TableLayoutPanel^ tm = L::Form2();
        maxAge_ = L::Num(1, 3600, 30, 0, 1);
        L::AddField(tm, "Max signal age (s)", maxAge_, "Immediate (market) signals older than this are skipped.");
        rangeExpiry_ = L::Num(1, 86400, 120, 0, 10);
        L::AddField(tm, "Range expiry (s)", rangeExpiry_, "A wait-for-range signal stops waiting this long after it was posted.");
        pendingExpiry_ = L::Num(60, 604800, 3600, 0, 60);
        L::AddField(tm, "Pending expiry (s)", pendingExpiry_, "Pending orders expire after this - only if your broker supports order expiry.");
        future_ = L::Num(0, 60, 5, 0, 1);
        L::AddField(tm, "Future time tolerance (s)", future_, "Allow message timestamps up to this far in the future (clock skew).");
        quoteAge_ = L::Num(100, 60000, 3000, 0, 100);
        L::AddField(tm, "Quote freshness (ms)", quoteAge_, "Reject a trade if the latest quote is older than this.");
        catchUp_ = L::Check("Catch up recent messages after a disconnect");
        L::AddField(tm, "", catchUp_);
        catchUpAge_ = L::Num(1, 3600, 60, 0, 5);
        L::AddField(tm, "Catch-up age limit (s)", catchUpAge_, "When catch-up is on, only replay messages newer than this.");
        semantic_ = L::Check("Send identical setups from different sources to review");
        L::AddField(tm, "", semantic_);
        semanticWindow_ = L::Num(5, 3600, 60, 0, 5);
        L::AddField(tm, "Duplicate window (s)", semanticWindow_, "Identical setups seen within this window are treated as duplicates and sent to review.");
        forceEnter_ = L::Check("Force enter when the price just missed the entry");
        L::Tip(forceEnter_, "For wait-for-range sources: still enter at market if the price slipped only a little past the entry. Off by default.");
        L::AddField(tm, "", forceEnter_);
        forceEnterPips_ = L::Num(0, 50, 2, 1, 0.5);
        L::AddField(tm, "Force-enter tolerance (pips)", forceEnterPips_, "How many pips past the entry still count as 'just missed'. 1 pip = 10 points on 5/3-digit FX, 1 point otherwise. Keep it small.");
        stack->Controls->Add(L::Group("Timing, expiry and duplicates", tm));

        TableLayoutPanel^ ac = L::Form2();
        maxPositions_ = L::Num(1, 500, 5, 0, 1);
        L::AddField(ac, "Max open positions", maxPositions_, "The most positions Pipkite may have open at once.");
        maxPending_ = L::Num(0, 500, 5, 0, 1);
        L::AddField(ac, "Max pending orders", maxPending_, "The most pending orders Pipkite may have waiting at once.");
        maxLots_ = L::Num(0.01, 1000, 1, 2, 0.1);
        L::AddField(ac, "Max total lots", maxLots_, "Cap on the combined lot size of all copier trades.");
        maxDailyLoss_ = L::Num(0, 10000000, 0, 2, 10);
        L::AddField(ac, "Max daily loss (0 = off)", maxDailyLoss_, "Stop copying for the day once realized losses (including fees) reach this. 0 disables it.");
        resetOffset_ = L::Num(-720, 840, 0, 0, 30);
        L::AddField(ac, "Daily reset UTC offset (min)", resetOffset_, "When the daily-loss counter resets, as minutes offset from UTC (0 = midnight UTC).");
        maxRisk_ = L::Num(0.01, 20, 2, 2, 0.1);
        L::AddField(ac, "Max risk per signal (%)", maxRisk_, "Reject a signal whose planned risk is more than this % of equity.");
        startMin_ = L::Box(80);
        endMin_ = L::Box(80);
        L::AddField(ac, "Trading hours (HH:MM - HH:MM)", L::Row(startMin_, L::Text("to"), endMin_), "Only copy during these local hours. Use 00:00 to 24:00 for all day.");
        stack->Controls->Add(L::Group("Account limits", ac));

        TableLayoutPanel^ ai = L::Form2();
        alwaysAssess_ = L::Check("Review every signal with AI (shows Good / Uncertain / Bad in the feed)");
        L::Tip(alwaysAssess_, "On: the fields of every valid signal (never your account or messages) are sent to the Pipkite AI and kept to help it learn. Its verdict shows in the Live feed's AI column. It's advice: it never blocks a trade.");
        L::AddField(ai, "", alwaysAssess_);
        deadline_ = L::Num(50, 10000, 250, 0, 50);
        L::AddField(ai, "Gate deadline (ms)", deadline_, "If a source uses a backend gate: how long to wait for a verdict before the timeout action.");
        threshold_ = L::Num(0, 1, 0.6, 2, 0.05);
        L::AddField(ai, "Gate acceptance score", threshold_, "A gated signal must score at least this (0-1) to be allowed. AI can never override the deterministic safety checks.");
        timeoutAction_ = L::Combo(140, gcnew array<String^>{"review", "skip"});
        L::AddField(ai, "On gate timeout", timeoutAction_, "What to do if the backend doesn't answer within the deadline: send to review, or skip the signal.");
        Button^ test = L::Btn("Test backend connection", nullptr, "Send a synthetic signal to your backend to check it responds. No real order is placed.");
        test->Click += gcnew EventHandler(this, &PolicyPage::OnTestAi);
        ai->Controls->Add(gcnew Label());
        ai->Controls->Add(test);
        stack->Controls->Add(L::Group("AI signal review", ai));

        Button^ save = L::Btn("Save policy", "accent", "Save execution settings. Creates a new configuration version and re-checks waiting signals.");
        save->Click += gcnew EventHandler(this, &PolicyPage::OnSave);
        Button^ reload = L::Btn("Reload", nullptr, "Discard unsaved changes and reload the saved policy.");
        reload->Click += gcnew EventHandler(this, &PolicyPage::OnReload);
        status_ = L::Text("", "muted");
        status_->AutoSize = false;
        status_->Size = Drawing::Size(360, 26);
        status_->TextAlign = ContentAlignment::MiddleLeft;
        stack->Controls->Add(L::Row(save, reload, status_));
        Controls->Add(stack);
        L::OnAnyChange(stack, gcnew EventHandler(this, &PolicyPage::OnDirty));
        Header("Execution policy", "How trades are sized, timed and limited. Hover any ? for details.", this);
    }

    virtual void OnActivated() override {
        if (Client == nullptr || !Client->Connected) return;
        OnReload(nullptr, nullptr);
        Client->Call("mt5.status", nullptr, gcnew ResponseHandler(this, &PolicyPage::OnStatus));  // broker symbols for the picker
    }

private:
    ComboBox ^ sizingMode_, ^targetMode_, ^timeoutAction_;
    NumericUpDown^ forceEnterPips_;
    NumericUpDown ^ lots_, ^risk_, ^tpIndex_, ^maxAge_, ^rangeExpiry_, ^pendingExpiry_, ^future_, ^quoteAge_, ^catchUpAge_, ^semanticWindow_, ^maxPositions_, ^maxPending_,
        ^maxLots_, ^maxDailyLoss_, ^resetOffset_, ^maxRisk_, ^deadline_, ^threshold_;
    CheckBox ^ messageLot_, ^fallback_, ^catchUp_, ^semantic_, ^forceEnter_, ^alwaysAssess_;
    TextBox ^ allocations_, ^startMin_, ^endMin_;
    ComboBox^ symbol_;
    NumericUpDown ^ spread_, ^deviation_, ^symLots_;
    Label ^ symStatus_, ^status_, ^suggest_;
    Button^ useSuggest_;
    FlowLayoutPanel^ suggestRow_;
    Dictionary<String^, array<double>^>^ limitMap_;  // symbol -> {max spread, entry deviation, max lots}
    List<String^>^ catalog_;                          // every broker symbol from MetaTrader
    JObj^ policy_;
    bool loading_, dirty_;

    void OnDirty(Object^ sender, EventArgs^) {
        if (loading_ || sender == symbol_) return;  // picking a symbol to look at is not a change
        SetStatus(true);
    }

    void OnStatus(bool ok, Object^ result, String^) {
        if (!ok) return;
        JObj^ r = J::Obj(result);
        catalog_->Clear();
        array<Object^>^ catalog = J::Arr(r, "catalog");
        if (catalog != nullptr && catalog->Length > 0) {
            for each (Object^ o in catalog) { String^ n = o->ToString(); if (n != "") catalog_->Add(n); }
        } else {
            for each (Object^ o in J::Arr(r, "symbols")) { String^ n = J::Str(J::Obj(o), "name"); if (n != "") catalog_->Add(n); }
        }
        RebuildSymbols();
    }

    // Symbols already set come first, then the rest of the broker's list.
    void RebuildSymbols() {
        bool was = loading_;
        loading_ = true;
        String^ keep = symbol_->Text;
        List<String^>^ set = gcnew List<String^>(limitMap_->Keys);
        set->Sort();
        symbol_->Items->Clear();
        for each (String^ s in set) symbol_->Items->Add(s);
        for each (String^ s in catalog_) if (!limitMap_->ContainsKey(s)) symbol_->Items->Add(s);
        int i = symbol_->Items->IndexOf(keep);
        symbol_->SelectedIndex = i >= 0 ? i : (symbol_->Items->Count > 0 ? 0 : -1);
        loading_ = was;
        ShowSymbol();
    }

    void OnSymbolPicked(Object^, EventArgs^) { ShowSymbol(); }

    void ShowSymbol() {
        bool was = loading_;
        loading_ = true;  // showing stored values is not an edit
        array<double>^ v;
        if (symbol_->Text == "" || !limitMap_->TryGetValue(symbol_->Text, v)) v = gcnew array<double>{0, 0, 0};
        SetNum(spread_, v[0]);
        SetNum(deviation_, v[1]);
        SetNum(symLots_, v[2]);
        loading_ = was;
        array<double>^ s = limitMap_->ContainsKey(symbol_->Text) ? nullptr : Suggested(symbol_->Text);
        suggestRow_->Visible = s != nullptr;
        if (s != nullptr)
            suggest_->Text = "Suggested: max spread " + J::Fmt(s[0]) + ", entry deviation " + J::Fmt(s[1]) + ", max lots " + J::Fmt(s[2]) +
                             ". Check the spread against your broker's in MetaTrader before you save.";
        UpdateSymStatus();
    }

    // Starting values shipped with the app. Only a suggestion: the user applies them and saves, never automatic.
    // Matched on the letters of the broker's name, so XAUUSD.m or GOLD# get the gold values.
    static array<double>^ Suggested(String^ sym) {
        String^ n = "";
        for each (wchar_t c in sym->ToUpperInvariant()) if (Char::IsLetter(c)) n += c;
        if (n->StartsWith("GOLD") || n->Contains("XAUUSD")) return gcnew array<double>{0.40, 1.00, 0.01};
        if (n->StartsWith("SILVER") || n->Contains("XAGUSD")) return gcnew array<double>{0.04, 0.05, 0.01};
        array<String^>^ majors = {"EURUSD", "GBPUSD", "AUDUSD", "NZDUSD", "USDCAD", "USDCHF", "EURGBP"};
        array<String^>^ yen = {"USDJPY", "EURJPY", "GBPJPY"};
        for each (String^ fx in majors) {
            if (n->Contains(fx)) return gcnew array<double>{0.0002, 0.0003, 0.01};
        }
        for each (String^ fx in yen) {
            if (n->Contains(fx)) return gcnew array<double>{0.03, 0.05, 0.01};
        }
        return nullptr;
    }

    void OnUseSuggested(Object^, EventArgs^) {
        array<double>^ s = Suggested(symbol_->Text);
        if (s == nullptr || symbol_->Text == "") return;
        limitMap_[symbol_->Text] = s;
        ShowSymbol();
        SetStatus(true);  // filled in, but only used after Save policy
    }

    void OnLimitEdited(Object^, EventArgs^) {
        if (loading_ || symbol_->Text == "") return;
        if (Val(spread_) > 0) limitMap_[symbol_->Text] = gcnew array<double>{Val(spread_), Val(deviation_), Val(symLots_)};
        else limitMap_->Remove(symbol_->Text);  // no max spread = not traded
        UpdateSymStatus();
    }

    void UpdateSymStatus() {
        List<String^>^ on = gcnew List<String^>();
        for each (KeyValuePair<String^, array<double>^> kv in limitMap_)
            if (kv.Value[0] > 0) on->Add(kv.Key);
        on->Sort();
        String^ hint = catalog_->Count == 0 ? " Connect MetaTrader to list all your broker's symbols." : "";
        symStatus_->Text = (on->Count == 0 ? "No symbol is set yet, so nothing is traded." : "Traded: " + String::Join(", ", on)) + hint;
    }
    void SetStatus(bool dirty) {
        dirty_ = dirty;
        String^ ver = policy_ != nullptr ? J::Str(policy_, "configVersion") : "";
        status_->Text = dirty ? "Unsaved changes — click Save policy" : (ver == "" ? "" : "Saved · configuration v" + ver);
        status_->ForeColor = dirty ? Theme::Warning : Theme::TextMuted;
    }

    static void SetNum(NumericUpDown^ n, double v) { n->Value = Decimal(Math::Max(Decimal::ToDouble(n->Minimum), Math::Min(Decimal::ToDouble(n->Maximum), v))); }
    static double Val(NumericUpDown^ n) { return Decimal::ToDouble(n->Value); }
    static String^ Hm(int minutes) { return (minutes / 60).ToString("00") + ":" + (minutes % 60).ToString("00"); }
    static int ParseHm(String^ s, int def) {
        array<String^>^ p = s->Split(':');
        int h, m;
        if (p->Length == 2 && Int32::TryParse(p[0], h) && Int32::TryParse(p[1], m) && h >= 0 && h <= 24 && m >= 0 && m < 60) return Math::Min(1440, h * 60 + m);
        return def;
    }

    void OnReload(Object^, EventArgs^) { Client->Call("policy.get", nullptr, gcnew ResponseHandler(this, &PolicyPage::OnPolicy)); }

    void OnPolicy(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        loading_ = true;  // loading values must not mark the page dirty
        policy_ = J::Obj(result);
        JObj^ s = J::Obj(policy_, "sizing");
        sizingMode_->SelectedItem = J::Str(s, "mode", "fixed_total_lots");
        SetNum(lots_, J::Num(s, "lots", 0.01));
        SetNum(risk_, J::Num(s, "riskPercent", 0.5));
        messageLot_->Checked = J::Bool(policy_, "allowMessageLot");
        JObj^ t = J::Obj(policy_, "targets");
        targetMode_->SelectedItem = J::Str(t, "mode", "single");
        SetNum(tpIndex_, J::Num(t, "selectedIndex", 1));
        String^ al = "";
        for each (Object^ a in J::Arr(t, "allocations")) al += (al == "" ? "" : ",") + J::Fmt(Convert::ToDouble(a));
        allocations_->Text = al;
        fallback_->Checked = J::Bool(t, "singleTargetFallback");
        SetNum(maxAge_, J::Num(policy_, "maxImmediateAgeSeconds", 30));
        SetNum(rangeExpiry_, J::Num(policy_, "rangeExpirySeconds", 120));
        SetNum(pendingExpiry_, J::Num(policy_, "pendingExpirySeconds", 3600));
        SetNum(future_, J::Num(policy_, "futureToleranceSeconds", 5));
        SetNum(quoteAge_, J::Num(policy_, "quoteMaxAgeMs", 3000));
        catchUp_->Checked = J::Bool(policy_, "catchUpAfterReconnect");
        SetNum(catchUpAge_, J::Num(policy_, "catchUpMaxAgeSeconds", 60));
        JObj^ sd = J::Obj(policy_, "semanticDuplicate");
        semantic_->Checked = J::Bool(sd, "enabled");
        SetNum(semanticWindow_, J::Num(sd, "windowSeconds", 60));
        forceEnter_->Checked = J::Bool(policy_, "forceEnterMissed");
        SetNum(forceEnterPips_, J::Num(policy_, "forceEnterPips", 2));
        JObj^ a = J::Obj(policy_, "accountLimits");
        SetNum(maxPositions_, J::Num(a, "maxOpenPositions", 5));
        SetNum(maxPending_, J::Num(a, "maxPendingOrders", 5));
        SetNum(maxLots_, J::Num(a, "maxTotalLots", 1));
        SetNum(maxDailyLoss_, J::Num(a, "maxDailyLoss", 0));
        SetNum(resetOffset_, J::Num(a, "lossResetUtcOffsetMinutes", 0));
        SetNum(maxRisk_, J::Num(a, "maxRiskPercentPerSignal", 2));
        startMin_->Text = Hm((int)J::Num(a, "tradingStartMinute", 0));
        endMin_->Text = Hm((int)J::Num(a, "tradingEndMinute", 1440));
        limitMap_->Clear();
        JObj^ lim = J::Obj(policy_, "symbolLimits");
        if (lim != nullptr)
            for each (KeyValuePair<String^, Object^> kv in lim) {
                JObj^ l = J::Obj(kv.Value);
                limitMap_[kv.Key] = gcnew array<double>{J::Num(l, "maxSpreadPrice"), J::Num(l, "maxEntryDeviationPrice"), J::Num(l, "maxLots")};
            }
        RebuildSymbols();
        JObj^ ai = J::Obj(policy_, "ai");
        alwaysAssess_->Checked = J::Bool(ai, "alwaysAssess", true);
        SetNum(deadline_, J::Num(ai, "backendGateDeadlineMs", 250));
        SetNum(threshold_, J::Num(ai, "gateAcceptThreshold", 0.6));
        timeoutAction_->SelectedItem = J::Str(ai, "gateTimeoutAction", "review");
        loading_ = false;
        SetStatus(false);
    }

    void OnSave(Object^, EventArgs^) {
        if (policy_ == nullptr) return;
        JObj^ s = J::New();
        s["mode"] = sizingMode_->Text;
        s["lots"] = Val(lots_);
        s["riskPercent"] = Val(risk_);
        policy_["sizing"] = s;
        policy_["allowMessageLot"] = messageLot_->Checked;
        JObj^ t = J::New();
        t["mode"] = targetMode_->Text;
        t["selectedIndex"] = (int)Val(tpIndex_);
        List<Object^>^ allocs = J::List();
        for each (String^ p in allocations_->Text->Split(gcnew array<wchar_t>{',', ';', ' '}, StringSplitOptions::RemoveEmptyEntries)) {
            double v;
            if (!J::TryNum(p, v)) return ShowError("Allocations must be numbers separated by commas");
            allocs->Add(v);
        }
        t["allocations"] = allocs;
        t["singleTargetFallback"] = fallback_->Checked;
        policy_["targets"] = t;
        policy_["maxImmediateAgeSeconds"] = (int)Val(maxAge_);
        policy_["rangeExpirySeconds"] = (int)Val(rangeExpiry_);
        policy_["pendingExpirySeconds"] = (int)Val(pendingExpiry_);
        policy_["futureToleranceSeconds"] = (int)Val(future_);
        policy_["quoteMaxAgeMs"] = (int)Val(quoteAge_);
        policy_["catchUpAfterReconnect"] = catchUp_->Checked;
        policy_["catchUpMaxAgeSeconds"] = (int)Val(catchUpAge_);
        JObj^ sd = J::New();
        sd["enabled"] = semantic_->Checked;
        sd["windowSeconds"] = (int)Val(semanticWindow_);
        policy_["semanticDuplicate"] = sd;
        policy_["forceEnterMissed"] = forceEnter_->Checked;
        policy_["forceEnterPips"] = Val(forceEnterPips_);
        JObj^ a = J::New();
        a["maxOpenPositions"] = (int)Val(maxPositions_);
        a["maxPendingOrders"] = (int)Val(maxPending_);
        a["maxTotalLots"] = Val(maxLots_);
        a["maxDailyLoss"] = Val(maxDailyLoss_);
        a["lossResetUtcOffsetMinutes"] = (int)Val(resetOffset_);
        a["maxRiskPercentPerSignal"] = Val(maxRisk_);
        a["tradingStartMinute"] = ParseHm(startMin_->Text, 0);
        a["tradingEndMinute"] = ParseHm(endMin_->Text, 1440);
        policy_["accountLimits"] = a;
        JObj^ lim = J::New();
        for each (KeyValuePair<String^, array<double>^> kv in limitMap_) {
            JObj^ l = J::New();
            l["maxSpreadPrice"] = kv.Value[0];
            l["maxEntryDeviationPrice"] = kv.Value[1];
            l["maxLots"] = kv.Value[2];
            lim[kv.Key] = l;
        }
        policy_["symbolLimits"] = lim;
        JObj^ ai = J::Obj(policy_, "ai");
        if (ai == nullptr) ai = J::New();
        // endpoint and localModelPath are preserved as-is (backend is shipped with the app; not user-editable here).
        ai["backendGateDeadlineMs"] = (int)Val(deadline_);
        ai["gateAcceptThreshold"] = Val(threshold_);
        ai["gateTimeoutAction"] = timeoutAction_->Text;
        ai["alwaysAssess"] = alwaysAssess_->Checked;
        policy_["ai"] = ai;
        JObj^ p = J::New();
        p["policy"] = policy_;
        Client->Call("policy.save", p, gcnew ResponseHandler(this, &PolicyPage::OnSaved));
    }

    void OnSaved(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError("Not saved: " + error);
        OnReload(nullptr, nullptr);  // reloads saved values and clears the unsaved-changes flag
    }

    void OnTestAi(Object^, EventArgs^) { Client->Call("ai.test", nullptr, gcnew ResponseHandler(this, &PolicyPage::OnAiTested)); }

    void OnAiTested(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        JObj^ r = J::Obj(result);
        String^ reasons = "";
        for each (Object^ o in J::Arr(r, "reasons")) reasons += "- " + Convert::ToString(o) + "\n";
        ShowInfo("AI endpoint test", "Verdict: " + J::Str(r, "verdict") + "\nModel: " + J::Str(r, "modelVersion") + "\nLatency: " + J::Fmt(J::Num(r, "latencyMs"), 0) + " ms\n" +
                                         (J::Str(r, "error") != "" ? "Error: " + J::Str(r, "error") + "\n" : "") + reasons);
    }
};

}  // namespace PipkiteUI
