#pragma once
// MetaTrader: terminal selection, one-click start with the EA attached (optionally automatic), pairing, account identity,
// broker connection, symbol specifications and symbol mapping, and explicit copier-owned actions.

#include "ChannelDialog.h"
#include "Pages.h"

namespace PipkiteUI {

public ref class MetaTraderPage : public PageBase {
public:
    MetaTraderPage() {
        FlowLayoutPanel^ stack = L::Stack();

        // terminal: pick, start with the EA attached, optionally at every Pipkite start
        FlowLayoutPanel^ term = gcnew FlowLayoutPanel();
        term->FlowDirection = FlowDirection::TopDown;
        term->AutoSize = true;
        terminals_ = gcnew BufferedGrid();
        terminals_->Width = 960;
        terminals_->Height = 110;
        terminals_->AddCol("origin", "Your MetaTrader terminal", 420)->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        terminals_->AddCol("state", "Status", 240);
        terminals_->EmptyText = "Open MetaTrader 5 and it will appear here.";
        terminals_->AccessibleName = "Your MetaTrader 5 terminals";
        term->Controls->Add(terminals_);
        Button^ start = L::Btn("Start MetaTrader", "accent", "Open MetaTrader with the Pipkite EA attached. Log in to your broker and Pipkite connects and pairs your account automatically. Nothing is traded until you turn on Automatic.");
        start->Click += gcnew EventHandler(this, &MetaTraderPage::OnStart);
        Button^ detect = L::Btn("Refresh", nullptr, "Look again for open MetaTrader 5 terminals.");
        detect->Click += gcnew EventHandler(this, &MetaTraderPage::OnDetect);
        Button^ browse = L::Btn("Choose folder...", nullptr, "MetaTrader not listed? Choose the folder it is installed in (the one with terminal64.exe). Pipkite checks it can work with it.");
        browse->Click += gcnew EventHandler(this, &MetaTraderPage::OnBrowse);
        symbol_ = L::Box(110);  // EA chart symbol: internal only, not shown to the user
        symbol_->Text = "EURUSD";
        term->Controls->Add(L::Row(start, detect, browse));
        autoLaunch_ = L::Check("Start MetaTrader automatically when Pipkite starts");
        autoLaunch_->CheckedChanged += gcnew EventHandler(this, &MetaTraderPage::OnAutoLaunchChanged);
        term->Controls->Add(autoLaunch_);
        terminalStatus_ = L::Wrap("", nullptr, 960);
        term->Controls->Add(terminalStatus_);
        stack->Controls->Add(L::Group("MetaTrader 5 terminal", term));

        // connection & pairing
        TableLayoutPanel^ conn = L::Form2();
        bridge_ = gcnew Badge();
        L::AddField(conn, "Bridge", bridge_);
        bound_ = L::Text("-");
        L::AddField(conn, "Bound account", bound_);
        pending_ = L::Text("-");
        L::AddField(conn, "Pairing request", pending_);
        Button^ approve = L::Btn("Approve pairing...", "accent", "Confirm the account MetaTrader connected with, by typing its number, to bind Pipkite to it.");
        approve->Click += gcnew EventHandler(this, &MetaTraderPage::OnApprove);
        Button^ unbind = L::Btn("Unbind account", "danger", "Disconnect the bound account. Copying pauses until you pair again. Open trades stay at the broker.");
        unbind->Click += gcnew EventHandler(this, &MetaTraderPage::OnUnbind);
        conn->Controls->Add(gcnew Label());
        conn->Controls->Add(L::Row(approve, unbind));
        Button^ closeAll = L::Btn("Close Pipkite positions...", "danger",
                                  "Close every open position Pipkite placed on the bound account, at market. Positions you opened yourself are not touched. "
                                  "You confirm by typing the account number.");
        closeAll->Click += gcnew EventHandler(this, &MetaTraderPage::OnClosePositions);
        Button^ cancelAll = L::Btn("Cancel Pipkite pending orders...", nullptr,
                                   "Cancel every pending order Pipkite placed on the bound account. Your own orders are not touched. You confirm by typing the "
                                   "account number.");
        cancelAll->Click += gcnew EventHandler(this, &MetaTraderPage::OnCancelPending);
        conn->Controls->Add(gcnew Label());
        conn->Controls->Add(L::Row(closeAll, cancelAll));
        impl_ = L::Text("", "muted");
        conn->Controls->Add(gcnew Label());
        conn->Controls->Add(impl_);
        stack->Controls->Add(L::Group("Connection and account", conn));

        // per-channel symbols: one row per Telegram channel, pick the broker symbol it trades
        FlowLayoutPanel^ sym = gcnew FlowLayoutPanel();
        sym->FlowDirection = FlowDirection::TopDown;
        sym->AutoSize = true;
        sym->Controls->Add(L::Wrap("Only for a channel that posts one instrument: click its Symbol cell and pick the broker symbol it trades. Other channels "
                                   "trade each message's own symbol, found in Execution policy > Symbol limits.",
                                   nullptr, 720));
        catalog_ = gcnew System::Collections::Generic::List<String^>();
        sources_ = gcnew System::Collections::Generic::List<JObj^>();
        chanSym_ = gcnew BufferedGrid();  // read-only cells; clicking the Symbol cell opens a searchable picker
        chanSym_->AllowUserToAddRows = false;
        chanSym_->Width = 720;
        chanSym_->Height = 300;
        chanSym_->AddCol("channel", "Telegram channel", 400);
        chanSym_->AddCol("symbol", "Symbol to trade (click to choose)", 300);
        chanSym_->AccessibleName = "Per-channel trade symbols";
        chanSym_->CellClick += gcnew DataGridViewCellEventHandler(this, &MetaTraderPage::OnChanSymClick);
        sym->Controls->Add(chanSym_);
        Button^ saveSym = L::Btn("Save", "accent", "Save the symbols you changed. Other channel settings are kept.");
        saveSym->Click += gcnew EventHandler(this, &MetaTraderPage::OnSaveChannelSymbols);
        Button^ refreshSym = L::Btn("Refresh", nullptr, "Reload channels and symbols.");
        refreshSym->Click += gcnew EventHandler(this, &MetaTraderPage::OnRefreshChannels);
        sym->Controls->Add(L::Row(saveSym, refreshSym));
        chanStatus_ = L::Wrap("", "muted", 720);
        sym->Controls->Add(chanStatus_);
        stack->Controls->Add(L::Group("Channel symbols", sym));

        Controls->Add(stack);
        Header("MetaTrader", "Connect one MetaTrader 5 terminal and account through the Pipkite EA.", this);
    }

    virtual void OnActivated() override {
        if (Client == nullptr || !Client->Connected) return;
        Client->Call("settings.get", nullptr, gcnew ResponseHandler(this, &MetaTraderPage::OnSettings));
        Client->Call("mt5.status", nullptr, gcnew ResponseHandler(this, &MetaTraderPage::OnStatus));
        Client->Call("sources.list", nullptr, gcnew ResponseHandler(this, &MetaTraderPage::OnSources));  // channels -> rows
    }

    virtual void OnSnapshot(JObj^ s) override {
        PageBase::OnSnapshot(s);
        if (!Visible) return;
        JObj^ br = J::Obj(s, "bridge");
        bool connected = J::Bool(br, "connected");
        bool paired = J::Bool(br, "paired");
        bool healthy = J::Bool(br, "healthy");
        String^ text = !connected ? "Not connected" : !paired ? "Connected - pairing required" : healthy ? "Paired and healthy" : "Paired - heartbeat missing";
        bridge_->Set(text, !connected ? Theme::TextMuted : (paired && healthy) ? Theme::Success : Theme::Warning);
        JObj^ b = J::Obj(s, "boundAccount");
        bound_->Text = b == nullptr ? "None" : "#" + J::Str(b, "login") + " @ " + J::Str(b, "server") + "  (terminal " + J::Str(b, "terminalId") + ")";
        pendingKey_ = J::Obj(br, "pairingPending");
        pending_->Text = pendingKey_ == nullptr ? "None" : "#" + J::Str(pendingKey_, "login") + " @ " + J::Str(pendingKey_, "server") + " is waiting for approval";
        impl_->Text = "Bridge implementation: " + J::Str(br, "implVersion", "-") + "   Heartbeat: " + (J::Int(br, "heartbeatAgoMs") >= 0 ? J::Str(br, "heartbeatAgoMs") + " ms ago" : "-") +
                      "   " + J::Str(br, "status");
        // the last start's message ("MetaTrader is starting...") is over once the EA is connected and paired
        String^ ts = connected && paired ? "" : J::Str(J::Obj(s, "terminal"), "status");
        if (terminalStatus_->Text != ts) terminalStatus_->Text = ts;
        if (verifyUntil_ != DateTime::MinValue && (connected || DateTime::Now > verifyUntil_)) {
            verifyUntil_ = DateTime::MinValue;
            if (connected) ShowInfo("MetaTrader connected", "Connection verified: MetaTrader and the Pipkite EA are talking to Pipkite.");
            else ShowError("MetaTrader did not connect to Pipkite within 2 minutes. In MetaTrader check that:\n\n"
                           "  -  you are logged in to your broker account\n"
                           "  -  the Pipkite EA is on a chart (smiley face at the top right)\n"
                           "  -  Algo Trading is on and \"Allow DLL imports\" is ticked (Tools > Options > Expert Advisors)\n"
                           "  -  MetaTrader runs under the same Windows user as Pipkite");
        }
        if ((DateTime::Now - lastStatus_).TotalSeconds > 2) {
            lastStatus_ = DateTime::Now;
            Client->Call("mt5.status", nullptr, gcnew ResponseHandler(this, &MetaTraderPage::OnStatus));
        }
    }

private:
    BufferedGrid^ terminals_;
    TextBox^ symbol_;
    CheckBox^ autoLaunch_;
    Label ^ terminalStatus_, ^pairSteps_;
    String^ savedDataPath_;
    String^ chosenExe_;     // terminal picked with Choose folder: selected after the next refresh
    DateTime verifyUntil_;  // after Start: wait for the bridge to connect until then (MinValue = not waiting)
    bool loading_;
    Badge^ bridge_;
    Label ^ bound_, ^pending_, ^impl_, ^chanStatus_;
    BufferedGrid^ chanSym_;
    System::Collections::Generic::List<String^>^ catalog_;
    System::Collections::Generic::List<JObj^>^ sources_;
    JObj^ pendingKey_;
    DateTime lastStatus_;
    int pendingSaves_;
    String^ saveError_;

    void OnDetect(Object^, EventArgs^) { Client->Call("mt5.detectTerminals", nullptr, gcnew ResponseHandler(this, &MetaTraderPage::OnTerminals)); }

    void OnSettings(bool ok, Object^ result, String^) {
        if (!ok) return;
        JObj^ st = J::Obj(result);
        loading_ = true;
        symbol_->Text = J::Str(st, "mt5.chartSymbol", "EURUSD");
        autoLaunch_->Checked = J::Str(st, "mt5.autoLaunch", "false") == "true";
        loading_ = false;
        savedDataPath_ = J::Str(st, "mt5.dataPath");
        OnDetect(nullptr, nullptr);
    }

    void OnTerminals(bool ok, Object^ result, String^ error) {
        if (!ok) return;
        terminals_->Rows->Clear();
        for each (Object^ o in J::Arr(result)) {
            JObj^ t = J::Obj(o);
            String^ state = !J::Bool(t, "exeFound")       ? "terminal64.exe not found"
                            : J::Bool(t, "needsFirstRun") ? "Installed, never opened: Start opens it once to finish setup"
                            : J::Bool(t, "running")       ? "Running"
                                                          : "Not running";
            if (J::Bool(t, "hasEA") && J::Bool(t, "hasDll")) state += "  ·  EA installed";
            int idx = terminals_->Rows->Add(gcnew array<Object^>{J::Str(t, "origin") == "" ? "(unknown installation)" : J::Str(t, "origin"), state});
            terminals_->Rows[idx]->Tag = t;
            if (chosenExe_ != nullptr ? J::Str(t, "exePath") == chosenExe_ : J::Str(t, "dataPath") == savedDataPath_) terminals_->Rows[idx]->Selected = true;
        }
        if (terminals_->Rows->Count == 1) terminals_->Rows[0]->Selected = true;
        terminals_->EmptyText = "No MetaTrader 5 terminal found for this Windows user. Install MetaTrader 5 from your broker and open it once.";
        terminals_->Invalidate();
    }

    JObj^ SelectedTerminal() {
        if (terminals_->SelectedRows->Count == 0) return nullptr;
        return dynamic_cast<JObj^>(terminals_->SelectedRows[0]->Tag);
    }

    bool ConfirmStart(String^ when) {
        return MessageBox::Show(FindForm(),
                                when + "\n\n"
                                       "  -  install the Pipkite EA and bridge DLL into this terminal\n"
                                       "  -  open MetaTrader with the EA on a " + symbol_->Text->Trim() + " chart\n"
                                       "  -  turn on Algo Trading and DLL imports in that terminal\n\n"
                                       "Once the account is paired, new signals are copied to it. Pause copying stops new trades.",
                                "Start MetaTrader", MessageBoxButtons::OKCancel, MessageBoxIcon::Information) == DialogResult::OK;
    }

    void OnStart(Object^, EventArgs^) {
        JObj^ t = SelectedTerminal();
        if (t == nullptr) return ShowError("Select a MetaTrader 5 terminal first.");
        if (!ConfirmStart("Pipkite will:")) return;
        JObj^ p = J::New();
        p["dataPath"] = J::Str(t, "dataPath");
        p["exePath"] = J::Str(t, "exePath");
        p["chartSymbol"] = symbol_->Text->Trim();
        terminalStatus_->Text = "Starting MetaTrader...";
        Client->Call("mt5.launch", p, gcnew ResponseHandler(this, &MetaTraderPage::OnLaunched));
    }

    void OnLaunched(bool ok, Object^ result, String^ error) {
        if (!ok) {
            terminalStatus_->Text = error;
            return ShowError(error);
        }
        JObj^ r = J::Obj(result);
        terminalStatus_->Text = J::Str(r, "message");
        // first start only creates the data folder; otherwise prove the EA really reaches Pipkite
        if (J::Str(r, "state") != "firstRun") verifyUntil_ = DateTime::Now.AddMinutes(2);
        OnDetect(nullptr, nullptr);
    }

    void OnAutoLaunchChanged(Object^, EventArgs^) {
        if (loading_) return;
        JObj^ values = J::New();
        if (autoLaunch_->Checked) {
            JObj^ t = SelectedTerminal();
            if (t == nullptr || !ConfirmStart("Each time Pipkite starts and MetaTrader is not already open, Pipkite will:")) {
                loading_ = true;
                autoLaunch_->Checked = false;
                loading_ = false;
                if (t == nullptr) ShowError("Select a MetaTrader 5 terminal first.");
                return;
            }
            values["mt5.dataPath"] = J::Str(t, "dataPath");
            values["mt5.exePath"] = J::Str(t, "exePath");
            values["mt5.chartSymbol"] = symbol_->Text->Trim();
        }
        values["mt5.autoLaunch"] = autoLaunch_->Checked ? "true" : "false";
        JObj^ p = J::New();
        p["values"] = values;
        Client->Call("settings.save", p, SimpleHandler());
    }

    void OnBrowse(Object^, EventArgs^) {
        FolderBrowserDialog^ d = gcnew FolderBrowserDialog();
        d->Description = "Choose the folder MetaTrader 5 is installed in (it contains terminal64.exe)";
        if (d->ShowDialog(FindForm()) != DialogResult::OK) return;
        JObj^ p = J::New();
        p["folder"] = d->SelectedPath;
        terminalStatus_->Text = "Checking " + d->SelectedPath + "...";
        Client->Call("mt5.checkFolder", p, gcnew ResponseHandler(this, &MetaTraderPage::OnFolderChecked));
    }

    void OnFolderChecked(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        JObj^ r = J::Obj(result);
        String^ report = "";
        for each (Object^ o in J::Arr(r, "checks")) {
            JObj^ c = J::Obj(o);
            report += (J::Bool(c, "ok") ? "OK      " : "FAILED  ") + J::Str(c, "name") + "\n          " + J::Str(c, "detail") + "\n";
        }
        if (!J::Bool(r, "ok")) return ShowError("Pipkite can't use this folder:\n\n" + report);
        chosenExe_ = J::Str(J::Obj(r, "terminal"), "exePath");
        String^ next = J::Bool(r, "bridgeConnected") ? "MetaTrader is already connected to Pipkite."
                                                      : "Click Start MetaTrader: Pipkite installs its EA, opens MetaTrader and confirms the connection.";
        ShowInfo("MetaTrader 5 found", report + "\n" + next);
        OnDetect(nullptr, nullptr);
    }

    void OnApprove(Object^, EventArgs^) {
        if (pendingKey_ == nullptr) return ShowError("No pairing request is pending. Start MetaTrader above and log in to your broker account first.");
        String^ typed = Prompt::Ask(FindForm(), "Approve pairing",
                                    "Destination account #" + J::Str(pendingKey_, "login") + " on " + J::Str(pendingKey_, "server") +
                                        ". Type the account number to bind Pipkite to this terminal and account:",
                                    "", false);
        if (typed == nullptr) return;
        JObj^ p = J::New();
        p["confirmLogin"] = typed->Trim();
        Client->Call("mt5.approvePairing", p, SimpleHandler());
    }

    void OnUnbind(Object^, EventArgs^) {
        if (MessageBox::Show(FindForm(), "Unbind the account? Copying pauses until a terminal is paired again. Existing positions stay at the broker.", "Pipkite",
                             MessageBoxButtons::YesNo, MessageBoxIcon::Warning) == DialogResult::Yes)
            Client->Call("mt5.unbind", nullptr, SimpleHandler());
    }

    // Polled (~2s): refresh the catalog of all broker symbols for the picker. Rows are not rebuilt here.
    void OnStatus(bool ok, Object^ result, String^) {
        if (!ok) return;
        JObj^ r = J::Obj(result);
        catalog_->Clear();
        array<Object^>^ catalog = J::Arr(r, "catalog");
        if (catalog != nullptr && catalog->Length > 0) {
            for each (Object ^ o in catalog) { String^ n = o->ToString(); if (n != "") catalog_->Add(n); }
        } else {
            for each (Object ^ o in J::Arr(r, "symbols")) { String^ n = J::Str(J::Obj(o), "name"); if (n != "") catalog_->Add(n); }
        }
        chanStatus_->Text = catalog_->Count == 0
            ? "No symbols yet - Start MetaTrader above and connect; the full broker list then appears when you click a Symbol cell."
            : String::Format("{0} broker symbols available - click a channel's Symbol cell to search and pick, then Save.", catalog_->Count);
    }

    // Rows are built once per activate/refresh (not on the poll), one per Telegram channel.
    void OnSources(bool ok, Object^ result, String^) {
        if (!ok) return;
        sources_->Clear();
        for each (Object ^ o in J::Arr(result)) sources_->Add(J::Obj(o));
        chanSym_->Rows->Clear();
        for each (JObj ^ b in sources_) {
            int idx = chanSym_->Rows->Add(gcnew array<Object^>{J::Str(b, "title") + (J::Bool(b, "enabled", true) ? "" : "  (paused)"), SymbolText(J::Str(b, "tradeSymbol"))});
            chanSym_->Rows[idx]->Tag = J::Str(b, "tradeSymbol");
        }
        chanSym_->EmptyText = sources_->Count == 0 ? "No channels yet - add Telegram source channels first (Telegram sources page or the wizard)." : "";
    }

    // Click a Symbol cell -> searchable picker of every broker symbol from the terminal.
    void OnChanSymClick(Object^, DataGridViewCellEventArgs^ e) {
        if (e->RowIndex < 0 || chanSym_->Columns[e->ColumnIndex]->Name != "symbol") return;
        if (catalog_ == nullptr || catalog_->Count == 0) { ShowError("No symbols yet - connect MetaTrader first (Start above), then click again."); return; }
        String^ current = Convert::ToString(chanSym_->Rows[e->RowIndex]->Tag);
        SymbolPickerDialog^ d = gcnew SymbolPickerDialog(catalog_, current, true);
        if (d->ShowDialog(FindForm()) == System::Windows::Forms::DialogResult::OK && d->Selected != nullptr) {
            chanSym_->Rows[e->RowIndex]->Tag = d->Selected;
            chanSym_->Rows[e->RowIndex]->Cells["symbol"]->Value = SymbolText(d->Selected);
        }
    }

    static String^ SymbolText(String^ sym) { return String::IsNullOrEmpty(sym) ? "Each message's own symbol" : sym; }

    void OnRefreshChannels(Object^, EventArgs^) {
        Client->Call("mt5.status", nullptr, gcnew ResponseHandler(this, &MetaTraderPage::OnStatus));
        Client->Call("sources.list", nullptr, gcnew ResponseHandler(this, &MetaTraderPage::OnSources));
    }

    // only the changed rows, one field each: the channels' other settings (and channels added meanwhile) are kept
    void OnSaveChannelSymbols(Object^, EventArgs^) {
        if (sources_->Count == 0) return ShowError("No channels to save. Add Telegram source channels first.");
        pendingSaves_ = 0;
        saveError_ = nullptr;
        for (int i = 0; i < sources_->Count && i < chanSym_->Rows->Count; i++) {
            String^ sym = Convert::ToString(chanSym_->Rows[i]->Tag);
            if (sym == J::Str(sources_[i], "tradeSymbol")) continue;
            JObj^ p = J::New();
            p["chatId"] = J::Str(sources_[i], "chatId");
            p["tradeSymbol"] = sym;
            ++pendingSaves_;
            Client->Call("source.update", p, gcnew ResponseHandler(this, &MetaTraderPage::OnChannelSaved));
        }
        if (pendingSaves_ == 0) ShowInfo("Channel symbols", "Nothing changed.");
    }
    void OnChannelSaved(bool ok, Object^, String^ error) {
        if (!ok) saveError_ = error;
        if (--pendingSaves_ > 0) return;
        if (saveError_ != nullptr) ShowError("Not saved: " + saveError_);
        else ShowInfo("Channel symbols", "Saved. Channels with a symbol trade only it; the others trade each message's own symbol.");
        OnRefreshChannels(nullptr, nullptr);
    }

    void Manage(String^ action, String^ what) {
        JObj^ snap = LastSnapshot;
        JObj^ b = J::Obj(snap, "boundAccount");
        if (b == nullptr) return ShowError("No account is bound.");
        JObj^ acc = J::Obj(snap, "account");
        const Int64 count = J::Int(acc, action == "close_positions" ? "copierOpenPositions" : "copierPendingOrders");
        String^ typed = Prompt::Ask(FindForm(), what, what + ": " + count + " placed by Pipkite on account #" + J::Str(b, "login") + " (" + J::Str(b, "server") +
                                                          "). Your own trades are not touched. Type the account number to confirm:", "", false);
        if (typed == nullptr) return;
        JObj^ p = J::New();
        p["action"] = action;
        p["confirmLogin"] = typed->Trim();
        p["expectedCount"] = count;
        Client->Call("mt5.manage", p, SimpleHandler());
    }
    void OnCancelPending(Object^, EventArgs^) { Manage("cancel_pending", "Cancel copier pending orders"); }
    void OnClosePositions(Object^, EventArgs^) { Manage("close_positions", "Close copier positions"); }
};

}  // namespace PipkiteUI
