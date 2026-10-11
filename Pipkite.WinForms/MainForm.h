#pragma once
// Main window: persistent top bar (Live/Paused badge, account suffix and server, connection
// status, Pause copying), keyboard-navigable sidebar, pages, tray icon. Closing minimizes to the tray;
// Exit stops the engine after an orderly shutdown.

#include "DashboardPage.h"
#include "Dialogs.h"
#include "HistoryPage.h"
#include "MetaTraderPage.h"
#include "PolicyPage.h"
#include "RulesPage.h"
#include "SettingsPage.h"
#include "SourcesPage.h"
#include "Wizard.h"

using namespace Microsoft::Win32;

namespace PipkiteUI {

// Flat status bar in theme colors (no gradients), with a hairline top border.
public ref class FlatStripRenderer : public ToolStripProfessionalRenderer {
protected:
    virtual void OnRenderToolStripBackground(ToolStripRenderEventArgs^ e) override { e->Graphics->FillRectangle(Theme::SurfaceBrush, e->AffectedBounds); }
    virtual void OnRenderToolStripBorder(ToolStripRenderEventArgs^ e) override { e->Graphics->DrawLine(Theme::BorderPen, 0, 0, e->ToolStrip->Width, 0); }
    virtual void OnRenderItemText(ToolStripItemTextRenderEventArgs^ e) override {
        e->TextColor = Theme::TextMuted;
        e->TextFont = Theme::Small;
        ToolStripProfessionalRenderer::OnRenderItemText(e);
    }
};

public ref class MainForm : public Form {
public:
    MainForm(bool startMinimized) {
        startMinimized_ = startMinimized;
        minimizeToTray_ = true;
        frameWatch_ = gcnew Diagnostics::Stopwatch();
        notifications_ = true;
        Text = "Pipkite";
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        AutoScaleDimensions = SizeF(96, 96);
        Size = Drawing::Size(1280, 800);
        MinimumSize = Drawing::Size(1024, 640);
        StartPosition = FormStartPosition::CenterScreen;
        Font = Theme::Body;
        KeyPreview = true;
        try {
            Icon = System::Drawing::Icon::ExtractAssociatedIcon(Application::ExecutablePath);
        } catch (Exception^) {
        }

        client_ = gcnew EngineClient(this);
        client_->SnapshotReceived += gcnew SnapshotHandler(this, &MainForm::OnSnapshot);
        client_->NotificationReceived += gcnew NotificationHandler(this, &MainForm::OnNotification);
        client_->ConnectionChanged += gcnew ConnectionHandler(this, &MainForm::OnConnection);

        // top bar
        top_ = gcnew Panel();
        top_->Dock = DockStyle::Top;
        top_->Height = 64;
        top_->Tag = "surface";
        top_->Padding = Windows::Forms::Padding(18, 0, 18, 0);
        // Full-width, responsive top bar: brand on the left, status badges stretch in the middle, action
        // buttons pinned right. A TableLayoutPanel (auto | 100% | auto) absorbs the window width dynamically.
        TableLayoutPanel^ bar = gcnew TableLayoutPanel();
        bar->Dock = DockStyle::Fill;
        bar->ColumnCount = 3;
        bar->RowCount = 1;
        bar->Tag = "surface";
        bar->ColumnStyles->Add(gcnew ColumnStyle(SizeType::AutoSize));
        bar->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Percent, 100));
        bar->ColumnStyles->Add(gcnew ColumnStyle(SizeType::AutoSize));
        bar->RowStyles->Add(gcnew RowStyle(SizeType::Percent, 100));  // single full-height row so children vertically centre

        BrandMark^ mark = gcnew BrandMark();
        mark->Margin = Windows::Forms::Padding(0, 0, 10, 0);
        Label^ brand = L::Text("Pipkite", "h2");
        brand->AutoSize = true;
        brand->Margin = Windows::Forms::Padding(0);
        FlowLayoutPanel^ leftGroup = gcnew FlowLayoutPanel();
        leftGroup->AutoSize = true;
        leftGroup->WrapContents = false;
        leftGroup->Margin = Windows::Forms::Padding(0);
        leftGroup->Anchor = AnchorStyles::Left;  // vertically centred, left-aligned
        leftGroup->Tag = "surface";
        leftGroup->Controls->Add(mark);
        leftGroup->Controls->Add(brand);

        modeBadge_ = gcnew Badge();
        accountBadge_ = gcnew Badge();
        connBadge_ = gcnew Badge();
        for each (Badge ^ b in gcnew array<Badge^>{modeBadge_, accountBadge_, connBadge_}) b->Margin = Windows::Forms::Padding(0, 0, 10, 0);
        FlowLayoutPanel^ midGroup = gcnew FlowLayoutPanel();
        midGroup->AutoSize = true;
        midGroup->WrapContents = false;
        midGroup->Margin = Windows::Forms::Padding(16, 0, 16, 0);
        midGroup->Anchor = AnchorStyles::None;  // centred in the stretchy middle column -> balanced, full-width
        midGroup->Tag = "surface";
        midGroup->Controls->Add(modeBadge_);
        midGroup->Controls->Add(accountBadge_);
        midGroup->Controls->Add(connBadge_);

        pauseBtn_ = L::Btn("Pause copying", "danger", "Stop copying new signals. Orders already sent can still fill; this never closes open trades.");
        pauseBtn_->Margin = Windows::Forms::Padding(0);
        pauseBtn_->Click += gcnew EventHandler(this, &MainForm::OnPause);
        FlowLayoutPanel^ rightGroup = gcnew FlowLayoutPanel();
        rightGroup->FlowDirection = FlowDirection::RightToLeft;  // first added sits rightmost
        rightGroup->AutoSize = true;
        rightGroup->WrapContents = false;
        rightGroup->Margin = Windows::Forms::Padding(0);
        rightGroup->Anchor = AnchorStyles::Right;
        rightGroup->Tag = "surface";
        rightGroup->Controls->Add(pauseBtn_);

        L::Tip(modeBadge_, "Live: copying is active. Not ready: something still needs setting up (see the Dashboard). Paused: you paused copying.");
        L::Tip(accountBadge_, "The MetaTrader account orders are sent to.");
        L::Tip(connBadge_, "Connection status: your Telegram sign-in and the MetaTrader bridge.");

        bar->Controls->Add(leftGroup, 0, 0);
        bar->Controls->Add(midGroup, 1, 0);
        bar->Controls->Add(rightGroup, 2, 0);
        top_->Controls->Add(bar);
        Divider^ topLine = gcnew Divider();  // hairline under the top bar

        // navigation
        nav_ = gcnew Panel();
        nav_->Dock = DockStyle::Left;
        nav_->Width = 220;
        nav_->Tag = "surface";
        nav_->Padding = Windows::Forms::Padding(0, 12, 0, 0);
        Label^ navFooter = L::Text("Version 1.0.0", "muted");
        navFooter->Dock = DockStyle::Bottom;
        navFooter->AutoSize = false;
        navFooter->Height = 34;
        navFooter->Padding = Windows::Forms::Padding(24, 0, 0, 12);
        navFooter->Font = Theme::Small;
        nav_->Controls->Add(navFooter);
        Divider^ navLine = gcnew Divider();  // hairline between sidebar and content
        navLine->Dock = DockStyle::Left;
        content_ = gcnew Panel();
        content_->Dock = DockStyle::Fill;

        pages_ = gcnew array<PageBase^>{gcnew DashboardPage(), gcnew SourcesPage(), gcnew MetaTraderPage(), gcnew RulesPage(), gcnew PolicyPage(), gcnew HistoryPage(),
                                        gcnew SettingsPage()};
        array<String^>^ names = {"Dashboard", "Telegram sources", "MetaTrader", "Signal rules", "Execution policy", "History", "Settings"};
        array<String^>^ glyphs = {L"\xE80F", L"\xE8BD", L"\xE7F4", L"\xE943", L"\xE72E", L"\xE81C", L"\xE713"};
        navButtons_ = gcnew array<NavButton^>(pages_->Length);
        for (int i = pages_->Length - 1; i >= 0; --i) {
            NavButton^ b = gcnew NavButton(glyphs[i], names[i]);
            b->TabIndex = i;
            b->AccessibleDescription = "Ctrl+" + (i + 1);
            b->Click += gcnew EventHandler(this, &MainForm::OnNav);
            navButtons_[i] = b;
            nav_->Controls->Add(b);
            pages_[i]->Client = client_;
            pages_[i]->Visible = false;
            content_->Controls->Add(pages_[i]);
        }
        status_ = gcnew StatusStrip();
        status_->SizingGrip = true;
        statusText_ = gcnew ToolStripStatusLabel("Starting...");
        statusText_->Spring = true;
        statusText_->TextAlign = ContentAlignment::MiddleLeft;
        statusRight_ = gcnew ToolStripStatusLabel("Pipkite 1.0.0");
        status_->Items->Add(statusText_);
        status_->Items->Add(statusRight_);
        status_->Renderer = gcnew FlatStripRenderer();
        status_->Font = Theme::Small;
        status_->Padding = Windows::Forms::Padding(12, 2, 8, 2);

        Controls->Add(content_);
        Controls->Add(navLine);
        Controls->Add(nav_);
        Controls->Add(topLine);
        Controls->Add(top_);
        Controls->Add(status_);

        // tray
        tray_ = gcnew NotifyIcon();
        tray_->Icon = Icon;
        tray_->Text = "Pipkite";
        tray_->Visible = true;
        tray_->DoubleClick += gcnew EventHandler(this, &MainForm::OnTrayOpen);
        Windows::Forms::ContextMenuStrip^ menu = gcnew Windows::Forms::ContextMenuStrip();
        menu->Items->Add("Open Pipkite", nullptr, gcnew EventHandler(this, &MainForm::OnTrayOpen));
        trayPause_ = gcnew ToolStripMenuItem("Pause copying", nullptr, gcnew EventHandler(this, &MainForm::OnPause));
        menu->Items->Add(trayPause_);
        menu->Items->Add(gcnew ToolStripSeparator());
        menu->Items->Add("Exit (stop engine)", nullptr, gcnew EventHandler(this, &MainForm::OnExitMenu));
        menu->Renderer = gcnew ThemedMenuRenderer();
        menu->Font = Theme::Body;
        tray_->ContextMenuStrip = menu;

        DashboardPage::OpenDetail = gcnew DetailRequest(this, &MainForm::OpenDetail);
        DashboardPage::Navigate = gcnew GoToPage(this, &MainForm::ShowPage);
        SettingsPage::ThemeChangeRequested = gcnew ThemeRequest(this, &MainForm::ApplyThemeName);
        SettingsPage::ExitRequested = gcnew MethodInvoker(this, &MainForm::ExitApplication);
        FirstRunWizard::Navigate = gcnew NavigateRequest(this, &MainForm::ShowPage);
        SourcesPage::EditRuleRequested = gcnew EditRuleRequest(this, &MainForm::OpenRuleForSource);

        Theme::Apply(this);
        BackColor = Theme::Bg;
        ShowPage(0);
    }

protected:
    virtual void OnHandleCreated(EventArgs^ e) override {
        Form::OnHandleCreated(e);
        Theme::TitleBar(this);  // dark native title bar matching the theme
    }

    virtual void OnLoad(EventArgs^ e) override {
        Form::OnLoad(e);
        client_->Start();
        if (startMinimized_) {
            WindowState = FormWindowState::Minimized;
            BeginInvoke(gcnew MethodInvoker(this, &MainForm::Hide));
        }
    }

    virtual void OnFormClosing(FormClosingEventArgs^ e) override {
        if (!exiting_ && e->CloseReason == CloseReason::UserClosing && minimizeToTray_) {
            e->Cancel = true;
            Hide();
            if (!trayHintShown_) {
                trayHintShown_ = true;
                tray_->ShowBalloonTip(4000, "Pipkite is still running", "Copying continues in the background. Use Exit in the tray menu to stop the engine.", ToolTipIcon::Info);
            }
            return;
        }
        tray_->Visible = false;
        client_->Stop();
        Form::OnFormClosing(e);
    }

    virtual void OnKeyDown(KeyEventArgs^ e) override {
        if (e->Control && e->KeyCode >= Keys::D1 && e->KeyCode <= Keys::D7) {
            ShowPage((int)(e->KeyCode - Keys::D1));
            e->Handled = true;
        }
        Form::OnKeyDown(e);
    }

private:
    EngineClient^ client_;
    Panel ^ top_, ^nav_, ^content_;
    Badge ^ modeBadge_, ^accountBadge_, ^connBadge_;
    Button^ pauseBtn_;
    Label^ pauseNote_;
    array<PageBase^>^ pages_;
    array<NavButton^>^ navButtons_;
    StatusStrip^ status_;
    ToolStripStatusLabel ^ statusText_, ^statusRight_;
    NotifyIcon^ tray_;
    ToolStripMenuItem^ trayPause_;
    int current_;
    bool exiting_, trayHintShown_, startMinimized_;
    bool minimizeToTray_;
    bool notifications_;
    bool settingsLoaded_;
    bool restartPrompted_;
    bool paused_;
    String^ mode_;
    JObj^ snapshot_;
    Diagnostics::Stopwatch^ frameWatch_;
    double frameMs_;

    void ShowPage(int index) {
        if (index < 0 || index >= pages_->Length) return;
        current_ = index;
        content_->SuspendLayout();
        for (int i = 0; i < pages_->Length; ++i) {
            pages_[i]->Visible = i == index;
            navButtons_[i]->Selected = i == index;
            navButtons_[i]->Invalidate();
        }
        content_->ResumeLayout();
        if (dynamic_cast<NavButton^>(ActiveControl) != nullptr) navButtons_[index]->Focus();  // keyboard focus follows the selection
        if (snapshot_ != nullptr) pages_[index]->OnSnapshot(snapshot_);
        pages_[index]->OnActivated();
    }

    void OpenRuleForSource(String^ chatId) {
        safe_cast<RulesPage^>(pages_[3])->FocusSource(chatId);
        ShowPage(3);
    }

    void OnNav(Object^ sender, EventArgs^) { ShowPage(safe_cast<NavButton^>(sender)->TabIndex); }

    void OnConnection(bool connected, String^ detail) {
        connBadge_->Set(connected ? "Engine connected" : "Engine offline", connected ? Theme::Success : Theme::Warning);
        statusText_->Text = detail;
        if (connected) {
            client_->Call("settings.get", nullptr, gcnew ResponseHandler(this, &MainForm::OnSettings));
            pages_[current_]->OnActivated();
        }
    }

    void OnSettings(bool ok, Object^ result, String^) {
        if (!ok) return;
        JObj^ s = J::Obj(result);
        minimizeToTray_ = J::Str(s, "ui.minimizeToTray", "true") == "true";
        notifications_ = J::Str(s, "ui.notifications", "true") == "true";
        ApplyThemeName(J::Str(s, "ui.theme", "dark"));
        DashboardPage::SetupHidden = J::Str(s, "ui.setupHidden", "false") == "true";
        if (!settingsLoaded_) {
            settingsLoaded_ = true;
            if (J::Str(s, "ui.firstRunComplete", "false") != "true") BeginInvoke(gcnew MethodInvoker(this, &MainForm::ShowWizard));
            JObj^ p = J::New();
            p["limit"] = 20;
            client_->Call("updates.history", p, gcnew ResponseHandler(this, &MainForm::OnUpdateHistory));
        }
    }

    void ShowWizard() {
        if (!Visible) return;
        FirstRunWizard^ w = gcnew FirstRunWizard(client_);
        w->ShowDialog(this);
    }

    void OnUpdateHistory(bool ok, Object^ result, String^) {
        if (!ok) return;
        // Show "What's changed" once per successful release/activation; later available from Settings.
        List<Object^>^ fresh = gcnew List<Object^>();
        for each (Object^ o in J::Arr(result)) {
            JObj^ e = J::Obj(o);
            if (J::Str(e, "state") == "activated" && !J::Bool(e, "notesShown") && J::Str(J::Obj(e, "notes"), "releaseId") != "local") fresh->Add(o);
        }
        if (fresh->Count > 0) {
            WhatsChangedForm^ f = gcnew WhatsChangedForm(fresh->ToArray(), client_, false);
            f->Show(this);
        }
    }

    void OnSnapshot(JObj^ s) {
        frameWatch_->Restart();
        snapshot_ = s;
        // A core/UI update that needs a restart: offer now or later (parser/bridge updates hot-reload silently).
        JObj^ upd = J::Obj(s, "updates");
        if (!restartPrompted_ && upd != nullptr && J::Bool(upd, "restartPending")) {
            restartPrompted_ = true;
            String^ ver = J::Str(upd, "pendingVersion");
            if (MessageBox::Show(this, "A Pipkite update" + (ver != "" ? " (v" + ver + ")" : "") +
                                           " is downloaded and ready. It needs a quick restart to apply.\n\nUpdate now?",
                                 "Update ready", MessageBoxButtons::YesNo, MessageBoxIcon::Information) == System::Windows::Forms::DialogResult::Yes)
                client_->Call("engine.shutdown", nullptr);  // the launcher relaunches and applies the staged update
        }
        mode_ = J::Str(s, "mode");
        paused_ = J::Bool(s, "paused");
        bool userPaused = J::Bool(s, "userPaused");
        const bool ready = J::Str(s, "dispatchBlockedReason") == "";
        modeBadge_->Set(userPaused ? "Paused" : ready ? "Live" : "Not ready", userPaused || !ready ? Theme::Warning : Theme::Success);
        JObj^ acc = J::Obj(s, "account");
        JObj^ bound = J::Obj(s, "boundAccount");
        JObj^ key = acc != nullptr ? J::Obj(acc, "account") : bound;
        if (key != nullptr) {
            String^ login = J::Str(key, "login");
            String^ suffix = login->Length > 4 ? "..." + login->Substring(login->Length - 4) : login;
            accountBadge_->Set("#" + suffix + "  ·  " + J::Str(key, "server"), J::Bool(J::Obj(s, "bridge"), "healthy") ? Theme::Success : Theme::Danger);
        } else {
            accountBadge_->Set("No MT5 account", Theme::TextMuted);
        }
        String^ tg = J::Str(J::Obj(s, "telegram"), "state");
        bool bridgeOk = J::Bool(J::Obj(s, "bridge"), "healthy");
        connBadge_->Set(String::Format("Telegram {0}  ·  MT5 {1}", tg == "Ready" ? "online" : tg == "LibraryMissing" ? "not installed" : tg == "NotConfigured" ? "not set up" : "signing in", bridgeOk ? "online" : "offline"), tg == "Ready" && bridgeOk ? Theme::Success : Theme::Warning);
        String^ pauseText = J::Bool(s, "userPaused") ? "Resume copying" : "Pause copying";
        if (pauseBtn_->Text != pauseText) {
            pauseBtn_->Text = pauseText;
            pauseBtn_->Tag = J::Bool(s, "userPaused") ? "accent" : "danger";
            L::Tip(pauseBtn_, J::Bool(s, "userPaused") ? "Resume copying new signals." : "Stop copying new signals. Orders already sent can still fill; this never closes open trades.");
            Theme::Apply(pauseBtn_);
        }
        if (trayPause_->Text != pauseText) trayPause_->Text = pauseText;
        String^ blocked = J::Str(s, "dispatchBlockedReason");
        String^ statusLine = (userPaused ? "Paused  ·  " : "") + (blocked == "" ? "Ready to send orders" : blocked) + "  ·  Configuration v" + J::Str(s, "configVersion");
        if (statusText_->Text != statusLine) statusText_->Text = statusLine;
        String^ trayText = String::Concat("Pipkite", userPaused ? " - paused" : " - live");
        if (tray_->Text != trayText) tray_->Text = trayText;  // Shell_NotifyIcon only on change
        pages_[current_]->OnSnapshot(s);
        // UI frame cost (snapshot handling incl. layout of changed controls), smoothed; shown so the cost stays visible.
        const double ms = frameWatch_->Elapsed.TotalMilliseconds;
        frameMs_ = frameMs_ <= 0 ? ms : frameMs_ * 0.9 + ms * 0.1;
        statusRight_->Text = "Updated " + DateTime::Now.ToString("HH:mm:ss") + "  ·  UI " + frameMs_.ToString("0.0") + " ms  ·  " + J::Str(s, "version");
    }

    void OnNotification(String^ level, String^ message) {
        statusText_->Text = message;
        if (notifications_ && (level == "error" || level == "warning") && !Visible)
            tray_->ShowBalloonTip(5000, "Pipkite", message, level == "error" ? ToolTipIcon::Error : ToolTipIcon::Warning);
    }

    void OnPause(Object^, EventArgs^) {
        if (snapshot_ != nullptr && J::Bool(snapshot_, "userPaused")) client_->Call("engine.resume", nullptr, gcnew ResponseHandler(this, &MainForm::OnPauseResult));
        else client_->Call("engine.pause", nullptr, gcnew ResponseHandler(this, &MainForm::OnPauseResult));
    }

    void OnPauseResult(bool ok, Object^ result, String^ error) {
        if (!ok) return Prompt::Error(this, error);
        statusText_->Text = J::Str(J::Obj(result), "note");
    }

    void OpenDetail(String^ chatId, String^ messageId, int revision) {
        SignalDetailForm^ f = gcnew SignalDetailForm(client_, chatId, messageId, revision);
        f->Show(this);
    }

    void ApplyThemeName(String^ name) {
        bool dark = name != "light";
        if (name == "system") {
            try {
                Object^ v = Registry::GetValue("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", "AppsUseLightTheme", gcnew Int32(0));
                dark = v == nullptr || Convert::ToInt32(v) == 0;
            } catch (Exception^) {
                dark = true;
            }
        }
        if (dark == Theme::Dark && BackColor == Theme::Bg) return;
        Theme::Set(dark);
        SuspendLayout();
        Theme::Apply(this);
        BackColor = Theme::Bg;
        for each (PageBase^ p in pages_) p->OnThemeChanged();
        ResumeLayout();
        Theme::TitleBar(this);
        Invalidate(true);
    }

    void OnTrayOpen(Object^, EventArgs^) {
        Show();
        WindowState = FormWindowState::Normal;
        Activate();
    }

    void OnExitMenu(Object^, EventArgs^) { ExitApplication(); }

    void ExitApplication() {
        if (MessageBox::Show(this->Visible ? this : nullptr, "Exit Pipkite and stop the engine? New signals will not be copied until you start it again. Existing positions stay at the broker.",
                             "Exit Pipkite", MessageBoxButtons::YesNo, MessageBoxIcon::Question) != Windows::Forms::DialogResult::Yes)
            return;
        exiting_ = true;
        client_->Call("engine.shutdown", nullptr);  // orderly engine shutdown
        Threading::Thread::Sleep(300);
        Close();
    }
};

}  // namespace PipkiteUI
