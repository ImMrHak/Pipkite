#pragma once
// Dashboard: connections, bound account, execution mode, live feed, reviews, exposure, errors,
// latency percentiles, profit cards and charts. Entirely local; useful without the backend.

#include "Pages.h"

namespace SignalRelayUI {

public delegate void DetailRequest(String^ chatId, String^ messageId, int revision);
public delegate void GoToPage(int pageIndex);

public ref class DashboardPage : public PageBase {
public:
    static DetailRequest^ OpenDetail;
    static GoToPage^ Navigate;  // jump to a page from the setup checklist

    DashboardPage() {
        // status cards (row 1) and account/profit cards (row 2); widths fill the row evenly
        cards_ = gcnew FlowLayoutPanel();
        cards_->Dock = DockStyle::Top;
        cards_->AutoSize = true;
        cards_->Padding = Windows::Forms::Padding(Theme::Gutter, 4, Theme::Gutter - 12, 0);
        telegram_ = gcnew StatCard("Telegram", L"\xE8BD");
        account_ = gcnew StatCard("MT5 account", L"\xE7F4");
        mode_ = gcnew StatCard("Execution", L"\xE768");
        health_ = gcnew StatCard("Journal", L"\xE9D9");
        latency_ = gcnew StatCard("Latency p95", L"\xE945");
        statusCards_ = gcnew array<StatCard^>{telegram_, account_, mode_, health_, latency_};
        for each (StatCard^ c in statusCards_) cards_->Controls->Add(c);
        cards_->Resize += gcnew EventHandler(this, &DashboardPage::OnCardsResize);

        profit_ = gcnew FlowLayoutPanel();
        profit_->Dock = DockStyle::Top;
        profit_->AutoSize = true;
        profit_->Padding = Windows::Forms::Padding(Theme::Gutter, 0, Theme::Gutter - 12, 0);
        balance_ = gcnew StatCard("Balance");
        equity_ = gcnew StatCard("Equity");
        free_ = gcnew StatCard("Free margin");
        floating_ = gcnew StatCard("Floating P/L");
        today_ = gcnew StatCard("Copier today");
        week_ = gcnew StatCard("Copier 7 days");
        exposure_ = gcnew StatCard("Open exposure");
        profitCards_ = gcnew array<StatCard^>{balance_, equity_, free_, floating_, today_, week_, exposure_};
        for each (StatCard^ c in profitCards_) profit_->Controls->Add(c);
        profit_->Resize += gcnew EventHandler(this, &DashboardPage::OnCardsResize);
        freshness_ = gcnew Badge();
        freshness_->Margin = Windows::Forms::Padding(0, 13, 12, 6);
        Label^ profitTitle = L::Text("Account and profit", "h2");
        L::Tip(profitTitle, "Copier P/L is net of commission, swap and fees; deposits are excluded.");
        FlowLayoutPanel^ profitHeader = L::Row(profitTitle, freshness_);
        profitHeader->WrapContents = false;
        profitHeader->Padding = Windows::Forms::Padding(Theme::Gutter, 0, 8, 0);

        SplitContainer^ split = gcnew SplitContainer();
        split->Dock = DockStyle::Fill;
        split->Orientation = Orientation::Vertical;
        split->FixedPanel = FixedPanel::Panel2;
        split->SplitterWidth = 12;
        split->Panel1->Padding = Windows::Forms::Padding(Theme::Gutter, 8, 0, 16);
        split->Panel2->Padding = Windows::Forms::Padding(0, 8, Theme::Gutter, 16);
        split_ = split;

        // live feed
        Label^ feedTitle = L::Text("Live feed", "h2");
        L::Tip(feedTitle, "Double-click a row for details. Right-click the header to choose columns.");
        FlowLayoutPanel^ feedHeader = L::Row(feedTitle);
        feedHeader->WrapContents = false;
        feed_ = gcnew BufferedGrid();
        feed_->Dock = DockStyle::Fill;
        feed_->AccessibleName = "Live signal feed";
        feed_->ColumnChooser = true;
        feed_->EmptyText = "No signals yet. Add a Telegram source and keep SignalRelay running; new messages from your chats appear here. "
                           "Use Signal rules to test a sample without sending anything.";
        feed_->AddCol("time", "Time", 76);
        feed_->AddCol("source", "Source", 140);
        feed_->AddCol("symbol", "Symbol", 110);
        feed_->AddCol("side", "Side", 64);
        feed_->AddCol("lots", "Lots", 60);
        feed_->AddCol("entry", "Entry", 130);
        feed_->AddCol("sl", "SL", 80);
        feed_->AddCol("tp", "TP", 120);
        feed_->AddCol("state", "State", 100);
        feed_->AddCol("command", "Command", 100)->Visible = false;
        feed_->AddCol("fill", "Requested / filled", 120)->Visible = false;
        feed_->AddCol("tickets", "Tickets", 100)->Visible = false;
        feed_->AddCol("spread", "Spread", 70)->Visible = false;
        feed_->AddCol("latency", "Latency", 80);
        feed_->AddCol("ai", "AI verdict", 130);  // backend opinion, shown by default
        feed_->AddCol("parser", "Parser", 140)->Visible = false;
        DataGridViewTextBoxColumn^ reason = feed_->AddCol("reason", "Reason", 320);
        reason->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        reason->MinimumWidth = 200;
        feed_->CellDoubleClick += gcnew DataGridViewCellEventHandler(this, &DashboardPage::OnFeedDoubleClick);
        feed_->KeyDown += gcnew KeyEventHandler(this, &DashboardPage::OnFeedKey);
        feed_->CellFormatting += gcnew DataGridViewCellFormattingEventHandler(this, &DashboardPage::OnCellFormatting);
        split->Panel1->Controls->Add(feed_);
        split->Panel1->Controls->Add(feedHeader);

        // right column: activity, latency, errors, charts
        FlowLayoutPanel^ right = L::Stack();
        right->Padding = Windows::Forms::Padding(0);
        TableLayoutPanel^ activity = L::Form2();
        pendingVal_ = AddStat(activity, "Pending reviews");
        waitingVal_ = AddStat(activity, "Waiting for entry range");
        inflightVal_ = AddStat(activity, "In flight");
        unresolvedVal_ = AddStat(activity, "Unresolved commands");
        signalsVal_ = AddStat(activity, "Signals stored");
        dealsVal_ = AddStat(activity, "Deals");
        right->Controls->Add(L::Group("Activity", activity));

        TableLayoutPanel^ lat = gcnew TableLayoutPanel();
        lat->ColumnCount = 4;
        lat->AutoSize = true;
        lat->AutoSizeMode = Windows::Forms::AutoSizeMode::GrowAndShrink;
        for (int i = 0; i < 4; ++i) lat->ColumnStyles->Add(gcnew ColumnStyle(SizeType::AutoSize));
        for each (String^ h in gcnew array<String^>{"", "p50", "p95", "p99"}) lat->Controls->Add(LatCell(h, true, h == ""));
        array<String^>^ latNames = {"Parse (us)", "Decision (ms)", "Journal (ms)", "Telegram to EA (ms)", "Broker (ms)"};
        latCells_ = gcnew array<Label^, 2>(5, 3);
        for (int r = 0; r < 5; ++r) {
            lat->Controls->Add(LatCell(latNames[r], true, true));
            for (int c = 0; c < 3; ++c) {
                latCells_[r, c] = LatCell("-", false, false);
                lat->Controls->Add(latCells_[r, c]);
            }
        }
        FlowLayoutPanel^ latBox = gcnew FlowLayoutPanel();
        latBox->FlowDirection = FlowDirection::TopDown;
        latBox->AutoSize = true;
        latBox->Controls->Add(lat);
        right->Controls->Add(L::Group("Latency", latBox));

        errors_ = gcnew NeoListBox();
        errors_->Width = 300;
        errors_->Height = 116;
        errors_->AccessibleName = "Recent errors";
        right->Controls->Add(L::Group("Recent errors", errors_));

        equityChart_ = gcnew LineChart();
        equityChart_->Title = "Equity and balance (includes deposits)";
        equityChart_->NameA = "Equity";
        equityChart_->NameB = "Balance";
        equityChart_->Size = Drawing::Size(330, 190);
        equityChart_->Margin = Windows::Forms::Padding(0, 0, 0, 16);
        right->Controls->Add(equityChart_);
        realizedChart_ = gcnew LineChart();
        realizedChart_->Title = "Realized copier P/L (net of fees)";
        realizedChart_->NameA = "Realized net";
        realizedChart_->Size = Drawing::Size(330, 190);
        right->Controls->Add(realizedChart_);
        split->Panel2->Controls->Add(right);

        Controls->Add(split);
        Controls->Add(profit_);
        Controls->Add(profitHeader);
        Controls->Add(cards_);
        Header("Dashboard", "Live status of copying. Broker data is authoritative; simulated terminals are labeled.", this);

        chartTimer_ = gcnew Windows::Forms::Timer();
        chartTimer_->Interval = 10000;
        chartTimer_->Tick += gcnew EventHandler(this, &DashboardPage::OnChartTimer);
        chartTimer_->Start();
    }

    virtual void OnActivated() override {
        OnChartTimer(nullptr, nullptr);
        try {
            if (split_->Width > 700) split_->SplitterDistance = Math::Max(300, split_->Width - 396);
        } catch (InvalidOperationException^) {
        }
    }

    virtual void OnSnapshot(JObj^ s) override {
        PageBase::OnSnapshot(s);
        if (!Visible) return;
        JObj^ tg = J::Obj(s, "telegram");
        String^ tgs = J::Str(tg, "state");
        telegram_->Set(TelegramText(tgs), J::Str(tg, "user") != "" ? J::Str(tg, "user") : J::Str(tg, "error"),
                       tgs == "Ready" ? Theme::Success : (tgs == "LibraryMissing" || tgs == "Error" || tgs == "Closed") ? Theme::Danger : Theme::Warning);
        JObj^ br = J::Obj(s, "bridge");
        JObj^ acc = J::Obj(s, "account");
        JObj^ bound = J::Obj(s, "boundAccount");
        if (acc != nullptr) {
            JObj^ key = J::Obj(acc, "account");
            String^ sim = J::Bool(acc, "simulated") ? " (SIMULATED)" : "";
            account_->Set("#" + J::Str(key, "login") + sim, J::Str(key, "server") + (J::Bool(br, "healthy") ? " - connected" : " - heartbeat missing"),
                          J::Bool(br, "healthy") ? (J::Bool(acc, "simulated") ? Theme::Warning : Theme::Success) : Theme::Danger);
        } else if (bound != nullptr) {
            account_->Set("#" + J::Str(bound, "login"), J::Str(bound, "server") + " - terminal not connected", Theme::Warning);
        } else {
            account_->Set("Not paired", J::Str(br, "status"), Theme::TextMuted);
        }
        String^ mode = J::Str(s, "mode");
        bool paused = J::Bool(s, "paused");
        String^ blocked = J::Str(s, "dispatchBlockedReason");
        mode_->Set(paused ? "Paused" : "Live", blocked == "" ? "Ready to copy" : blocked, paused ? Theme::Warning : Theme::Success);
        JObj^ h = J::Obj(s, "health");
        bool journalOk = J::Bool(h, "journalHealthy", true);
        health_->Set(journalOk ? "Journal OK" : "Journal FAILED", J::Int(h, "diskFreeMb").ToString("N0") + " MB free" + (J::Arr(h, "staleQuotes")->Length > 0 ? " - stale quotes" : ""),
                     journalOk ? Theme::Success : Theme::Danger);
        JObj^ m = J::Obj(s, "metrics");
        JObj^ e2e = J::Obj(m, "e2eMs");
        if (J::Int(e2e, "n") > 0) latency_->AddSpark(J::Num(e2e, "p50"));
        latency_->Set(J::Int(e2e, "n") > 0 ? J::Fmt(J::Num(e2e, "p95"), 1) + " ms" : "-", J::Int(e2e, "n") > 0 ? "p50 " + J::Fmt(J::Num(e2e, "p50"), 1) + " ms, n=" + J::Str(e2e, "n") : "no executions measured yet",
                      Theme::Text);
        array<JObj^>^ lm = {J::Obj(m, "parseUs"), J::Obj(m, "decisionMs"), J::Obj(m, "journalMs"), e2e, J::Obj(m, "brokerMs")};
        for (int r = 0; r < 5; ++r) {
            const bool has = J::Int(lm[r], "n") > 0;
            SetText(latCells_[r, 0], has ? J::Fmt(J::Num(lm[r], "p50"), 2) : "-");
            SetText(latCells_[r, 1], has ? J::Fmt(J::Num(lm[r], "p95"), 2) : "-");
            SetText(latCells_[r, 2], has ? J::Fmt(J::Num(lm[r], "p99"), 2) : "-");
        }

        JObj^ p = J::Obj(s, "profit");
        String^ cur = J::Str(p, "currency");
        bool fresh = J::Bool(p, "fresh");
        balance_->Set(Money(J::Num(p, "balance"), cur), "", Theme::Text);
        if (acc != nullptr) equity_->AddSpark(J::Num(p, "equity"));
        equity_->Set(Money(J::Num(p, "equity"), cur), "", Theme::Text);
        free_->Set(Money(J::Num(p, "freeMargin"), cur), "", Theme::Text);
        double fl = J::Num(p, "floating");
        if (acc != nullptr) floating_->AddSpark(fl);
        floating_->Set(Money(fl, cur), "", fl >= 0 ? Theme::Success : Theme::Danger);
        JObj^ t = J::Obj(p, "copierToday");
        JObj^ w = J::Obj(p, "copierWeek");
        today_->AddSpark(J::Num(t, "net"));
        today_->Set(Money(J::Num(t, "net"), cur), "fees " + J::Fmt(J::Num(t, "commission") + J::Num(t, "fee") + J::Num(t, "swap"), 2) + (J::Int(t, "unresolved") > 0 ? ", unresolved " + J::Str(t, "unresolved") : ""),
                    J::Num(t, "net") >= 0 ? Theme::Success : Theme::Danger);
        week_->Set(Money(J::Num(w, "net"), cur), J::Str(w, "deals") + " deals", J::Num(w, "net") >= 0 ? Theme::Success : Theme::Danger);
        exposure_->Set(J::Fmt(J::Num(p, "copierOpenLots"), 2) + " lots", J::Str(p, "copierOpenPositions") + " positions", Theme::Text);
        freshness_->Set(fresh ? "Live" : (acc == nullptr ? "Offline" : "Stale - not live"), fresh ? Theme::Success : Theme::Warning);

        JObj^ c = J::Obj(s, "counts");
        SetText(pendingVal_, J::Str(c, "pendingReview", "0"));
        Color pendingColor = J::Str(c, "pendingReview", "0") != "0" ? Theme::Warning : Theme::Text;
        if (pendingVal_->ForeColor != pendingColor) pendingVal_->ForeColor = pendingColor;
        SetText(waitingVal_, J::Str(c, "waiting", "0"));
        SetText(inflightVal_, J::Str(c, "inflight", "0"));
        SetText(unresolvedVal_, J::Str(c, "unresolvedCommands", "0"));
        SetText(signalsVal_, J::Str(c, "signals", "0"));
        SetText(dealsVal_, J::Str(c, "deals", "0"));

        String^ errKey = J::Stringify(J::Get(s, "errors"));
        if (errKey != lastErrors_) {
            lastErrors_ = errKey;
            errors_->BeginUpdate();
            errors_->Items->Clear();
            for each (Object^ o in J::Arr(s, "errors")) {
                JObj^ e = J::Obj(o);
                errors_->Items->Add(J::ShortTime(J::Int(e, "at")) + "  " + J::Str(e, "message"));
            }
            if (errors_->Items->Count == 0) errors_->Items->Add("No recent errors");
            errors_->EndUpdate();
        }
        UpdateFeed(J::Arr(s, "feed"));
    }

private:
    StatCard ^ telegram_, ^account_, ^mode_, ^health_, ^latency_, ^balance_, ^equity_, ^free_, ^floating_, ^today_, ^week_, ^exposure_;
    array<StatCard^> ^ statusCards_, ^profitCards_;
    FlowLayoutPanel ^ cards_, ^profit_;
    Badge^ freshness_;
    BufferedGrid^ feed_;
    Label ^ pendingVal_, ^waitingVal_, ^inflightVal_, ^unresolvedVal_, ^signalsVal_, ^dealsVal_;
    array<Label^, 2>^ latCells_;
    ListBox^ errors_;
    LineChart ^ equityChart_, ^realizedChart_;
    Windows::Forms::Timer^ chartTimer_;
    SplitContainer^ split_;
    String^ lastFeed_;
    String^ lastErrors_;
    array<Object^>^ feedItems_;

    static String^ TelegramText(String^ s) {
        if (s == "Ready") return "Connected";
        if (s == "LibraryMissing") return "TDLib missing";
        if (s == "NotConfigured") return "Not configured";
        if (s == "WaitPhone" || s == "WaitCode" || s == "WaitPassword") return "Sign-in needed";
        return s;
    }
    static String^ Money(double v, String^ cur) { return v.ToString("N2") + (String::IsNullOrEmpty(cur) ? "" : " " + cur); }
    static void SetText(Label^ l, String^ t) {
        if (l->Text != t) l->Text = t;  // avoid relayout when nothing changed
    }

    static Label^ AddStat(TableLayoutPanel^ t, String^ name) {
        Label^ k = L::Text(name, "muted");
        k->Margin = Windows::Forms::Padding(0, 4, 24, 4);
        Label^ v = L::Text("-");
        v->Font = Theme::BodyBold;
        v->Margin = Windows::Forms::Padding(0, 4, 0, 4);
        t->Controls->Add(k);
        t->Controls->Add(v);
        return v;
    }
    static Label^ LatCell(String^ text, bool muted, bool name) {
        Label^ l = muted ? L::Text(text, "muted") : L::Text(text);
        l->AutoSize = false;
        l->Size = Drawing::Size(name ? 132 : 50, 24);
        l->TextAlign = name ? ContentAlignment::MiddleLeft : ContentAlignment::MiddleRight;
        l->Margin = Windows::Forms::Padding(0, 0, 2, 0);
        return l;
    }
    // Cards share the row width evenly (min 120 px), so both rows line up at any window size.
    void OnCardsResize(Object^ sender, EventArgs^) {
        FlowLayoutPanel^ row = safe_cast<FlowLayoutPanel^>(sender);
        array<StatCard^>^ cards = row == cards_ ? statusCards_ : profitCards_;
        const int gap = 12;
        const int avail = row->ClientSize.Width - row->Padding.Horizontal;
        const int w = Math::Max(Theme::Scale(row, 120), (avail - gap * cards->Length) / cards->Length);  // every card has a 12 px right margin
        row->SuspendLayout();
        for each (StatCard^ c in cards)
            if (c->Width != w) c->Width = w;
        row->ResumeLayout();
    }

    void UpdateFeed(array<Object^>^ items) {
        String^ key = J::Stringify(items);
        if (key == lastFeed_) return;  // repaint only when data changed
        lastFeed_ = key;
        feedItems_ = items;
        String^ selected = feed_->SelectedRows->Count > 0 ? dynamic_cast<String^>(feed_->SelectedRows[0]->Tag) : nullptr;
        feed_->SuspendLayout();
        feed_->Rows->Clear();
        for each (Object^ o in items) {
            JObj^ f = J::Obj(o);
            String^ entry = "";
            array<Object^>^ en = J::Arr(f, "entry");
            if (en->Length == 2) {
                double lo = Convert::ToDouble(en[0]), hi = Convert::ToDouble(en[1]);
                entry = lo == hi ? J::Fmt(lo) : J::Fmt(lo) + " - " + J::Fmt(hi);
            }
            String^ tps = "";
            for each (Object^ tp in J::Arr(f, "tps")) tps += (tps == "" ? "" : ", ") + J::Fmt(Convert::ToDouble(tp));
            String^ tickets = "";
            for each (Object^ tk in J::Arr(f, "tickets")) tickets += (tickets == "" ? "" : ",") + Convert::ToString(tk);
            String^ fill = J::Has(f, "requested") ? J::Fmt(J::Num(f, "requested"), 2) + " / " + J::Fmt(J::Num(f, "filled"), 2) : "";
            String^ symbol = J::Str(f, "symbol") == "" ? "-" : J::Str(f, "symbol") + (J::Bool(f, "simulated") ? " (sim)" : "");
            int idx = feed_->Rows->Add(gcnew array<Object^>{J::ShortTime(J::Int(f, "at")), J::Str(f, "source"), symbol, J::Str(f, "side"), J::Has(f, "lots") ? J::Fmt(J::Num(f, "lots"), 2) : "",
                                                              entry, J::Has(f, "sl") ? J::Fmt(J::Num(f, "sl")) : "", tps, J::Str(f, "state"), J::Str(f, "commandState"), fill, tickets,
                                                              J::Has(f, "spread") ? J::Fmt(J::Num(f, "spread")) : "", J::Has(f, "latencyMs") ? J::Fmt(J::Num(f, "latencyMs"), 1) + " ms" : "",
                                                              AiText(f), J::Str(f, "parser"), J::Str(f, "reason")});
            feed_->Rows[idx]->Tag = J::Str(f, "key");
            feed_->Rows[idx]->Cells["ai"]->ToolTipText = AiTooltip(f);  // full verdict + reasons on hover
            if (selected != nullptr && selected == J::Str(f, "key")) feed_->Rows[idx]->Selected = true;
        }
        feed_->ResumeLayout();
    }

    void OnCellFormatting(Object^, DataGridViewCellFormattingEventArgs^ e) {
        String^ col = feed_->Columns[e->ColumnIndex]->Name;
        if (col == "state" || col == "command") e->CellStyle->ForeColor = Theme::StateColor(dynamic_cast<String^>(e->Value));
        else if (col == "ai") e->CellStyle->ForeColor = AiColor(dynamic_cast<String^>(e->Value));
    }

    // AI verdict as shown in the feed: "Good 0.82", "Uncertain", "Checking...", "-".
    static String^ AiText(JObj^ f) {
        String^ v = J::Str(f, "ai");
        if (v == "" || v == "Off") return "-";
        if (v == "Pending") return "Checking...";
        JObj^ d = J::Obj(f, "aiDetail");
        if (d != nullptr && J::Has(d, "score")) return v + " " + J::Fmt(J::Num(d, "score"), 2);
        return v;
    }
    static Color AiColor(String^ text) {
        if (text == nullptr) return Theme::TextMuted;
        if (text->StartsWith("Good")) return Theme::Success;
        if (text->StartsWith("Bad")) return Theme::Danger;
        if (text->StartsWith("Uncertain")) return Theme::Warning;
        if (text->StartsWith("Checking")) return Theme::Accent;
        return Theme::TextMuted;
    }
    static String^ AiTooltip(JObj^ f) {
        JObj^ d = J::Obj(f, "aiDetail");
        if (d == nullptr) {
            String^ v = J::Str(f, "ai");
            return v == "Pending" ? "Waiting for the backend's assessment..." : v == "Off" || v == "" ? "No AI assessment for this signal." : v;
        }
        String^ t = "Verdict: " + J::Str(d, "verdict");
        if (J::Has(d, "score")) t += "   Score: " + J::Fmt(J::Num(d, "score"), 2) + (J::Bool(d, "scoreCalibrated") ? " (calibrated)" : "");
        if (J::Str(d, "provider") != "") t += "   (" + J::Str(d, "provider") + ")";
        for each (Object^ r in J::Arr(d, "reasons")) t += "\n- " + Convert::ToString(r);
        if (J::Str(d, "unavailableReason") != "") t += "\n" + J::Str(d, "unavailableReason");
        return t;
    }

    void OpenSelected(int row) {
        if (row < 0 || feedItems_ == nullptr || row >= feedItems_->Length) return;
        JObj^ f = J::Obj(feedItems_[row]);
        if (OpenDetail != nullptr) OpenDetail(J::Str(f, "chatId"), J::Str(f, "messageId"), (int)J::Int(f, "revision"));
    }
    void OnFeedDoubleClick(Object^, DataGridViewCellEventArgs^ e) { OpenSelected(e->RowIndex); }
    void OnFeedKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode == Keys::Enter && feed_->CurrentRow != nullptr) {
            OpenSelected(feed_->CurrentRow->Index);
            e->Handled = true;
        }
    }

    void OnChartTimer(Object^, EventArgs^) {
        if (Client == nullptr || !Client->Connected || !Visible) return;
        JObj^ p = J::New();
        p["fromMs"] = DateTimeOffset::UtcNow.AddDays(-7).ToUnixTimeMilliseconds();
        Client->Call("profit.curve", p, gcnew ResponseHandler(this, &DashboardPage::OnCurve));
    }

    // X is minutes relative to baseMs: raw epoch minutes (~3e7) exceed float precision and collapse nearby points.
    static void Fill(List<PointF>^ dst, array<Object^>^ src, double baseMs) {
        dst->Clear();
        for each (Object^ o in src) {
            array<Object^>^ pt = J::Arr(o);
            if (pt->Length != 2) continue;
            double x = (Convert::ToDouble(pt[0]) - baseMs) / 60000.0, y = Convert::ToDouble(pt[1]);
            if (Double::IsNaN(x) || Double::IsInfinity(x) || Double::IsNaN(y) || Double::IsInfinity(y)) continue;
            dst->Add(PointF((float)x, (float)y));
        }
    }

    void OnCurve(bool ok, Object^ result, String^) {
        if (!ok) return;
        JObj^ r = J::Obj(result);
        const double baseMs = (double)DateTimeOffset::UtcNow.AddDays(-8).ToUnixTimeMilliseconds();
        Fill(equityChart_->SeriesA, J::Arr(r, "equity"), baseMs);
        Fill(equityChart_->SeriesB, J::Arr(r, "balance"), baseMs);
        Fill(realizedChart_->SeriesA, J::Arr(r, "realized"), baseMs);
        equityChart_->Invalidate();
        realizedChart_->Invalidate();
    }
};

}  // namespace SignalRelayUI
