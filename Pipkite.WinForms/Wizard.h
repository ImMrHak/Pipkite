#pragma once
// first-run wizard: license key, telegram, chats, MT5, parsers, sample test

#include "Controls.h"
#include "EngineClient.h"

namespace PipkiteUI {

public delegate void NavigateRequest(int pageIndex);

public ref class FirstRunWizard : public Form {
public:
    static NavigateRequest^ Navigate;

    FirstRunWizard(EngineClient^ client) {
        client_ = client;
        Text = "Welcome to Pipkite";
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        StartPosition = FormStartPosition::CenterParent;
        FormBorderStyle = Windows::Forms::FormBorderStyle::FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        Size = Drawing::Size(760, 560);
        Font = Theme::Body;
        steps_ = gcnew array<String^, 2>{
            {"Welcome to Pipkite",
             "Pipkite is free to use. A license key unlocks the backend features - AI review of every signal, custom parser requests and automatic "
             "updates.\r\n\r\nEnter your license key below. Don't have one yet? Generate one for free from our website - nothing is needed, it only takes a "
             "moment. Please enter it so you can get the most out of Pipkite.\r\n\r\nYou can also do this later anytime in Settings.",
             ""},
            {"1. How it works",
             "Pipkite reads new messages from the Telegram chats you choose, turns them into trade instructions, checks them against your rules and "
             "sends the orders to your MetaTrader 5 terminal.\r\n\r\nCopying starts as soon as MetaTrader is paired and a channel has its parser, so "
             "pair a demo account first. Pause copying (top bar or tray) stops new trades at any time; it never closes open positions.\r\n\r\n"
             "No copier can guarantee fills or profits.",
             ""},
            {"2. Sign in to Telegram",
             "Get your api_id / api_hash from my.telegram.org (link below), then sign in right here: save credentials, "
             "send the code to your phone, and enter it. Credentials are DPAPI-encrypted and never logged.",
             "1"},
            {"3. Choose source chats",
             "Tick the channels/groups you want to copy - each is added as a source automatically; untick to remove. "
             "Chats are tracked by ID, so renaming a chat keeps its binding.",
             "1"},
            {"4. Connect MetaTrader 5",
             "Tick auto-start if you want MetaTrader to open with Pipkite, then Detect & start MetaTrader. "
             "Log in to your account and Pipkite pairs it automatically (demo or real).",
             "2"},
            {"5. Each channel: symbol & parser",
             "For every source, set the symbol it should capture and request a custom parser for it. The team builds it and it installs automatically "
             "(hot reload, no restart). Ships with no parsers, so request one per channel.",
             "1"},
            {"6. Test a sample",
             "Pick a channel, paste a real message it posted, and Test to see exactly what Pipkite reads from it - nothing is sent. Better to check "
             "here before going live. You can also skip.",
             "1"},
            {"7. You're set",
             "When everything above is green, Pipkite copies new signals from your channels. Watch the first ones on the Dashboard and in History, "
             "and use Pause copying in the top bar whenever you want to stop new trades.",
             ""}};
        // Layout note: everything lives in a single top-down FlowLayoutPanel so controls stack and never
        // overlap (invisible ones take no space). The buttons sit in a docked bottom bar. No absolute Points.
        title_ = L::Text("", "title");
        body_ = gcnew Label();
        body_->AutoSize = true;
        body_->MaximumSize = Drawing::Size(690, 0);  // wrap + grow vertically
        body_->Font = Theme::Body;

        // License controls (welcome step): key entry + validate on one row, a free-license link, status.
        licenseKey_ = L::Box(380);
        validate_ = L::Btn("Validate & save", "accent");
        validate_->Click += gcnew EventHandler(this, &FirstRunWizard::OnValidateLicense);
        licRow_ = L::Row(licenseKey_, validate_);
        getKeyLink_ = gcnew LinkLabel();
        getKeyLink_->Text = "Generate a free license key (free - nothing needed)";
        getKeyLink_->AutoSize = true;
        getKeyLink_->LinkColor = Theme::Accent;
        getKeyLink_->Click += gcnew EventHandler(this, &FirstRunWizard::OnGetKey);
        licenseStatus_ = L::Wrap("", "muted", 680);

        // Telegram API credentials link (step 2 only).
        tgLink_ = gcnew LinkLabel();
        tgLink_->Text = "Open my.telegram.org to get your api_id / api_hash";
        tgLink_->AutoSize = true;
        tgLink_->LinkColor = Theme::Accent;
        tgLink_->Tag = "https://my.telegram.org/apps";
        tgLink_->Click += gcnew EventHandler(this, &FirstRunWizard::OnLink);

        // Inline Telegram sign-in (step 2): fill the inputs and log in without leaving the wizard.
        TableLayoutPanel^ tg = L::Form2();
        apiId_ = L::Box(160);
        L::AddField(tg, "API ID", apiId_);
        apiHash_ = L::Box(280);
        apiHash_->UseSystemPasswordChar = true;
        L::AddField(tg, "API hash", apiHash_);
        Button^ saveCred = L::Btn("Save credentials", "accent");
        saveCred->Click += gcnew EventHandler(this, &FirstRunWizard::OnSaveCred);
        tg->Controls->Add(gcnew Label());
        tg->Controls->Add(saveCred);
        phone_ = L::Box(180);
        Button^ sendPhone = L::Btn("Send code");
        sendPhone->Click += gcnew EventHandler(this, &FirstRunWizard::OnPhone);
        L::AddField(tg, "Phone (+...)", L::Row(phone_, sendPhone));
        code_ = L::Box(120);
        Button^ submitCode = L::Btn("Submit code");
        submitCode->Click += gcnew EventHandler(this, &FirstRunWizard::OnCode);
        L::AddField(tg, "Login code", L::Row(code_, submitCode));
        password_ = L::Box(180);
        password_->UseSystemPasswordChar = true;
        Button^ submitPwd = L::Btn("Submit password");
        submitPwd->Click += gcnew EventHandler(this, &FirstRunWizard::OnPassword);
        L::AddField(tg, "Two-step password", L::Row(password_, submitPwd));
        tgPanel_ = tg;

        // Step 3, choose source chats right here (checkbox list + actions).
        chatsList_ = gcnew CheckedListBox();
        chatsList_->Width = 660;
        chatsList_->Height = 190;
        chatsList_->CheckOnClick = true;
        chatsList_->IntegralHeight = false;
        chatsList_->ItemCheck += gcnew ItemCheckEventHandler(this, &FirstRunWizard::OnChatItemCheck);  // auto add/remove as a source
        Button^ refreshChats = L::Btn("Refresh chats");
        refreshChats->Click += gcnew EventHandler(this, &FirstRunWizard::OnRefreshChats);
        chatsBtns_ = L::Row(refreshChats);

        // Step 4, MetaTrader: auto-launch + connect/pair, with step-by-step guidance.
        mtAuto_ = L::Check("Start MetaTrader automatically when Pipkite starts");
        Button^ mtStart = L::Btn("Detect && start MetaTrader", "accent");
        mtStart->Click += gcnew EventHandler(this, &FirstRunWizard::OnMtStart);
        mtBtns_ = L::Row(mtStart);
        mtSteps_ = L::Wrap("To connect: 1) Detect & start MetaTrader (installs the EA and opens it on a chart), "
                           "2) log in to your broker account inside MetaTrader and enable Algo Trading. "
                           "Pipkite then pairs the account automatically (demo or real, whichever you logged in to).", "muted", 680);

        // Step 5, per channel: the symbol it should capture (loaded from MetaTrader) + an example message
        // + its parser, and request a custom one.
        TableLayoutPanel^ ps = L::Form2();
        srcCombo_ = L::Combo(380, gcnew array<String^>{});
        srcCombo_->SelectedIndexChanged += gcnew EventHandler(this, &FirstRunWizard::OnSrcChanged);
        L::AddField(ps, "Channel", srcCombo_);
        symbolCombo_ = gcnew ComboBox();
        symbolCombo_->Width = 240;
        symbolCombo_->DropDownStyle = ComboBoxStyle::DropDownList;  // select only; symbols come from MetaTrader
        symbolCombo_->MaxDropDownItems = 18;
        L::AddField(ps, "Symbol to capture", symbolCombo_);
        exampleBox_ = gcnew TextBox();
        exampleBox_->Multiline = true;
        exampleBox_->Width = 440;
        exampleBox_->Height = 70;
        exampleBox_->ScrollBars = ScrollBars::Vertical;
        L::AddField(ps, "Example message", exampleBox_);
        parserLabel_ = L::Wrap("", "muted", 440);
        L::AddField(ps, "Current parser", parserLabel_);
        Button^ reqParser = L::Btn("Request a custom parser for this channel", "accent");
        reqParser->Click += gcnew EventHandler(this, &FirstRunWizard::OnRequestParser);
        ps->Controls->Add(gcnew Label());
        ps->Controls->Add(reqParser);
        parserStep_ = ps;

        // Step 6, test a sample message against a channel's active parser (read-only, nothing is sent).
        FlowLayoutPanel^ tst = gcnew FlowLayoutPanel();
        tst->FlowDirection = FlowDirection::TopDown;
        tst->AutoSize = true;
        tst->WrapContents = false;
        testCombo_ = L::Combo(380, gcnew array<String^>{});
        tst->Controls->Add(testCombo_);
        testBox_ = gcnew TextBox();
        testBox_->Multiline = true;
        testBox_->Width = 660;
        testBox_->Height = 90;
        testBox_->ScrollBars = ScrollBars::Vertical;
        tst->Controls->Add(testBox_);
        Button^ testBtn = L::Btn("Test this message", "accent");
        testBtn->Click += gcnew EventHandler(this, &FirstRunWizard::OnTest);
        tst->Controls->Add(testBtn);
        testResult_ = L::Wrap("", nullptr, 680);
        testResult_->Font = Theme::Mono;
        tst->Controls->Add(testResult_);
        testStep_ = tst;

        // Live status for the current step (Telegram / sources / MT5 / symbols), refreshed from each snapshot.
        stepStatus_ = L::Wrap("", nullptr, 680);

        content_ = gcnew FlowLayoutPanel();
        content_->Dock = DockStyle::Fill;
        content_->FlowDirection = FlowDirection::TopDown;
        content_->WrapContents = false;
        content_->AutoScroll = true;
        content_->Padding = System::Windows::Forms::Padding(24, 18, 24, 8);
        for each (Control ^ c in gcnew array<Control^>{title_, body_, licRow_, getKeyLink_, licenseStatus_, tgLink_, tgPanel_, chatsList_, chatsBtns_, mtAuto_, mtBtns_, mtSteps_, parserStep_, testStep_, stepStatus_}) {
            c->Margin = System::Windows::Forms::Padding(0, 6, 0, 6);
            content_->Controls->Add(c);
        }

        // Bottom button bar (docked, never overlaps content).
        progress_ = L::Text("", "muted");
        progress_->Margin = System::Windows::Forms::Padding(16, 10, 0, 0);
        back_ = L::Btn("Back");
        back_->Click += gcnew EventHandler(this, &FirstRunWizard::OnBack);
        next_ = L::Btn("Next", "accent");
        next_->Click += gcnew EventHandler(this, &FirstRunWizard::OnNext);
        Button^ skip = L::Btn("Close");
        skip->Click += gcnew EventHandler(this, &FirstRunWizard::OnClose);
        // One right-to-left flow bar holds all buttons so none can be clipped: Close (rightmost), Next, Back,
        // then the step counter on the left.
        bottomBar_ = gcnew FlowLayoutPanel();
        bottomBar_->Dock = DockStyle::Bottom;
        bottomBar_->FlowDirection = FlowDirection::RightToLeft;
        bottomBar_->WrapContents = false;
        bottomBar_->Height = 60;
        bottomBar_->Padding = System::Windows::Forms::Padding(12, 12, 12, 12);
        bottomBar_->Controls->Add(skip);
        bottomBar_->Controls->Add(next_);
        bottomBar_->Controls->Add(back_);
        bottomBar_->Controls->Add(progress_);

        Controls->Add(content_);    // Fill (added first so it takes the space above the bar)
        Controls->Add(bottomBar_);  // Bottom
        AcceptButton = next_;
        Theme::Apply(this);
        BackColor = Theme::Bg;
        HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
        ShowStep(0);
        // Load the current key and the portal URL so the link and entry box reflect saved state.
        if (client_ != nullptr && client_->Connected) client_->Call("settings.get", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnSettings));
        // Keep the per-step connection status live while the wizard is open.
        if (client_ != nullptr) {
            snapHandler_ = gcnew SnapshotHandler(this, &FirstRunWizard::OnSnapshot);
            client_->SnapshotReceived += snapHandler_;
        }
        // On the per-channel step, keep asking the backend until requested parsers are built and installed.
        pollTimer_ = gcnew System::Windows::Forms::Timer();
        pollTimer_->Interval = 20000;  // 20s
        pollTimer_->Tick += gcnew EventHandler(this, &FirstRunWizard::OnPollTick);
    }

private:
    static String^ kDefaultPortal = "https://pipkite.example.com/get-license";  // placeholder; set license.portalUrl in Settings
    EngineClient^ client_;
    array<String^, 2>^ steps_;
    int step_;
    Label ^ title_, ^body_, ^progress_, ^licenseStatus_, ^stepStatus_;
    Button ^ back_, ^next_, ^validate_;
    TextBox ^ licenseKey_, ^apiId_, ^apiHash_, ^phone_, ^code_, ^password_;
    LinkLabel ^ getKeyLink_, ^tgLink_;
    TableLayoutPanel^ tgPanel_;
    FlowLayoutPanel ^ content_, ^bottomBar_;
    Control ^ licRow_, ^chatsBtns_, ^mtBtns_;
    CheckedListBox^ chatsList_;
    CheckBox^ mtAuto_;
    Label ^ mtSteps_, ^parserLabel_;
    TableLayoutPanel^ parserStep_;
    ComboBox ^ srcCombo_, ^testCombo_, ^symbolCombo_;
    TextBox ^ testBox_, ^exampleBox_;
    Control^ testStep_;
    Label^ testResult_;
    String ^ portalUrl_, ^pendingSaveNote_;
    SnapshotHandler^ snapHandler_;
    JObj^ lastSnap_;
    array<Object^>^ chats_;
    System::Collections::Generic::List<JObj^> ^ chatRows_, ^sources_;
    String ^ mtDataPath_, ^mtExePath_, ^mtChartSymbol_;
    bool loadingChats_, pairingSent_;
    System::Windows::Forms::Timer^ pollTimer_;

    void ShowStep(int i) {
        step_ = i;
        title_->Text = steps_[i, 0];
        body_->Text = steps_[i, 1];
        progress_->Text = "Step " + (i + 1) + " of " + steps_->GetLength(0);
        back_->Enabled = i > 0;
        next_->Text = i == steps_->GetLength(0) - 1 ? "Finish" : "Next";
        bool lic = i == 0;  // license entry lives on the welcome step
        licRow_->Visible = lic;
        getKeyLink_->Visible = lic;
        licenseStatus_->Visible = lic;
        tgLink_->Visible = i == 2;  // Telegram sign-in step
        tgPanel_->Visible = i == 2;
        chatsList_->Visible = i == 3;  // choose source chats
        chatsBtns_->Visible = i == 3;
        mtAuto_->Visible = i == 4;  // MetaTrader connect
        mtBtns_->Visible = i == 4;
        mtSteps_->Visible = i == 4;
        parserStep_->Visible = i == 5;  // per-channel symbol + parser
        testStep_->Visible = i == 6;    // test a sample
        stepStatus_->Visible = !lic;
        if (pollTimer_ != nullptr) { if (i == 5) pollTimer_->Start(); else pollTimer_->Stop(); }
        UpdateStepStatus();
        UpdateNav();
        if (i == 3 && client_ != nullptr && client_->Connected) {
            client_->Call("telegram.chats", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnChats));
            client_->Call("sources.list", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnSources));
        }
        if (i == 4 && client_ != nullptr && client_->Connected) {
            client_->Call("mt5.detectTerminals", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnTerminals));
            client_->Call("settings.get", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnMtSettings));
        }
        if (i == 5 && client_ != nullptr && client_->Connected) {
            client_->Call("sources.list", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnSourcesForParser));
            client_->Call("mt5.status", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnSymbols));  // load broker symbols
        }
        if (i == 6 && client_ != nullptr && client_->Connected)
            client_->Call("sources.list", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnSourcesForTest));
    }

    void OnSnapshot(JObj^ s) {
        lastSnap_ = s;
        AutoPair();
        UpdateStepStatus();
        UpdateNav();
    }

    // Pair the single MT5 account automatically: when the terminal requests pairing and nothing is bound yet,
    // approve it with the pending login (no manual step). Supports one account for now.
    void AutoPair() {
        if (lastSnap_ == nullptr || client_ == nullptr) return;
        JObj^ pending = J::Obj(J::Obj(lastSnap_, "bridge"), "pairingPending");
        if (pending != nullptr && J::Obj(lastSnap_, "account") == nullptr) {
            if (!pairingSent_) {
                pairingSent_ = true;
                JObj^ p = J::New();
                p["confirmLogin"] = J::Str(pending, "login");
                client_->Call("mt5.approvePairing", p);
            }
        } else {
            pairingSent_ = false;  // reset once bound or no request
        }
    }

    // Live "is the connection good?" line for the current step, so setup can be confirmed without leaving the wizard.
    void UpdateStepStatus() {
        if (stepStatus_ == nullptr || !stepStatus_->Visible) return;
        if (lastSnap_ == nullptr) {
            stepStatus_->Text = "Live status will appear here once the engine is connected.";
            stepStatus_->ForeColor = Theme::TextMuted;
            return;
        }
        String^ text; Color color = Theme::TextMuted;
        if (step_ == 2) {  // Telegram
            String^ st = J::Str(J::Obj(lastSnap_, "telegram"), "state");
            String^ user = J::Str(J::Obj(lastSnap_, "telegram"), "user");
            if (st == "Ready") { text = "Telegram connected" + (user != "" ? " as " + user : ""); color = Theme::Success; }
            else if (st == "LibraryMissing") { text = "Telegram library missing - reinstall the app"; color = Theme::Danger; }
            else if (st == "NotConfigured" || st == "") { text = "Telegram not set up yet - enter your API ID/hash above and sign in"; color = Theme::Warning; }
            else { text = "Telegram: " + st + "..."; color = Theme::Warning; }
        } else if (step_ == 3) {  // Sources
            int n = J::Int(J::Obj(lastSnap_, "telegram"), "sources");
            text = n > 0 ? n + " source chat(s) selected" : "No source chats yet - tick chats above and press Add checked as sources";
            color = n > 0 ? Theme::Success : Theme::Warning;
        } else if (step_ == 4) {  // MT5 connection
            JObj^ br = J::Obj(lastSnap_, "bridge");
            JObj^ acc = J::Obj(lastSnap_, "account");
            if (acc != nullptr && J::Bool(br, "healthy")) {
                String^ sim = J::Bool(acc, "simulated") ? " (simulated)" : "";
                text = "MetaTrader connected - account #" + J::Str(J::Obj(acc, "account"), "login") + sim;
                color = J::Bool(acc, "simulated") ? Theme::Warning : Theme::Success;
            } else {
                text = "MetaTrader not connected (" + J::Str(br, "status") + ") - use Detect & start MetaTrader below";
                color = Theme::Warning;
            }
        } else if (step_ == 5) {  // Account + symbols imported from MT5
            JObj^ br = J::Obj(lastSnap_, "bridge");
            JObj^ acc = J::Obj(lastSnap_, "account");
            int syms = J::Int(br, "symbols");
            if (acc != nullptr && J::Bool(br, "healthy")) {
                text = syms + " symbol(s) imported from MetaTrader. Map providers to broker symbols (e.g. GOLD -> XAUUSD) on this page.";
                color = syms > 0 ? Theme::Success : Theme::Warning;
            } else {
                text = "Connect MetaTrader first (previous step); symbols then import automatically.";
                color = Theme::Warning;
            }
        } else {
            text = "";
        }
        stepStatus_->Text = text;
        stepStatus_->ForeColor = color;
    }

    void OnLink(Object^ sender, EventArgs^) {
        LinkLabel^ l = safe_cast<LinkLabel^>(sender);
        String^ url = l->Tag->ToString();
        try { Diagnostics::Process::Start(url); }
        catch (Exception^) { MessageBox::Show(this, "Open this link:\r\n" + url, "Pipkite"); }
    }

    // Inline Telegram sign-in: the same engine methods the Sources page uses; the status line reflects progress.
    void OnSaveCred(Object^, EventArgs^) {
        if (apiId_->Text->Trim() == "" || apiHash_->Text->Trim() == "") { stepStatus_->Text = "Enter both API ID and API hash from my.telegram.org."; return; }
        JObj^ p = J::New();
        p["apiId"] = apiId_->Text->Trim();
        p["apiHash"] = apiHash_->Text->Trim();
        client_->Call("telegram.setCredentials", p);
        apiHash_->Clear();
        stepStatus_->Text = "Credentials saved. Enter your phone number and press Send code.";
    }
    void OnPhone(Object^, EventArgs^) {
        if (phone_->Text->Trim() == "") { stepStatus_->Text = "Enter your phone number in international format (+...)."; return; }
        JObj^ p = J::New();
        p["phone"] = phone_->Text->Trim();
        client_->Call("telegram.phone", p);
        stepStatus_->Text = "Code requested. Check Telegram, then enter the login code.";
    }
    void OnCode(Object^, EventArgs^) {
        if (code_->Text->Trim() == "") { stepStatus_->Text = "Enter the login code Telegram sent you."; return; }
        JObj^ p = J::New();
        p["code"] = code_->Text->Trim();
        client_->Call("telegram.code", p);
        code_->Clear();
    }
    void OnPassword(Object^, EventArgs^) {
        JObj^ p = J::New();
        p["password"] = password_->Text;
        client_->Call("telegram.password", p);
        password_->Clear();
    }

    // Step 3: source chats
    void OnRefreshChats(Object^, EventArgs^) {
        if (client_ != nullptr) client_->Call("telegram.chats", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnChats));
    }
    void OnChats(bool ok, Object^ result, String^ error) {
        if (!ok) return;
        chats_ = J::Arr(result);
        chatRows_ = gcnew System::Collections::Generic::List<JObj^>();
        loadingChats_ = true;  // suppress ItemCheck while we fill
        chatsList_->Items->Clear();
        for each (Object ^ o in chats_) {
            JObj^ c = J::Obj(o);
            String^ type = J::Str(c, "type");
            String^ label = J::Str(c, "title") + "   (" + (type == "channel" ? "channel" : "group") + ")";
            chatRows_->Add(c);
            chatsList_->Items->Add(label, HasSource(J::Str(c, "chatId")));  // tick ones already saved as sources
        }
        loadingChats_ = false;
        if (chatsList_->Items->Count == 0) stepStatus_->Text = "No chats yet. Make sure you're signed in to Telegram (previous step), then Refresh.";
    }
    // Checking a chat adds it as a source immediately; unchecking removes it. No separate "Add" button.
    void OnChatItemCheck(Object^, ItemCheckEventArgs^ e) {
        if (loadingChats_ || chatRows_ == nullptr || e->Index < 0 || e->Index >= chatRows_->Count) return;
        if (sources_ == nullptr) sources_ = gcnew System::Collections::Generic::List<JObj^>();
        JObj^ c = chatRows_[e->Index];
        String^ chatId = J::Str(c, "chatId");
        bool willCheck = e->NewValue == System::Windows::Forms::CheckState::Checked;
        if (willCheck) {
            if (!HasSource(chatId)) sources_->Add(NewBinding(chatId, J::Str(c, "title"), J::Str(c, "type")));
        } else {
            for (int k = sources_->Count - 1; k >= 0; --k)
                if (J::Str(sources_[k], "chatId") == chatId) sources_->RemoveAt(k);
        }
        SaveSources(willCheck ? "Added " + J::Str(c, "title") : "Removed " + J::Str(c, "title"));
    }
    void OnSources(bool ok, Object^ result, String^ error) {
        if (!ok) return;
        sources_ = gcnew System::Collections::Generic::List<JObj^>();
        for each (Object ^ o in J::Arr(result)) sources_->Add(J::Obj(o));
        UpdateStepStatus();
        UpdateNav();
    }
    bool HasSource(String^ chatId) {
        if (sources_ == nullptr) return false;
        for each (JObj ^ b in sources_) if (J::Str(b, "chatId") == chatId) return true;
        return false;
    }
    JObj^ NewBinding(String^ chatId, String^ title, String^ type) {
        JObj^ b = J::New();
        b["chatId"] = chatId;
        b["title"] = title;
        b["chatType"] = type;
        b["enabled"] = true;
        b["template"] = "";  // no parser yet; request one per channel on the next step
        b["entryMode"] = "wait_for_range";
        b["aiMode"] = "off";
        b["immediateTolerancePrice"] = 0.5;
        return b;
    }
    void SaveSources(String^ note) {
        array<Object^>^ list = gcnew array<Object^>(sources_->Count);
        for (int i = 0; i < sources_->Count; ++i) list[i] = sources_[i];
        JObj^ p = J::New();
        p["sources"] = list;
        pendingSaveNote_ = note;
        client_->Call("sources.save", p, gcnew ResponseHandler(this, &FirstRunWizard::OnSourcesSaved));
    }
    void OnSourcesSaved(bool ok, Object^ result, String^ error) {
        if (!ok) {
            stepStatus_->ForeColor = Theme::Danger;
            stepStatus_->Text = "Could not save sources: " + error;
            return;
        }
        stepStatus_->ForeColor = Theme::Success;
        stepStatus_->Text = pendingSaveNote_ + " (" + sources_->Count + " total)";
        UpdateNav();
    }

    // Step 4: MetaTrader
    void OnMtSettings(bool ok, Object^ result, String^) {
        if (!ok) return;
        JObj^ s = J::Obj(result);
        mtAuto_->Checked = J::Str(s, "mt5.autoLaunch", "false") == "true";
        mtChartSymbol_ = J::Str(s, "mt5.chartSymbol");  // the EA's chart symbol, from settings (not hardcoded)
    }
    void OnTerminals(bool ok, Object^ result, String^) {
        if (!ok) return;
        mtDataPath_ = ""; mtExePath_ = "";
        for each (Object ^ o in J::Arr(result)) {
            JObj^ t = J::Obj(o);
            if (J::Bool(t, "exeFound")) { mtDataPath_ = J::Str(t, "dataPath"); mtExePath_ = J::Str(t, "exePath"); break; }
        }
        if (mtExePath_ == "") stepStatus_->Text = "No MetaTrader 5 terminal found for this Windows user. Install it from your broker and open it once.";
    }
    void OnAutoToggle(Object^, EventArgs^) {}
    void OnMtStart(Object^, EventArgs^) {
        if (String::IsNullOrEmpty(mtExePath_)) { stepStatus_->Text = "No MetaTrader terminal detected. Install MetaTrader 5 and open it once, then Detect again."; return; }
        if (MessageBox::Show(this, "Pipkite will install its EA into this terminal and open MetaTrader with the EA on a chart. "
                                   "Once the account is paired, new signals are copied to it (Pause copying stops them). Continue?",
                             "Start MetaTrader", MessageBoxButtons::OKCancel, MessageBoxIcon::Information) != System::Windows::Forms::DialogResult::OK)
            return;
        String^ chart = mtChartSymbol_ != nullptr && mtChartSymbol_ != "" ? mtChartSymbol_ : "EURUSD";  // EA chart only; capture symbol is per-channel from the terminal
        JObj^ p = J::New();
        p["dataPath"] = mtDataPath_;
        p["exePath"] = mtExePath_;
        p["chartSymbol"] = chart;
        client_->Call("mt5.launch", p);
        if (mtAuto_->Checked) {
            JObj^ v = J::New();
            v["mt5.autoLaunch"] = "true";
            v["mt5.dataPath"] = mtDataPath_;
            v["mt5.exePath"] = mtExePath_;
            v["mt5.chartSymbol"] = chart;
            JObj^ sp = J::New();
            sp["values"] = v;
            client_->Call("settings.save", sp);
        }
        stepStatus_->ForeColor = Theme::TextMuted;
        stepStatus_->Text = "Starting MetaTrader... log in to your broker and enable Algo Trading. Pipkite will pair the account automatically.";
    }
    // Step 5: per-channel symbol + parser + request
    void OnSourcesForParser(bool ok, Object^ result, String^) {
        if (!ok) return;
        sources_ = gcnew System::Collections::Generic::List<JObj^>();
        for each (Object ^ o in J::Arr(result)) sources_->Add(J::Obj(o));
        srcCombo_->Items->Clear();
        for each (JObj ^ b in sources_) srcCombo_->Items->Add(J::Str(b, "title"));
        if (srcCombo_->Items->Count > 0) srcCombo_->SelectedIndex = 0;
        else parserLabel_->Text = "No source chats yet - add some on the previous step.";
        UpdateNav();
    }
    // Poll on the per-channel step: ask the backend to pull/install new parsers, then refresh status (without
    // rebuilding the dropdown, so the user's selection and typed symbol/example are preserved).
    void OnPollTick(Object^, EventArgs^) {
        if (step_ != 5 || client_ == nullptr || !client_->Connected) return;
        client_->Call("updates.check", nullptr);
        client_->Call("sources.list", nullptr, gcnew ResponseHandler(this, &FirstRunWizard::OnParserPoll));
    }
    void OnParserPoll(bool ok, Object^ result, String^) {
        if (!ok) return;
        sources_ = gcnew System::Collections::Generic::List<JObj^>();
        for each (Object ^ o in J::Arr(result)) sources_->Add(J::Obj(o));
        OnSrcChanged(nullptr, nullptr);  // refresh the current channel's parser label
        UpdateNav();
    }
    bool AllParsersInstalled() {
        if (sources_ == nullptr || sources_->Count == 0) return false;
        for each (JObj ^ b in sources_)
            if (J::Str(b, "activeParserVersion") == "" && J::Str(b, "template") == "") return false;
        return true;
    }
    void OnSymbols(bool ok, Object^ result, String^) {
        if (!ok) return;
        JObj^ r = J::Obj(result);
        String^ keep = symbolCombo_->Text;
        symbolCombo_->Items->Clear();
        array<Object^>^ catalog = J::Arr(r, "catalog");  // every broker symbol (names only)
        if (catalog != nullptr && catalog->Length > 0) {
            for each (Object ^ o in catalog) { String^ n = o->ToString(); if (n != "") symbolCombo_->Items->Add(n); }
        } else {
            for each (Object ^ o in J::Arr(r, "symbols")) { String^ n = J::Str(J::Obj(o), "name"); if (n != "") symbolCombo_->Items->Add(n); }
        }
        symbolCombo_->Text = keep;  // re-select previous choice if still present
        if (symbolCombo_->Items->Count == 0)
            stepStatus_->Text = "No symbols from MetaTrader yet - connect it on the previous step; symbols import automatically.";
    }
    void OnSrcChanged(Object^, EventArgs^) {
        int idx = srcCombo_->SelectedIndex;
        if (sources_ == nullptr || idx < 0 || idx >= sources_->Count) return;
        JObj^ b = sources_[idx];
        String^ active = J::Str(b, "activeParserVersion");
        String^ tmpl = J::Str(b, "template");
        if (active != "") { parserLabel_->ForeColor = Theme::Success; parserLabel_->Text = "Parser installed and active (" + active + ")."; }
        else if (tmpl != "") { parserLabel_->ForeColor = Theme::Warning; parserLabel_->Text = "Parser '" + tmpl + "' assigned but not installed yet - waiting for the backend..."; }
        else { parserLabel_->ForeColor = Theme::TextMuted; parserLabel_->Text = "No parser yet - request a custom one below. The team builds it (up to ~10-15 min) and it installs automatically, no restart."; }
        if (symbolCombo_->Text == "") symbolCombo_->Text = J::Str(b, "captureSymbol");
    }
    void OnRequestParser(Object^, EventArgs^) {
        int idx = srcCombo_->SelectedIndex;
        if (sources_ == nullptr || idx < 0 || idx >= sources_->Count) { stepStatus_->ForeColor = Theme::Warning; stepStatus_->Text = "Add and pick a channel first."; return; }
        JObj^ b = sources_[idx];
        String^ sym = symbolCombo_->Text->Trim();
        String^ example = exampleBox_->Text->Trim();
        JObj^ p = J::New();
        p["chatId"] = J::Str(b, "chatId");
        p["title"] = J::Str(b, "title");
        String^ note = sym != "" ? "Capture symbol: " + sym : "Capture: (any symbol this channel posts)";
        if (example != "") note += "\r\nExample message:\r\n" + example;
        p["note"] = note;
        client_->Call("parser.requestCustom", p, gcnew ResponseHandler(this, &FirstRunWizard::OnParserRequested));
        // Remember the chosen symbol on the binding so it shows next time.
        if (sym != "") { b["captureSymbol"] = sym; SaveSources("Saved."); }
        stepStatus_->ForeColor = Theme::TextMuted;
        stepStatus_->Text = "Sending parser request for " + J::Str(b, "title") + "...";
    }
    void OnParserRequested(bool ok, Object^ result, String^ error) {
        if (!ok) { stepStatus_->ForeColor = Theme::Danger; stepStatus_->Text = "Request failed: " + error; return; }
        JObj^ r = J::Obj(result);
        if (J::Bool(r, "submitted")) {
            stepStatus_->ForeColor = Theme::Success;
            stepStatus_->Text = "Parser requested. Please wait up to 10-15 minutes - it builds on the backend and installs automatically (no restart). "
                                "I'll keep checking; Next unlocks once every channel's parser is installed.";
        } else {
            stepStatus_->ForeColor = Theme::Danger;
            stepStatus_->Text = "Couldn't reach the backend (HTTP " + J::Str(r, "status", "?") + "). Check your license key (step 1) and that the backend is running, then try again.";
        }
    }

    // Step 6: test a sample
    void OnSourcesForTest(bool ok, Object^ result, String^) {
        if (!ok) return;
        sources_ = gcnew System::Collections::Generic::List<JObj^>();
        for each (Object ^ o in J::Arr(result)) sources_->Add(J::Obj(o));
        testCombo_->Items->Clear();
        for each (JObj ^ b in sources_) testCombo_->Items->Add(J::Str(b, "title"));
        if (testCombo_->Items->Count > 0) testCombo_->SelectedIndex = 0;
    }
    void OnTest(Object^, EventArgs^) {
        int idx = testCombo_->SelectedIndex;
        if (sources_ == nullptr || idx < 0 || idx >= sources_->Count) { testResult_->ForeColor = Theme::Warning; testResult_->Text = "Pick a channel first (add sources on step 3)."; return; }
        if (testBox_->Text->Trim() == "") { testResult_->ForeColor = Theme::Warning; testResult_->Text = "Paste a sample message to test."; return; }
        JObj^ p = J::New();
        p["chatId"] = J::Str(sources_[idx], "chatId");
        p["text"] = testBox_->Text;
        testResult_->ForeColor = Theme::TextMuted;
        testResult_->Text = "Testing...";
        client_->Call("source.testParse", p, gcnew ResponseHandler(this, &FirstRunWizard::OnTested));
    }
    void OnTested(bool ok, Object^ result, String^ error) {
        if (!ok) { testResult_->ForeColor = Theme::Danger; testResult_->Text = "Test failed: " + error; return; }
        JObj^ r = J::Obj(result);
        if (!J::Bool(r, "parsed")) { testResult_->ForeColor = Theme::Warning; testResult_->Text = J::Str(r, "message"); return; }
        JObj^ sig = J::Obj(r, "signal");
        String^ status = J::Str(sig, "status");
        bool good = status == "Valid" || status == "Complete";
        String^ txt = "Parser: " + J::Str(r, "parser") + "\r\nStatus: " + status;
        if (J::Str(sig, "symbol") != "") txt += "\r\nSymbol: " + J::Str(sig, "symbol");
        if (J::Str(sig, "side") != "") txt += "   Side: " + J::Str(sig, "side");
        array<Object^>^ diags = J::Arr(sig, "diagnostics");
        if (diags != nullptr && diags->Length > 0) {
            txt += "\r\nNotes:";
            for each (Object ^ d in diags) txt += "\r\n  - " + d->ToString();
        }
        testResult_->ForeColor = good ? Theme::Success : Theme::Warning;
        testResult_->Text = txt;
    }

    void OnMtPair(Object^, EventArgs^) {
        JObj^ pending = lastSnap_ != nullptr ? J::Obj(J::Obj(lastSnap_, "bridge"), "pairingPending") : nullptr;
        if (pending == nullptr) {
            stepStatus_->ForeColor = Theme::Warning;
            stepStatus_->Text = "No pairing request yet. Start MetaTrader and log in to your broker account first.";
            return;
        }
        String^ typed = Prompt::Ask(this, "Approve pairing",
                                    "Destination account #" + J::Str(pending, "login") + " on " + J::Str(pending, "server") +
                                        ". Type the account number to bind Pipkite to this terminal:",
                                    "", false);
        if (typed == nullptr) return;
        JObj^ p = J::New();
        p["confirmLogin"] = typed->Trim();
        client_->Call("mt5.approvePairing", p);
        stepStatus_->Text = "Approving pairing... watch for 'MetaTrader connected' below.";
    }

    void OnSettings(bool ok, Object^ result, String^ error) {
        if (!ok) return;
        JObj^ s = J::Obj(result);
        portalUrl_ = J::Str(s, "license.portalUrl");
        if (String::IsNullOrEmpty(portalUrl_)) portalUrl_ = kDefaultPortal;
        String^ key = J::Str(s, "license.key");
        if (key != "") {
            licenseKey_->Text = key;
            licenseStatus_->Text = "A license key is already saved. You can re-check it with Validate & save.";
        }
        // Prefill everything the user entered before, so they can see it again (login code is never saved).
        String^ apiId = J::Str(s, "telegram.apiId");
        if (apiId != "" && apiId != "0") apiId_->Text = apiId;
        apiHash_->Text = J::Str(s, "telegram.apiHash");
        phone_->Text = J::Str(s, "telegram.phone");
    }

    void OnGetKey(Object^, EventArgs^) {
        String^ url = String::IsNullOrEmpty(portalUrl_) ? kDefaultPortal : portalUrl_;
        try {
            Diagnostics::Process::Start(url);
        } catch (Exception^) {
            MessageBox::Show(this, "Open this link to generate a free license key:\r\n" + url, "Get a license key");
        }
    }

    void OnValidateLicense(Object^, EventArgs^) {
        String^ key = licenseKey_->Text->Trim();
        if (key == "") {
            licenseStatus_->ForeColor = Theme::Warning;
            licenseStatus_->Text = "Enter a license key, or use the link above to generate one for free.";
            return;
        }
        licenseStatus_->ForeColor = Theme::TextMuted;
        licenseStatus_->Text = "Validating with the backend...";
        JObj^ p = J::New();
        p["key"] = key;
        client_->Call("license.validate", p, gcnew ResponseHandler(this, &FirstRunWizard::OnValidated));
    }

    void OnValidated(bool ok, Object^ result, String^ error) {
        if (!ok) {
            licenseStatus_->ForeColor = Theme::Danger;
            licenseStatus_->Text = "Could not reach the backend: " + error;
            return;
        }
        JObj^ r = J::Obj(result);
        if (!J::Bool(r, "valid")) {
            licenseStatus_->ForeColor = Theme::Danger;
            licenseStatus_->Text = "That key was not accepted (" + J::Str(r, "error", "invalid") + "). It was not saved.";
            return;
        }
        JObj^ v = J::New();
        v["license.key"] = licenseKey_->Text->Trim();
        JObj^ p = J::New();
        p["values"] = v;
        client_->Call("settings.save", p);
        JObj^ info = J::Obj(r, "info");
        String^ who = info != nullptr ? J::Str(info, "customerName") : "";
        licenseStatus_->ForeColor = Theme::Success;  // valid -> green
        licenseStatus_->Text = "License valid and saved. Backend features are now enabled." + (who != "" ? " Licensed to: " + who + "." : "");
    }
    void OnNext(Object^, EventArgs^) {
        if (step_ + 1 < steps_->GetLength(0)) ShowStep(step_ + 1);
        else Finish();
    }
    void OnBack(Object^, EventArgs^) {
        if (step_ > 0) ShowStep(step_ - 1);
    }
    void OnClose(Object^, EventArgs^) { Finish(); }

    // Each step requires the previous one: Next is disabled until the current step's prerequisite is met.
    bool StepComplete(int i) {
        if (i == 2) return lastSnap_ != nullptr && J::Str(J::Obj(lastSnap_, "telegram"), "state") == "Ready";  // signed in
        if (i == 3) return sources_ != nullptr && sources_->Count > 0;                                         // at least one source
        if (i == 4) return lastSnap_ != nullptr && J::Bool(J::Obj(lastSnap_, "bridge"), "healthy") && J::Obj(lastSnap_, "account") != nullptr;  // MT5 paired
        if (i == 5) return AllParsersInstalled();  // every channel must have a parser installed before testing
        return true;  // license / intro / test are not hard gates
    }
    void UpdateNav() {
        bool last = step_ == steps_->GetLength(0) - 1;
        next_->Enabled = last || StepComplete(step_);
        next_->Text = last ? "Finish" : StepComplete(step_) ? "Next" : "Next (finish this step)";
    }
    void Finish() {
        if (pollTimer_ != nullptr) pollTimer_->Stop();
        if (client_ != nullptr && snapHandler_ != nullptr) {
            client_->SnapshotReceived -= snapHandler_;
            snapHandler_ = nullptr;
        }
        JObj^ v = J::New();
        v["ui.firstRunComplete"] = "true";
        JObj^ p = J::New();
        p["values"] = v;
        if (client_ != nullptr) client_->Call("settings.save", p);
        Close();
    }
};

}  // namespace PipkiteUI
