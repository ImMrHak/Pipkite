#pragma once
// Signal rules = how each Telegram channel's messages are read. Parsers are built and maintained by the
// Pipkite team: the user picks which parser a channel uses (on the Telegram sources page) and, when a
// channel needs a new or adjusted parser, requests one here. The request (recent messages, redacted) goes
// to the backend; the finished parser is delivered as a signed module the app hot-reloads with no restart.
// There is no in-app parser editor, that complexity is handled for the user.

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
        grid_->Height = 240;
        grid_->AddCol("channel", "Channel", 300);
        grid_->AddCol("parser", "Parser", 220);
        DataGridViewTextBoxColumn^ st = grid_->AddCol("status", "Status", 360);
        st->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        grid_->EmptyText = "No channels yet. Add them on the Telegram sources page, then request a parser for each.";
        grid_->AccessibleName = "Channels and their parsers";
        grid_->SelectionChanged += gcnew EventHandler(this, &RulesPage::OnRowSelected);
        ch->Controls->Add(grid_);
        request_ = L::Btn("Request a custom parser for this channel...", "accent",
                          "Ask the Pipkite team to build or adjust the parser for the selected channel. Recent messages (redacted) are included.");
        request_->Click += gcnew EventHandler(this, &RulesPage::OnRequest);
        Button^ refresh = L::Btn("Refresh", nullptr, "Reload the channel list and parser status.");
        refresh->Click += gcnew EventHandler(this, &RulesPage::OnRefresh);
        Button^ updates = L::Btn("Check for parser updates", nullptr,
                                 "Ask the backend for new or updated parsers. They install and take effect automatically, without restarting the app.");
        updates->Click += gcnew EventHandler(this, &RulesPage::OnCheckUpdates);
        ch->Controls->Add(L::Row(request_, refresh, updates));
        status_ = L::Wrap("", "muted", 900);
        ch->Controls->Add(status_);
        stack->Controls->Add(L::Group("Your channels", ch));

        // installed parsers (read-only, informational)
        FlowLayoutPanel^ inst = gcnew FlowLayoutPanel();
        inst->FlowDirection = FlowDirection::TopDown;
        inst->AutoSize = true;
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
        stack->Controls->Add(L::Group("Installed parsers", inst));

        Controls->Add(stack);
        Header("Signal rules", "How each channel's messages are read. Parsers are built for you and update automatically.", this);
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
    Button^ request_;
    Label^ status_;
    array<Object^>^ sources_;
    array<Object^>^ parsers_;
    String^ focusChatId_;

    void Reload() { Client->Call("parser.list", nullptr, gcnew ResponseHandler(this, &RulesPage::OnParsers)); }
    void OnRefresh(Object^, EventArgs^) { Reload(); }

    void OnParsers(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        parsers_ = J::Arr(result);
        installed_->Rows->Clear();
        for each (Object^ o in parsers_) {
            JObj^ v = J::Obj(o);
            if (!J::Bool(v, "active")) continue;  // one row per active parser
            installed_->Rows->Add(gcnew array<Object^>{J::Str(v, "id"), J::Str(v, "version"),
                                                       J::Bool(v, "native") ? "Built-in" : J::Str(v, "origin") == "package" ? "Delivered" : "Custom", "Active"});
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
        grid_->Rows->Clear();
        for each (Object^ o in sources_) {
            JObj^ b = J::Obj(o);
            String^ pid = J::Str(b, "template");
            JObj^ active = ActiveParser(pid);
            String^ statusText;
            if (active == nullptr)
                statusText = "No parser installed — request one for this channel";
            else if (J::Bool(active, "native"))
                statusText = "Built-in parser · active";
            else
                statusText = (J::Str(active, "origin") == "package" ? "Delivered parser · v" : "Custom parser · v") + J::Str(active, "version");
            int idx = grid_->Rows->Add(gcnew array<Object^>{J::Str(b, "title"), pid, statusText});
            grid_->Rows[idx]->Tag = b;
            if (focusChatId_ != nullptr && J::Str(b, "chatId") == focusChatId_) grid_->Rows[idx]->Selected = true;
        }
        grid_->Invalidate();
        OnRowSelected(nullptr, nullptr);
    }

    JObj^ SelectedSource() {
        if (grid_->SelectedRows->Count == 0) return nullptr;
        return dynamic_cast<JObj^>(grid_->SelectedRows[0]->Tag);
    }
    void SelectByChatId(String^ chatId) {
        for each (DataGridViewRow^ r in grid_->Rows) {
            JObj^ b = dynamic_cast<JObj^>(r->Tag);
            if (b != nullptr && J::Str(b, "chatId") == chatId) {
                r->Selected = true;
                return;
            }
        }
    }

    void OnRowSelected(Object^, EventArgs^) { request_->Enabled = SelectedSource() != nullptr; }

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
