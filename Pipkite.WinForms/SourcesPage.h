#pragma once
// Telegram sources: user-session authentication, searchable chats (groups and channels clearly
// distinguished), the source allowlist, per-source parser/policy bindings, bulk apply and overrides.
// Source bindings are keyed by chat ID; renaming a chat never changes its binding.

#include "Pages.h"

namespace PipkiteUI {

public ref class OverridesDialog : public Form {
public:
    JObj^ Binding;
    OverridesDialog(JObj^ binding) {
        Binding = binding;
        Text = "Source overrides - " + J::Str(binding, "title");
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        FormBorderStyle = Windows::Forms::FormBorderStyle::FixedDialog;
        StartPosition = FormStartPosition::CenterParent;
        MaximizeBox = false;
        MinimizeBox = false;
        AutoSize = true;
        AutoSizeMode = Windows::Forms::AutoSizeMode::GrowAndShrink;
        Font = Theme::Body;
        Padding = Windows::Forms::Padding(16);
        JObj^ sizing = J::Obj(binding, "sizing");
        JObj^ targets = J::Obj(binding, "targets");
        TableLayoutPanel^ t = L::Form2();
        overrideSizing_ = L::Check("Override global sizing for this source");
        overrideSizing_->Checked = J::Bool(binding, "overrideSizing");
        t->Controls->Add(overrideSizing_);
        t->SetColumnSpan(overrideSizing_, 2);
        sizingMode_ = L::Combo(200, gcnew array<String^>{"fixed_total_lots", "risk_percent"});
        sizingMode_->SelectedItem = J::Str(sizing, "mode", "fixed_total_lots");
        L::AddField(t, "Sizing mode", sizingMode_);
        lots_ = L::Num(0.01, 100, J::Num(sizing, "lots", 0.01), 2, 0.01);
        L::AddField(t, "Total lots", lots_);
        risk_ = L::Num(0.01, 10, J::Num(sizing, "riskPercent", 0.5), 2, 0.05);
        L::AddField(t, "Risk % of equity at SL", risk_);
        overrideTargets_ = L::Check("Override global take-profit policy for this source");
        overrideTargets_->Checked = J::Bool(binding, "overrideTargets");
        t->Controls->Add(overrideTargets_);
        t->SetColumnSpan(overrideTargets_, 2);
        targetMode_ = L::Combo(200, gcnew array<String^>{"single", "split"});
        targetMode_->SelectedItem = J::Str(targets, "mode", "single");
        L::AddField(t, "Target mode", targetMode_);
        index_ = L::Num(1, 20, J::Num(targets, "selectedIndex", 1), 0, 1);
        L::AddField(t, "Selected TP index", index_);
        allocations_ = L::Box(200);
        String^ al = "";
        for each (Object^ a in J::Arr(targets, "allocations")) al += (al == "" ? "" : ",") + J::Fmt(Convert::ToDouble(a));
        allocations_->Text = al;
        L::AddField(t, "Split allocations % (sum 100)", allocations_);
        fallback_ = L::Check("If min lot prevents splitting, use the single selected target");
        fallback_->Checked = J::Bool(targets, "singleTargetFallback");
        t->Controls->Add(fallback_);
        t->SetColumnSpan(fallback_, 2);
        Button^ ok = L::Btn("Apply", "accent");
        ok->Click += gcnew EventHandler(this, &OverridesDialog::OnOk);
        Button^ cancel = L::Btn("Cancel");
        cancel->DialogResult = Windows::Forms::DialogResult::Cancel;
        FlowLayoutPanel^ s = gcnew FlowLayoutPanel();
        s->FlowDirection = FlowDirection::TopDown;
        s->AutoSize = true;
        s->Controls->Add(t);
        s->Controls->Add(L::Wrap("Split orders are submitted sequentially; unsubmitted children stop after a failure. Netting accounts support single-position mode only.", "muted", 420));
        s->Controls->Add(L::Row(ok, cancel));
        Controls->Add(s);
        CancelButton = cancel;
        Theme::Apply(this);
        BackColor = Theme::Bg;
        HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
    }

private:
    CheckBox ^ overrideSizing_, ^overrideTargets_, ^fallback_;
    ComboBox ^ sizingMode_, ^targetMode_;
    NumericUpDown ^ lots_, ^risk_, ^index_;
    TextBox^ allocations_;

    void OnOk(Object^, EventArgs^) {
        List<Object^>^ allocs = J::List();
        double sum = 0;
        for each (String^ part in allocations_->Text->Split(gcnew array<wchar_t>{',', ';', ' '}, StringSplitOptions::RemoveEmptyEntries)) {
            double v;
            if (!J::TryNum(part, v) || v <= 0) {
                Prompt::Error(this, "Allocations must be positive numbers separated by commas");
                return;
            }
            allocs->Add(v);
            sum += v;
        }
        if (targetMode_->Text == "split" && (allocs->Count < 2 || Math::Abs(sum - 100) > 1e-6)) {
            Prompt::Error(this, "Split mode needs at least two allocations that sum to exactly 100%");
            return;
        }
        JObj^ sizing = J::New();
        sizing["mode"] = sizingMode_->Text;
        sizing["lots"] = Decimal::ToDouble(lots_->Value);
        sizing["riskPercent"] = Decimal::ToDouble(risk_->Value);
        JObj^ targets = J::New();
        targets["mode"] = targetMode_->Text;
        targets["selectedIndex"] = Decimal::ToInt32(index_->Value);
        targets["allocations"] = allocs;
        targets["singleTargetFallback"] = fallback_->Checked;
        Binding["overrideSizing"] = overrideSizing_->Checked;
        Binding["sizing"] = sizing;
        Binding["overrideTargets"] = overrideTargets_->Checked;
        Binding["targets"] = targets;
        DialogResult = Windows::Forms::DialogResult::OK;
    }
};

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
        Button^ saveCred = L::Btn("Save credentials", "accent", "Store your Telegram api_id and api_hash (from my.telegram.org). They are DPAPI-encrypted and never logged.");
        saveCred->Click += gcnew EventHandler(this, &SourcesPage::OnSaveCredentials);
        auth->Controls->Add(gcnew Label());
        auth->Controls->Add(saveCred);
        phone_ = L::Box(200);
        Button^ sendPhone = L::Btn("Send code", nullptr, "Ask Telegram to send a login code to this phone number.");
        sendPhone->Click += gcnew EventHandler(this, &SourcesPage::OnPhone);
        L::AddField(auth, "Phone number", L::Row(phone_, sendPhone));
        code_ = L::Box(120);
        Button^ sendCode = L::Btn("Submit code", nullptr, "Send the login code you received in Telegram.");
        sendCode->Click += gcnew EventHandler(this, &SourcesPage::OnCode);
        L::AddField(auth, "Login code", L::Row(code_, sendCode));
        password_ = L::Box(200);
        password_->UseSystemPasswordChar = true;
        Button^ sendPwd = L::Btn("Submit password", nullptr, "Send your Telegram two-step verification password, if you have one.");
        sendPwd->Click += gcnew EventHandler(this, &SourcesPage::OnPassword);
        L::AddField(auth, "Two-step verification", L::Row(password_, sendPwd));
        Button^ logout = L::Btn("Log out", nullptr, "Sign out of Telegram in Pipkite. Your chats and sources are kept.");
        logout->Click += gcnew EventHandler(this, &SourcesPage::OnLogout);
        Button^ del = L::Btn("Delete local Telegram data", "danger", "Remove the local Telegram session and cache. You will sign in again next time. Trade history is not touched.");
        del->Click += gcnew EventHandler(this, &SourcesPage::OnDeleteSession);
        auth->Controls->Add(gcnew Label());
        auth->Controls->Add(L::Row(logout, del));
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
        if (st == "LibraryMissing") hint = "Telegram support (tdjson.dll) is missing from the Pipkite folder. Reinstall Pipkite to restore it. You can still use the simulated demo source and the parser sandbox.";
        else if (st == "NotConfigured") hint = "Enter your API ID and API hash, then your phone number.";
        else if (st == "WaitPhone") hint = "Enter your phone number in international format (+...).";
        else if (st == "WaitCode") hint = "Enter the login code Telegram sent to your app or SMS.";
        else if (st == "WaitPassword") hint = "Your account has two-step verification: enter your cloud password.";
        else hint = J::Str(tg, "error");
        if (authHint_->Text != hint) authHint_->Text = hint;
        if (st == "Ready" && !chatsLoaded_) {
            chatsLoaded_ = true;
            Client->Call("telegram.chats", nullptr, gcnew ResponseHandler(this, &SourcesPage::OnChats));
        }
    }

private:
    Badge^ authState_;
    TextBox ^ apiId_, ^apiHash_, ^phone_, ^code_, ^password_, ^search_;
    Label^ authHint_;
    Label^ saveState_;
    BufferedGrid^ chats_;
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
        if (!ok) return;
        sources_ = gcnew System::Collections::Generic::List<JObj^>();
        for each (Object ^ o in J::Arr(result)) sources_->Add(J::Obj(o));
        FillChats();  // reflect which chats are already sources
    }

    void OnChats(bool ok, Object^ result, String^ error) {
        if (!ok) return;
        allChats_ = J::Arr(result);
        FillChats();
    }

    void FillChats() {
        if (allChats_ == nullptr) return;
        String^ q = search_->Text->Trim()->ToLowerInvariant();
        loadingChats_ = true;  // suppress the auto-add handler while we rebuild rows
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
        chats_->ResumeLayout();
        chats_->Invalidate();
        loadingChats_ = false;
    }

    void OnSearch(Object^, EventArgs^) { FillChats(); }
    void OnRefreshChats(Object^, EventArgs^) { Client->Call("telegram.chats", nullptr, gcnew ResponseHandler(this, &SourcesPage::OnChats)); }

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
        b["template"] = "";  // no parser yet; delivered per channel from the backend
        b["entryMode"] = "wait_for_range";
        b["aiMode"] = "off";
        b["immediateTolerancePrice"] = 0.5;
        return b;
    }

    // Commit the checkbox edit as soon as it's toggled so CellValueChanged fires immediately.
    void OnChatDirty(Object^, EventArgs^) {
        if (chats_->IsCurrentCellDirty && chats_->CurrentCell != nullptr && chats_->CurrentCell->OwningColumn->Name == "pick")
            chats_->CommitEdit(DataGridViewDataErrorContexts::Commit);
    }
    // Ticking a chat adds it as a source immediately; unticking removes it. No separate Add button.
    void OnChatPickChanged(Object^, DataGridViewCellEventArgs^ e) {
        if (loadingChats_ || e->RowIndex < 0 || chats_->Columns[e->ColumnIndex]->Name != "pick") return;
        JObj^ c = dynamic_cast<JObj^>(chats_->Rows[e->RowIndex]->Tag);
        if (c == nullptr) return;
        if (sources_ == nullptr) sources_ = gcnew System::Collections::Generic::List<JObj^>();
        String^ chatId = J::Str(c, "chatId");
        bool on = Convert::ToBoolean(chats_->Rows[e->RowIndex]->Cells["pick"]->Value);
        if (on) {
            if (!HasSource(chatId)) sources_->Add(NewBinding(chatId, J::Str(c, "title"), J::Str(c, "type")));
        } else {
            for (int k = sources_->Count - 1; k >= 0; --k)
                if (J::Str(sources_[k], "chatId") == chatId) sources_->RemoveAt(k);
        }
        SaveSources(on ? "Now copying " + J::Str(c, "title") : "Stopped copying " + J::Str(c, "title"));
    }

    void SaveSources(String^ note) {
        pendingNote_ = note;
        array<Object^>^ list = gcnew array<Object^>(sources_->Count);
        for (int i = 0; i < sources_->Count; ++i) list[i] = sources_[i];
        Client->Call("sources.save", Params("sources", list), gcnew ResponseHandler(this, &SourcesPage::OnSaved));
    }

    void OnSaved(bool ok, Object^ result, String^ error) {
        if (!ok) {
            saveState_->ForeColor = Theme::Danger;
            saveState_->Text = "Could not save: " + error;
            return;
        }
        saveState_->ForeColor = Theme::Success;
        saveState_->Text = pendingNote_ + "  (" + sources_->Count + " source" + (sources_->Count == 1 ? "" : "s") + ")";
    }
};

}  // namespace PipkiteUI
