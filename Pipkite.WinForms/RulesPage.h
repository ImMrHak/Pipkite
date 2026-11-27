#pragma once
// Signal rules = how each Telegram channel's messages are read. Selecting a channel shows what Pipkite reads in its
// latest messages and lets the user start copying it (ChannelCheck). Under Advanced tools the user detects the
// format by hand, requests a custom parser from the Pipkite team or checks for parser updates. Delivered parsers and
// formats install while the app runs, with no restart.

#include "ChannelCheck.h"
#include "ChannelDialog.h"
#include "Pages.h"

using namespace System::Text;

namespace PipkiteUI {

// Modal: request a custom parser for one channel.
public ref class RequestParserDialog : public Form {
public:
    String^ ChatId;
    String^ Title;
    TextBox^ Note;
    RequestParserDialog(String^ chatId, String^ title) {
        ChatId = chatId;
        Title = title;
        Text = "Request a custom parser";
        FormBorderStyle = Windows::Forms::FormBorderStyle::FixedDialog;
        StartPosition = FormStartPosition::CenterParent;
        MinimizeBox = false;
        MaximizeBox = false;
        ShowInTaskbar = false;
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        AutoSize = true;
        AutoSizeMode = Windows::Forms::AutoSizeMode::GrowAndShrink;
        Font = Theme::Body;
        Padding = Windows::Forms::Padding(20);
        FlowLayoutPanel^ s = gcnew FlowLayoutPanel();
        s->FlowDirection = FlowDirection::TopDown;
        s->AutoSize = true;
        s->Controls->Add(L::Text("Request a parser for: " + title, "h2"));
        s->Controls->Add(L::Text("Notes for the team (optional)"));
        Note = gcnew TextBox();
        Note->Multiline = true;
        Note->Width = 460;
        Note->Height = 90;
        Note->AccessibleName = "Notes for the team";
        s->Controls->Add(Note);
        Button^ ok = L::Btn("Send request", "accent");
        ok->DialogResult = Windows::Forms::DialogResult::OK;
        Button^ cancel = L::Btn("Cancel");
        cancel->DialogResult = Windows::Forms::DialogResult::Cancel;
        FlowLayoutPanel^ row = L::Row(ok, cancel);
        row->Margin = Windows::Forms::Padding(0, 12, 0, 0);
        s->Controls->Add(row);
        Controls->Add(s);
        AcceptButton = ok;
        CancelButton = cancel;
        Theme::Apply(this);
        BackColor = Theme::Bg;
        HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
    }
};

public ref class RulesPage : public PageBase {
public:
    RulesPage() {
        FlowLayoutPanel^ stack = L::Stack();

        // channels and their parsers
        FlowLayoutPanel^ ch = gcnew FlowLayoutPanel();
        ch->FlowDirection = FlowDirection::TopDown;
        ch->AutoSize = true;
        grid_ = gcnew BufferedGrid();
        grid_->Width = 900;
        grid_->Height = 150;  // a few channels; what the selected one reads shows right below, without scrolling
        grid_->AddCol("channel", "Channel", 160);
        grid_->AddCol("parser", "Format", 140);
        grid_->AddCol("trades", "Copies with", 270);
        grid_->AddCol("status", "Status", 120);
        for each (DataGridViewColumn^ c in grid_->Columns) {  // they share the width, so the status still fits in a narrow window
            c->FillWeight = static_cast<float>(c->Width);
            c->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        }
        grid_->EmptyText = "No channels yet. Tick the ones you want to copy on the Telegram sources page.";
        grid_->AccessibleName = "Channels and their parsers";
        grid_->SelectionChanged += gcnew EventHandler(this, &RulesPage::OnRowSelected);
        ch->Controls->Add(grid_);
        settings_ = L::Btn("Channel settings...", nullptr,
                           "This channel's broker symbol, how it enters (wait for the price, right away, or pending orders) and its own sizing or "
                           "take-profit if it shouldn't follow Execution policy.");
        settings_->Click += gcnew EventHandler(this, &RulesPage::OnSettings);
        Button^ refresh = L::Btn("Refresh", nullptr, "Reload the channel list.");
        refresh->Click += gcnew EventHandler(this, &RulesPage::OnRefresh);
        ch->Controls->Add(L::Row(settings_, refresh));
        check_ = gcnew ChannelCheck(900);  // what Pipkite reads in the selected channel, with Start copying
        check_->Changed += gcnew EventHandler(this, &RulesPage::OnCheckChanged);
        ch->Controls->Add(check_);
        status_ = L::Wrap("", "muted", 900);
        ch->Controls->Add(status_);
        stack->Controls->Add(L::Group("Your channels", ch));

        // formats and parsers by hand, for people who know them: hidden until asked for
        advanced_ = L::Check("Show advanced tools");
        L::Tip(advanced_, "Detect format by hand, request a custom parser, check for parser updates and see the installed parsers.");
        advanced_->CheckedChanged += gcnew EventHandler(this, &RulesPage::OnAdvanced);
        stack->Controls->Add(advanced_);
        FlowLayoutPanel^ inst = gcnew FlowLayoutPanel();
        inst->FlowDirection = FlowDirection::TopDown;
        inst->AutoSize = true;
        detect_ = L::Btn("Detect format...", nullptr,
                         "Try every installed format on the selected channel's recent messages, see what each reads, and pick one. You can also teach "
                         "Pipkite by marking the parts of a signal.");
        detect_->Click += gcnew EventHandler(this, &RulesPage::OnDetect);
        request_ = L::Btn("Request a custom parser...", nullptr,
                          "Ask the Pipkite team to build or adjust the parser for the selected channel. Recent messages (redacted) are included.");
        request_->Click += gcnew EventHandler(this, &RulesPage::OnRequest);
        Button^ updates = L::Btn("Check for parser updates", nullptr,
                                 "Ask the backend for new or updated parsers. They install and take effect automatically, without restarting the app.");
        updates->Click += gcnew EventHandler(this, &RulesPage::OnCheckUpdates);
        inst->Controls->Add(L::Row(detect_, request_, updates));
        installed_ = gcnew BufferedGrid();
        installed_->Width = 900;
        installed_->Height = 150;
        installed_->AddCol("id", "Parser", 240);
        installed_->AddCol("version", "Version", 110);
        installed_->AddCol("origin", "Source", 140);
        DataGridViewTextBoxColumn^ ia = installed_->AddCol("active", "Active", 120);
        ia->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        installed_->EmptyText = "No parsers installed yet.";
        installed_->AccessibleName = "Installed parsers";
        inst->Controls->Add(installed_);
        advancedBox_ = L::Group("Advanced tools", inst);
        advancedBox_->Visible = false;
        stack->Controls->Add(advancedBox_);

        Controls->Add(stack);
        Header("Signal rules", "Select a channel to see what Pipkite reads in its latest messages, then start copying it.", this);
    }

    // Select a channel's row (used by the Telegram sources page "Edit signal rule" button).
    void FocusSource(String^ chatId) {
        focusChatId_ = chatId;
        SelectByChatId(chatId);
    }

    virtual void OnActivated() override {
        if (Client == nullptr || !Client->Connected) return;
        Reload();
    }

private:
    BufferedGrid ^ grid_, ^installed_;
    Button ^ request_, ^detect_, ^settings_;
    ChannelCheck^ check_;
    CheckBox^ advanced_;
    GroupBox^ advancedBox_;
    Label^ status_;
    bool filling_;
    array<Object^>^ sources_;
    array<Object^>^ parsers_;
    String^ focusChatId_;

    void Reload() { Client->Call("parser.list", nullptr, gcnew ResponseHandler(this, &RulesPage::OnParsers)); }
    void OnRefresh(Object^, EventArgs^) { Reload(); }
    void OnCheckChanged(Object^, EventArgs^) { Reload(); }
    void OnAdvanced(Object^, EventArgs^) { advancedBox_->Visible = advanced_->Checked; }

    void OnParsers(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        parsers_ = J::Arr(result);
        installed_->Rows->Clear();
        for each (Object^ o in parsers_) {
            JObj^ v = J::Obj(o);
            if (!J::Bool(v, "active")) continue;  // one row per active parser
            installed_->Rows->Add(gcnew array<Object^>{J::Str(v, "id"), J::Str(v, "version"), OriginText(v), "Active"});
        }
        installed_->Invalidate();
        Client->Call("sources.list", nullptr, gcnew ResponseHandler(this, &RulesPage::OnSources));
    }

    JObj^ ActiveParser(String^ id) {
        if (parsers_ == nullptr) return nullptr;
        for each (Object^ o in parsers_) {
            JObj^ v = J::Obj(o);
            if (J::Str(v, "id") == id && J::Bool(v, "active")) return v;
        }
        return nullptr;
    }

    void OnSources(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        sources_ = J::Arr(result);
        filling_ = true;  // the selected channel is shown once the list is rebuilt
        grid_->Rows->Clear();
        for each (Object^ o in sources_) {
            JObj^ b = J::Obj(o);
            // the parser the channel really uses (its own, or the one delivered for every channel)
            String^ format = J::Str(b, "activeFormat");
            String^ pid = format->Contains("@") ? format->Substring(0, format->IndexOf("@")) : format;
            JObj^ active = ActiveParser(pid);
            const bool pending = J::Bool(b, "formatPending");
            const bool paused = !J::Bool(b, "enabled", true);
            // what to do about it is shown below the list for the selected channel
            String^ statusText = paused ? "Paused" : active == nullptr ? "Not set up" : pending ? "Not copying yet" : "Copying";
            int idx = grid_->Rows->Add(gcnew array<Object^>{J::Str(b, "title"), pid == "" ? "-" : pid, CopiesWith(b), statusText});
            grid_->Rows[idx]->Tag = b;
            if (paused) grid_->Rows[idx]->DefaultCellStyle->ForeColor = Theme::TextMuted;
            else if (pending) grid_->Rows[idx]->DefaultCellStyle->ForeColor = Theme::Warning;
        }
        if (focusChatId_ != nullptr) SelectByChatId(focusChatId_);  // the channel selected before the reload stays selected
        grid_->Invalidate();
        filling_ = false;
        OnRowSelected(nullptr, nullptr);
        MaybeOpenForTest();
    }

    // test-only (PIPKITE_TEST_HOOKS=1 and PIPKITE_TEST_OPEN=channel): opens the first channel's settings, for screenshots
    static bool testOpened_;
    void MaybeOpenForTest() {
        if (testOpened_ || grid_->Rows->Count == 0 || Environment::GetEnvironmentVariable("PIPKITE_TEST_HOOKS") != "1" ||
            Environment::GetEnvironmentVariable("PIPKITE_TEST_OPEN") != "channel")
            return;
        testOpened_ = true;
        grid_->Rows[0]->Selected = true;
        BeginInvoke(gcnew EventHandler(this, &RulesPage::OnSettings), gcnew array<Object^>{this, EventArgs::Empty});
    }

    JObj^ SelectedSource() {
        if (grid_->SelectedRows->Count == 0) return nullptr;
        return dynamic_cast<JObj^>(grid_->SelectedRows[0]->Tag);
    }
    void SelectByChatId(String^ chatId) {
        for each (DataGridViewRow^ r in grid_->Rows) {
            JObj^ b = dynamic_cast<JObj^>(r->Tag);
            if (b != nullptr && J::Str(b, "chatId") == chatId) {
                grid_->CurrentCell = r->Cells[0];  // selects the row and moves the keyboard there
                return;
            }
        }
    }

    // the channel's own settings at a glance; everything else follows Execution policy and Symbol limits
    static String^ CopiesWith(JObj^ b) {
        String^ sym = J::Str(b, "tradeSymbol");
        String^ mode = J::Str(b, "entryMode", "wait_for_range");
        String^ text = (sym == "" ? "message's symbol" : sym + " only") + " · " +
                       (mode == "immediate_within_tolerance" ? "enters right away" : mode == "explicit_pending_only" ? "pending orders" : "waits for the price");
        if (J::Bool(b, "overrideSizing")) text += " · own sizing";
        if (J::Bool(b, "overrideTargets")) text += " · own take-profit";
        return text;
    }

    void OnSettings(Object^, EventArgs^) {
        JObj^ b = SelectedSource();
        if (b == nullptr) return ShowError("Select a channel first.");
        ChannelDialog^ d = gcnew ChannelDialog(Client, b);
        if (d->ShowDialog(FindForm()) == Windows::Forms::DialogResult::OK) {
            status_->Text = J::Str(b, "title") + ": settings saved.";
            Reload();
        }
    }

    static String^ OriginText(JObj^ v) {
        String^ o = J::Str(v, "origin");
        if (J::Bool(v, "native")) return "Parser module";
        return o == "builtin" ? "Built in" : o == "package" ? "Delivered" : "Taught here";
    }

    void OnRowSelected(Object^, EventArgs^) {
        JObj^ b = SelectedSource();
        request_->Enabled = b != nullptr;
        detect_->Enabled = request_->Enabled;
        settings_->Enabled = request_->Enabled;
        if (filling_) return;
        if (b == nullptr) {
            check_->Visible = false;
            return;
        }
        focusChatId_ = J::Str(b, "chatId");
        check_->ShowChannel(Client, b);
    }

    void OnDetect(Object^, EventArgs^) {
        JObj^ b = SelectedSource();
        if (b == nullptr) return ShowError("Select a channel first.");
        FormatDialog^ d = gcnew FormatDialog(Client, J::Str(b, "chatId"), J::Str(b, "title"), nullptr);
        if (d->ShowDialog(FindForm()) == Windows::Forms::DialogResult::OK) {
            status_->Text = J::Str(b, "title") + " now uses the format you picked. Its signals are not copied until you press Start copying.";
            check_->Recheck();
            Reload();
        }
    }

    void OnRequest(Object^, EventArgs^) {
        JObj^ b = SelectedSource();
        if (b == nullptr) return ShowError("Select a channel first.");
        RequestParserDialog^ d = gcnew RequestParserDialog(J::Str(b, "chatId"), J::Str(b, "title"));
        if (d->ShowDialog(FindForm()) != Windows::Forms::DialogResult::OK) return;
        JObj^ p = J::New();
        p["chatId"] = J::Str(b, "chatId");
        p["title"] = J::Str(b, "title");
        p["note"] = d->Note->Text;
        status_->Text = "Sending request...";
        Client->Call("parser.requestCustom", p, gcnew ResponseHandler(this, &RulesPage::OnRequested));
    }

    void OnRequested(bool ok, Object^ result, String^ error) {
        if (!ok) {
            status_->Text = "";
            return ShowError(error);
        }
        JObj^ r = J::Obj(result);
        String^ msg;
        if (J::Bool(r, "submitted"))
            msg = "Request sent to the Pipkite team (" + J::Str(r, "sampleCount") + " sample messages). The parser will arrive automatically as an update; no restart needed.";
        else
            msg = "Request saved on this PC (" + J::Str(r, "sampleCount") + " sample messages):\n" + J::Str(r, "saved") +
                  "\n\nNo backend request address is set (Settings), so send that file to the Pipkite team. When the parser is published it installs automatically.";
        status_->Text = J::Bool(r, "submitted") ? "Request sent. The parser will install automatically when ready." : "Request saved locally - send it to the team.";
        ShowInfo("Custom parser requested", msg);
    }

    void OnCheckUpdates(Object^, EventArgs^) { Client->Call("updates.check", nullptr, gcnew ResponseHandler(this, &RulesPage::OnUpdateChecked)); }
    void OnUpdateChecked(bool ok, Object^, String^ error) {
        if (!ok) return ShowError(error);
        status_->Text = "Checking for new and updated parsers in the background. Any that arrive install automatically, without restarting.";
    }
};

}  // namespace PipkiteUI
