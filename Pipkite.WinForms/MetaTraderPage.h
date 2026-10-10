#pragma once
// MetaTrader: terminal selection, one-click start with the EA attached (optionally automatic), pairing, account identity,
// broker connection, symbol specifications and symbol mapping, and explicit copier-owned actions.

#include "Pages.h"

namespace PipkiteUI {

// Searchable symbol picker: type to filter the full broker symbol list, double-click or Select to choose.
public ref class SymbolPickerDialog : public Form {
public:
    String^ Selected;
    SymbolPickerDialog(System::Collections::Generic::List<String^>^ symbols, String^ current) {
        all_ = symbols;
        Selected = nullptr;
        Text = "Choose symbol";
        FormBorderStyle = Windows::Forms::FormBorderStyle::FixedDialog;
        StartPosition = FormStartPosition::CenterParent;
        MaximizeBox = false;
        MinimizeBox = false;
        ClientSize = Drawing::Size(340, 440);
        Font = Theme::Body;
        search_ = gcnew TextBox();
        search_->SetBounds(16, 16, 308, 26);
        search_->TextChanged += gcnew EventHandler(this, &SymbolPickerDialog::OnSearch);
        list_ = gcnew ListBox();
        list_->SetBounds(16, 50, 308, 330);
        list_->DoubleClick += gcnew EventHandler(this, &SymbolPickerDialog::OnPick);
        Button^ ok = L::Btn("Select", "accent");
        ok->SetBounds(16, 392, 90, 32);
        ok->Click += gcnew EventHandler(this, &SymbolPickerDialog::OnPick);
        Button^ cancel = L::Btn("Cancel");
        cancel->SetBounds(234, 392, 90, 32);
        cancel->DialogResult = Windows::Forms::DialogResult::Cancel;
        Controls->Add(search_);
        Controls->Add(list_);
        Controls->Add(ok);
        Controls->Add(cancel);
        AcceptButton = ok;
        CancelButton = cancel;
        Fill("");
        if (current != nullptr && current != "") search_->Text = current;
        Theme::Apply(this);
        BackColor = Theme::Bg;
        HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
        search_->Select();
    }

private:
    System::Collections::Generic::List<String^>^ all_;
    TextBox^ search_;
    ListBox^ list_;
    void Fill(String^ q) {
        String^ u = q->Trim()->ToUpperInvariant();
        list_->BeginUpdate();
        list_->Items->Clear();
        for each (String ^ s in all_)
            if (u == "" || s->ToUpperInvariant()->Contains(u)) list_->Items->Add(s);
        list_->EndUpdate();
        if (list_->Items->Count > 0) list_->SelectedIndex = 0;
    }
    void OnSearch(Object^, EventArgs^) { Fill(search_->Text); }
    void OnPick(Object^, EventArgs^) {
        if (list_->SelectedItem == nullptr) return;
        Selected = list_->SelectedItem->ToString();
        DialogResult = Windows::Forms::DialogResult::OK;
    }
};

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
        symbol_ = L::Box(110);  // EA chart symbol: internal only, not shown to the user
        symbol_->Text = "EURUSD";
        term->Controls->Add(L::Row(start, detect));
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
        impl_ = L::Text("", "muted");
        conn->Controls->Add(gcnew Label());
        conn->Controls->Add(impl_);
        stack->Controls->Add(L::Group("Connection and account", conn));

        // per-channel symbols: one row per Telegram channel, pick the broker symbol it trades
        FlowLayoutPanel^ sym = gcnew FlowLayoutPanel();
        sym->FlowDirection = FlowDirection::TopDown;
        sym->AutoSize = true;
        sym->Controls->Add(L::Text("For each Telegram channel you copy, click its Symbol cell and pick the broker symbol it should trade (searchable list from your MetaTrader terminal).", nullptr));
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
        Button^ saveSym = L::Btn("Save", "accent", "Save which broker symbol each channel trades.");
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
        if (J::Bool(br, "simulated")) text += " (SIMULATED terminal)";
        bridge_->Set(text, !connected ? Theme::TextMuted : (paired && healthy) ? Theme::Success : Theme::Warning);
        JObj^ b = J::Obj(s, "boundAccount");
        bound_->Text = b == nullptr ? "None" : "#" + J::Str(b, "login") + " @ " + J::Str(b, "server") + "  (terminal " + J::Str(b, "terminalId") + ")";
        pendingKey_ = J::Obj(br, "pairingPending");
        pending_->Text = pendingKey_ == nullptr ? "None" : "#" + J::Str(pendingKey_, "login") + " @ " + J::Str(pendingKey_, "server") + " is waiting for approval";
        impl_->Text = "Bridge implementation: " + J::Str(br, "implVersion", "-") + "   Heartbeat: " + (J::Int(br, "heartbeatAgoMs") >= 0 ? J::Str(br, "heartbeatAgoMs") + " ms ago" : "-") +
                      "   " + J::Str(br, "status");
        String^ ts = J::Str(J::Obj(s, "terminal"), "status");
        if (terminalStatus_->Text != ts) terminalStatus_->Text = ts;
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
    bool loading_;
    Badge^ bridge_;
    Label ^ bound_, ^pending_, ^impl_, ^chanStatus_;
    BufferedGrid^ chanSym_;
    System::Collections::Generic::List<String^>^ catalog_;
    System::Collections::Generic::List<JObj^>^ sources_;
    JObj^ pendingKey_;
    DateTime lastStatus_;

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
            String^ state = !J::Bool(t, "exeFound") ? "terminal64.exe not found" : J::Bool(t, "running") ? "Running" : "Not running";
            if (J::Bool(t, "hasEA") && J::Bool(t, "hasDll")) state += "  ·  EA installed";
            int idx = terminals_->Rows->Add(gcnew array<Object^>{J::Str(t, "origin") == "" ? "(unknown installation)" : J::Str(t, "origin"), state});
            terminals_->Rows[idx]->Tag = t;
            if (J::Str(t, "dataPath") == savedDataPath_) terminals_->Rows[idx]->Selected = true;
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
        terminalStatus_->Text = J::Str(J::Obj(result), "message");
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
        d->Description = "Select the MT5 data folder (File > Open Data Folder in MetaTrader)";
        if (d->ShowDialog(FindForm()) != DialogResult::OK) return;
        JObj^ p = J::New();
        p["dataPath"] = d->SelectedPath;
        Client->Call("mt5.installEA", p, gcnew ResponseHandler(this, &MetaTraderPage::OnInstalled));
    }

    void OnInstalled(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        JObj^ r = J::Obj(result);
        String^ files = "";
        for each (Object^ f in J::Arr(r, "copied")) files += Convert::ToString(f) + "  ";
        ShowInfo("EA installed", "Copied: " + files + "\n\n" + J::Str(r, "next"));
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
        for each (JObj ^ b in sources_)
            chanSym_->Rows->Add(gcnew array<Object^>{J::Str(b, "title"), J::Str(b, "tradeSymbol")});
        chanSym_->EmptyText = sources_->Count == 0 ? "No channels yet - add Telegram source channels first (Telegram sources page or the wizard)." : "";
    }

    // Click a Symbol cell -> searchable picker of every broker symbol from the terminal.
    void OnChanSymClick(Object^, DataGridViewCellEventArgs^ e) {
        if (e->RowIndex < 0 || chanSym_->Columns[e->ColumnIndex]->Name != "symbol") return;
        if (catalog_ == nullptr || catalog_->Count == 0) { ShowError("No symbols yet - connect MetaTrader first (Start above), then click again."); return; }
        String^ current = Convert::ToString(chanSym_->Rows[e->RowIndex]->Cells["symbol"]->Value);
        SymbolPickerDialog^ d = gcnew SymbolPickerDialog(catalog_, current);
        if (d->ShowDialog(FindForm()) == System::Windows::Forms::DialogResult::OK && d->Selected != nullptr)
            chanSym_->Rows[e->RowIndex]->Cells["symbol"]->Value = d->Selected;
    }

    void OnRefreshChannels(Object^, EventArgs^) {
        Client->Call("mt5.status", nullptr, gcnew ResponseHandler(this, &MetaTraderPage::OnStatus));
        Client->Call("sources.list", nullptr, gcnew ResponseHandler(this, &MetaTraderPage::OnSources));
    }

    void OnSaveChannelSymbols(Object^, EventArgs^) {
        if (sources_->Count == 0) return ShowError("No channels to save. Add Telegram source channels first.");
        chanSym_->EndEdit();
        for (int i = 0; i < sources_->Count && i < chanSym_->Rows->Count; i++) {
            String^ sym = Convert::ToString(chanSym_->Rows[i]->Cells["symbol"]->Value);
            sources_[i]["tradeSymbol"] = sym == nullptr ? "" : sym;  // the broker symbol this channel trades
        }
        array<Object^>^ list = gcnew array<Object^>(sources_->Count);
        for (int i = 0; i < sources_->Count; i++) list[i] = sources_[i];
        JObj^ p = J::New();
        p["sources"] = list;
        Client->Call("sources.save", p, gcnew ResponseHandler(this, &MetaTraderPage::OnChannelsSaved));
    }
    void OnChannelsSaved(bool ok, Object^, String^ error) {
        if (!ok) return ShowError(error);
        ShowInfo("Channel symbols", "Saved. Each channel now trades the symbol you chose.");
    }

    void Manage(String^ action, String^ what) {
        JObj^ snap = LastSnapshot;
        JObj^ b = J::Obj(snap, "boundAccount");
        if (b == nullptr) return ShowError("No account is bound.");
        JObj^ acc = J::Obj(snap, "account");
        String^ count = action == "close_positions" ? J::Str(acc, "copierOpenPositions", "?") : J::Str(acc, "copierPendingOrders", "?");
        String^ typed = Prompt::Ask(FindForm(), what, what + ": " + count + " copier-owned item(s) on account #" + J::Str(b, "login") + " (" + J::Str(b, "server") +
                                                          "). Type the account number to confirm:", "", false);
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
