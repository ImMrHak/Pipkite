#pragma once
// Telegram sources: user-session authentication and searchable chats (groups and channels clearly distinguished).
// Ticking a chat adds it and shows what Pipkite reads in it, with Start copying; unticking pauses it and keeps its
// settings. Per-channel settings are in Signal rules.
// Source bindings are keyed by chat ID; renaming a chat never changes its binding.

#include "ChannelCheck.h"
#include "Pages.h"

namespace PipkiteUI {

public delegate void EditRuleRequest(String^ chatId);

public ref class SourcesPage : public PageBase {
public:
    static EditRuleRequest^ EditRuleRequested;  // opens Signal rules for one source
    SourcesPage() {
        FlowLayoutPanel^ stack = L::Stack();

        // Telegram authentication
        TableLayoutPanel^ auth = L::Form2();
        authState_ = gcnew Badge();
        L::AddField(auth, "Status", authState_);
        apiId_ = L::Box(160);
        L::AddField(auth, "API ID", apiId_, "Your Telegram api_id from my.telegram.org. Stored encrypted (DPAPI) and never logged.");
        apiHash_ = L::Box(300);
        apiHash_->UseSystemPasswordChar = true;
        L::AddField(auth, "API hash", apiHash_, "Your Telegram api_hash from my.telegram.org. Stored encrypted (DPAPI) and never logged.");
        saveCred_ = L::Btn("Save credentials", "accent", "Store your Telegram api_id and api_hash (from my.telegram.org). They are DPAPI-encrypted and never logged.");
        saveCred_->Click += gcnew EventHandler(this, &SourcesPage::OnSaveCredentials);
        L::AddField(auth, "", saveCred_);
        phone_ = L::Box(200);
        Button^ sendPhone = L::Btn("Send code", nullptr, "Ask Telegram to send a login code to this phone number.");
        sendPhone->Click += gcnew EventHandler(this, &SourcesPage::OnPhone);
        phoneRow_ = L::Row(phone_, sendPhone);
        L::AddField(auth, "Phone number", phoneRow_);
        code_ = L::Box(120);
        Button^ sendCode = L::Btn("Submit code", nullptr, "Send the login code you received in Telegram.");
        sendCode->Click += gcnew EventHandler(this, &SourcesPage::OnCode);
        codeRow_ = L::Row(code_, sendCode);
        L::AddField(auth, "Login code", codeRow_);
        password_ = L::Box(200);
        password_->UseSystemPasswordChar = true;
        Button^ sendPwd = L::Btn("Submit password", nullptr, "Send your Telegram two-step verification password, if you have one.");
        sendPwd->Click += gcnew EventHandler(this, &SourcesPage::OnPassword);
        passwordRow_ = L::Row(password_, sendPwd);
        L::AddField(auth, "Two-step verification", passwordRow_);
        logout_ = L::Btn("Log out", nullptr, "Sign out of Telegram in Pipkite. Your chats and sources are kept.");
        logout_->Click += gcnew EventHandler(this, &SourcesPage::OnLogout);
        Button^ del = L::Btn("Delete local Telegram data", "danger", "Remove the local Telegram session and cache. You will sign in again next time. Trade history is not touched.");
        del->Click += gcnew EventHandler(this, &SourcesPage::OnDeleteSession);
        auth->Controls->Add(gcnew Label());
        auth->Controls->Add(L::Row(logout_, del));
        authHint_ = L::Wrap("", "muted", 760);
        auth->Controls->Add(gcnew Label());
        auth->Controls->Add(authHint_);
        stack->Controls->Add(L::Group("Telegram account", auth));

        // chats: tick to copy (added as a source immediately; untick to stop)
        FlowLayoutPanel^ chatsPanel = gcnew FlowLayoutPanel();
        chatsPanel->FlowDirection = FlowDirection::TopDown;
        chatsPanel->AutoSize = true;
        search_ = L::Box(300);
        search_->TextChanged += gcnew EventHandler(this, &SourcesPage::OnSearch);
        Button^ refresh = L::Btn("Refresh chats", nullptr, "Reload the list of Telegram chats your account can read.");
        refresh->Click += gcnew EventHandler(this, &SourcesPage::OnRefreshChats);
        chatsPanel->Controls->Add(L::Row(L::Text("Search"), search_, refresh));
        chats_ = gcnew BufferedGrid();
        chats_->ReadOnly = false;
        chats_->AllowUserToAddRows = false;
        chats_->Width = 900;
        chats_->Height = 320;
        chats_->AddCheckCol("pick", "Copy", 60);
        chats_->AddCol("title", "Chat", 500)->ReadOnly = true;
        chats_->AddCol("type", "Type", 160)->ReadOnly = true;
        DataGridViewTextBoxColumn^ chatIdCol = chats_->AddCol("chatId", "Chat ID", 170);
        chatIdCol->ReadOnly = true;
        chatIdCol->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        chats_->AccessibleName = "Telegram chats";
        chats_->CurrentCellDirtyStateChanged += gcnew EventHandler(this, &SourcesPage::OnChatDirty);  // commit the tick immediately
        chats_->CellValueChanged += gcnew DataGridViewCellEventHandler(this, &SourcesPage::OnChatPickChanged);
        chatsPanel->Controls->Add(chats_);
        saveState_ = L::Wrap("", "muted", 900);
        chatsPanel->Controls->Add(saveState_);
        check_ = gcnew ChannelCheck(900);  // what Pipkite reads in the chat just ticked
        check_->Changed += gcnew EventHandler(this, &SourcesPage::OnCheckChanged);
        chatsPanel->Controls->Add(check_);
        stack->Controls->Add(L::Group("Chats to copy", chatsPanel));
        Controls->Add(stack);
        Header("Telegram sources", "Sign in and tick the chats you want to copy.", this);
    }

    virtual void OnActivated() override {
        if (Client == nullptr || !Client->Connected) return;
        Client->Call("sources.list", nullptr, gcnew ResponseHandler(this, &SourcesPage::OnSources));  // current sources -> tick state
        Client->Call("telegram.chats", nullptr, gcnew ResponseHandler(this, &SourcesPage::OnChats));
        Client->Call("settings.get", nullptr, gcnew ResponseHandler(this, &SourcesPage::OnSourceSettings));  // prefill saved API ID
    }

    void OnSourceSettings(bool ok, Object^ result, String^) {
        if (!ok) return;
        JObj^ s = J::Obj(result);
        String^ apiId = J::Str(s, "telegram.apiId");
        if (apiId != "" && apiId != "0") apiId_->Text = apiId;
        apiHash_->Text = J::Str(s, "telegram.apiHash");  // what the user entered, so they can see it again
        if (phone_ != nullptr) phone_->Text = J::Str(s, "telegram.phone");
    }

    virtual void OnSnapshot(JObj^ s) override {
        PageBase::OnSnapshot(s);
        if (!Visible) return;
        JObj^ tg = J::Obj(s, "telegram");
        String^ st = J::Str(tg, "state");
        String^ stText = st == "Ready" ? "Signed in as " + J::Str(tg, "user")
                         : st == "LibraryMissing" ? "TDLib not installed"
                         : st == "NotConfigured" ? "Not set up"
                         : st == "WaitPhone" ? "Enter phone number"
                         : st == "WaitCode" ? "Enter login code"
                         : st == "WaitPassword" ? "Enter two-step password"
                         : st;
        authState_->Set(stText, st == "Ready" ? Theme::Success : (st == "LibraryMissing" || st == "Error") ? Theme::Danger : Theme::Warning);
        String^ hint;
        if (st == "LibraryMissing") hint = "Telegram support (tdjson.dll) is missing from the Pipkite folder. Reinstall Pipkite to restore it.";
        else if (st == "NotConfigured") hint = "Enter your API ID and API hash, then your phone number.";
        else if (st == "WaitPhone") hint = "Enter your phone number in international format (+...).";
        else if (st == "WaitCode") hint = "Enter the login code Telegram sent to your app or SMS.";
        else if (st == "WaitPassword") hint = "Your account has two-step verification: enter your cloud password.";
        else hint = J::Str(tg, "error");
        if (authHint_->Text != hint) authHint_->Text = hint;
        // only the sign-in step Telegram is waiting for is shown
        const bool ready = st == "Ready";
        L::ShowField(apiId_, !ready);
        L::ShowField(apiHash_, !ready);
        L::ShowField(saveCred_, !ready);
        L::ShowField(phoneRow_, !ready && st != "WaitCode" && st != "WaitPassword" && st != "LibraryMissing");
        L::ShowField(codeRow_, st == "WaitCode");
        L::ShowField(passwordRow_, st == "WaitPassword");
        logout_->Visible = ready;
        if (st == "Ready" && !chatsLoaded_) {
            chatsLoaded_ = true;
            Client->Call("telegram.chats", nullptr, gcnew ResponseHandler(this, &SourcesPage::OnChats));
        }
    }

private:
    Badge^ authState_;
    TextBox ^ apiId_, ^apiHash_, ^phone_, ^code_, ^password_, ^search_;
    FlowLayoutPanel ^ phoneRow_, ^codeRow_, ^passwordRow_;
    Button ^ saveCred_, ^logout_;
    Label^ authHint_;
    Label^ saveState_;
    BufferedGrid^ chats_;
    ChannelCheck^ check_;
    String^ checkChat_;  // the ticked chat shown in check_
    array<Object^>^ allChats_;
    System::Collections::Generic::List<JObj^>^ sources_;
    String^ pendingNote_;
    bool chatsLoaded_, loadingChats_;

    JObj^ Params(String^ k, Object^ v) {
        JObj^ p = J::New();
        p[k] = v;
        return p;
    }

    void OnSaveCredentials(Object^, EventArgs^) {
        JObj^ p = J::New();
        p["apiId"] = apiId_->Text->Trim();
        p["apiHash"] = apiHash_->Text->Trim();
        Client->Call("telegram.setCredentials", p, SimpleHandler());
        apiHash_->Clear();
    }
    void OnPhone(Object^, EventArgs^) { Client->Call("telegram.phone", Params("phone", phone_->Text->Trim()), SimpleHandler()); }
    void OnCode(Object^, EventArgs^) {
        Client->Call("telegram.code", Params("code", code_->Text->Trim()), SimpleHandler());
        code_->Clear();  // login codes are never kept
    }
    void OnPassword(Object^, EventArgs^) {
        Client->Call("telegram.password", Params("password", password_->Text), SimpleHandler());
        password_->Clear();
    }
    void OnLogout(Object^, EventArgs^) {
        if (MessageBox::Show(FindForm(), "Log out of Telegram? Trade history is kept.", "Pipkite", MessageBoxButtons::YesNo) == DialogResult::Yes)
            Client->Call("telegram.logout", nullptr, SimpleHandler());
    }
    void OnDeleteSession(Object^, EventArgs^) {
        if (MessageBox::Show(FindForm(), "Delete the local Telegram session data? You will need to sign in again. Trade history is not affected.", "Pipkite",
                             MessageBoxButtons::YesNo, MessageBoxIcon::Warning) == DialogResult::Yes)
            Client->Call("telegram.deleteSession", nullptr, SimpleHandler());
    }

    void OnSources(bool ok, Object^ result, String^ error) {
        if (!ok) {
            saveState_->ForeColor = Theme::Danger;
            saveState_->Text = "Could not load your channels: " + error;
            return;
        }
        sources_ = gcnew System::Collections::Generic::List<JObj^>();
        for each (Object ^ o in J::Arr(result)) sources_->Add(J::Obj(o));
        FillChats();  // reflect which chats are already sources
        JObj^ shown = nullptr;
        for each (JObj ^ b in sources_)
            if (J::Str(b, "chatId") == checkChat_ && J::Bool(b, "enabled", true)) shown = b;
        if (shown != nullptr) check_->ShowChannel(Client, shown);
        else check_->Visible = false;
    }
    void OnCheckChanged(Object^, EventArgs^) { Client->Call("sources.list", nullptr, gcnew ResponseHandler(this, &SourcesPage::OnSources)); }

    void OnChats(bool ok, Object^ result, String^ error) {
        if (!ok) return;
        allChats_ = J::Arr(result);
        FillChats();
    }

    void FillChats() {
        if (allChats_ == nullptr) return;
        String^ q = search_->Text->Trim()->ToLowerInvariant();
        loadingChats_ = true;  // suppress the auto-add handler while we rebuild rows
        // a tick reloads the same rows: they keep their scroll position and the row the user was on
        const int count = chats_->Rows->Count, top = chats_->FirstDisplayedScrollingRowIndex;
        const int row = chats_->CurrentCell != nullptr ? chats_->CurrentCell->RowIndex : -1;
        const int col = chats_->CurrentCell != nullptr ? chats_->CurrentCell->ColumnIndex : 0;
        chats_->SuspendLayout();
        chats_->Rows->Clear();
        for each (Object ^ o in allChats_) {
            JObj^ c = J::Obj(o);
            String^ title = J::Str(c, "title");
            if (q != "" && !title->ToLowerInvariant()->Contains(q)) continue;
            String^ type = J::Str(c, "type");
            bool picked = HasSource(J::Str(c, "chatId")) || J::Bool(c, "selected");
            int idx = chats_->Rows->Add(gcnew array<Object^>{picked, title,
                                                             type == "channel" ? "Channel (broadcast)" : type == "supergroup" ? "Group (supergroup)" : "Group", J::Str(c, "chatId")});
            chats_->Rows[idx]->Tag = c;
        }
        chats_->EmptyText = allChats_->Length == 0 ? "No chats yet. Sign in to Telegram above, then refresh." : "No chats match the search.";
        if (chats_->Rows->Count == count && row >= 0 && row < count) {
            chats_->CurrentCell = chats_->Rows[row]->Cells[col];
            if (top >= 0) chats_->FirstDisplayedScrollingRowIndex = top;
        }
        chats_->ResumeLayout();
        chats_->Invalidate();
        loadingChats_ = false;
    }

    void OnSearch(Object^, EventArgs^) { FillChats(); }
    void OnRefreshChats(Object^, EventArgs^) { Client->Call("telegram.chats", nullptr, gcnew ResponseHandler(this, &SourcesPage::OnChats)); }

    // copied = a saved channel that isn't paused
    bool HasSource(String^ chatId) {
        if (sources_ == nullptr) return false;
        for each (JObj ^ b in sources_) if (J::Str(b, "chatId") == chatId) return J::Bool(b, "enabled", true);
        return false;
    }

    // Commit the checkbox edit as soon as it's toggled so CellValueChanged fires immediately.
    void OnChatDirty(Object^, EventArgs^) {
        if (chats_->IsCurrentCellDirty && chats_->CurrentCell != nullptr && chats_->CurrentCell->OwningColumn->Name == "pick")
            chats_->CommitEdit(DataGridViewDataErrorContexts::Commit);
    }
    // Ticking a chat adds it (a new one copies after Start copying, one ticked before copies again as it did);
    // unticking pauses it and keeps its settings (format, OK to copy, symbol, entry, sizing).
    // Only that one chat is sent to the engine, so nothing else can be overwritten.
    void OnChatPickChanged(Object^, DataGridViewCellEventArgs^ e) {
        if (loadingChats_ || e->RowIndex < 0 || chats_->Columns[e->ColumnIndex]->Name != "pick") return;
        JObj^ c = dynamic_cast<JObj^>(chats_->Rows[e->RowIndex]->Tag);
        if (c == nullptr) return;
        const bool on = Convert::ToBoolean(chats_->Rows[e->RowIndex]->Cells["pick"]->Value);
        if (sources_ == nullptr) {  // the channel list isn't loaded yet: don't guess, undo the tick
            loadingChats_ = true;
            chats_->Rows[e->RowIndex]->Cells["pick"]->Value = !on;
            loadingChats_ = false;
            saveState_->ForeColor = Theme::Warning;
            saveState_->Text = "Still loading your channels - tick it again in a moment.";
            return;
        }
        JObj^ p = J::New();
        p["chatId"] = J::Str(c, "chatId");
        if (on) {
            p["title"] = J::Str(c, "title");
            p["chatType"] = J::Str(c, "type");
        } else {
            p["enabled"] = false;
        }
        if (on) checkChat_ = J::Str(c, "chatId");
        pendingNote_ = on ? "Added " + J::Str(c, "title") + ". Below: what Pipkite reads in it."
                          : "Paused " + J::Str(c, "title") + " (its settings are kept; tick it to copy again)";
        Client->Call(on ? "sources.add" : "source.update", p, gcnew ResponseHandler(this, &SourcesPage::OnSaved));
    }

    void OnSaved(bool ok, Object^, String^ error) {
        if (!ok) {
            saveState_->ForeColor = Theme::Danger;
            saveState_->Text = "Could not save: " + error;
        } else {
            saveState_->ForeColor = Theme::Success;
            saveState_->Text = pendingNote_;
        }
        Client->Call("sources.list", nullptr, gcnew ResponseHandler(this, &SourcesPage::OnSources));  // ticks show what was saved
    }
};

}  // namespace PipkiteUI
