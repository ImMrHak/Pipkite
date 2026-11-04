#pragma once
// Settings: theme, startup/tray, notifications, retention, local SQLite export/import/backup, updater,
// AI storage, future licensing (disabled), diagnostics and connection reset.

#include "Dialogs.h"
#include "DashboardPage.h"
#include "Pages.h"

using namespace Microsoft::Win32;

namespace PipkiteUI {

public delegate void ThemeRequest(String^ theme);

public ref class SettingsPage : public PageBase {
public:
    static ThemeRequest^ ThemeChangeRequested;
    static MethodInvoker^ ExitRequested;

    SettingsPage() {
        FlowLayoutPanel^ stack = L::Stack();

        TableLayoutPanel^ ap = L::Form2();
        theme_ = L::Combo(160, gcnew array<String^>{"dark", "light", "system"});
        theme_->SelectedIndexChanged += gcnew EventHandler(this, &SettingsPage::OnTheme);
        L::AddField(ap, "Theme", theme_);
        tray_ = L::Check("Closing the window minimizes to the tray (use Exit to stop the engine)");
        L::AddField(ap, "", tray_);
        startup_ = L::Check("Start Pipkite when I sign in to Windows (opt-in)");
        L::AddField(ap, "", startup_);
        notify_ = L::Check("Show notifications for errors, reviews and updates");
        L::AddField(ap, "", notify_);
        keepAwake_ = L::Check("Keep Windows awake while copying automatically (sleep would suspend execution)");
        L::AddField(ap, "", keepAwake_);
        setupList_ = L::Check("Show the Get started list on the Dashboard until setup is complete");
        L::AddField(ap, "", setupList_);
        Button^ saveAp = L::Btn("Apply", "accent");
        saveAp->Click += gcnew EventHandler(this, &SettingsPage::OnSaveGeneral);
        ap->Controls->Add(gcnew Label());
        ap->Controls->Add(saveAp);
        stack->Controls->Add(L::Group("Appearance, startup and notifications", ap));

        TableLayoutPanel^ rt = L::Form2();
        rawDays_ = L::Num(1, 3650, 30, 0, 1);
        L::AddField(rt, "Raw message text retention (days)", rawDays_);
        metaDays_ = L::Num(1, 3650, 180, 0, 1);
        L::AddField(rt, "Execution metadata retention (days)", metaDays_);
        Button^ saveRt = L::Btn("Save retention");
        saveRt->Click += gcnew EventHandler(this, &SettingsPage::OnSaveRetention);
        Button^ purge = L::Btn("Apply retention now");
        purge->Click += gcnew EventHandler(this, &SettingsPage::OnPurge);
        rt->Controls->Add(gcnew Label());
        rt->Controls->Add(L::Row(saveRt, purge));
        stack->Controls->Add(L::Group("Retention", rt));

        FlowLayoutPanel^ dt = gcnew FlowLayoutPanel();
        dt->FlowDirection = FlowDirection::TopDown;
        dt->AutoSize = true;
        Button^ exportCfg = L::Btn("Export configuration and parsers...", nullptr, "Save your sources, rules and settings to a file (no secrets) to move to another PC or back up.");
        exportCfg->Click += gcnew EventHandler(this, &SettingsPage::OnExportConfig);
        Button^ backup = L::Btn("Back up now (full history)...", "accent", "Save a full snapshot of history and settings, optionally password-protected. Secrets are never exported.");
        backup->Click += gcnew EventHandler(this, &SettingsPage::OnBackup);
        Button^ import = L::Btn("Import data...", nullptr, "Restore configuration or history from an export or backup file.");
        import->Click += gcnew EventHandler(this, &SettingsPage::OnImport);
        Button^ erase = L::Btn("Erase local history...", "danger", "Permanently delete local signal and trade history from this PC. Cannot be undone.");
        erase->Click += gcnew EventHandler(this, &SettingsPage::OnErase);
        dt->Controls->Add(L::Row(exportCfg, backup, import, erase));
        dataDir_ = L::Text("", "muted");
        dt->Controls->Add(dataDir_);
        stack->Controls->Add(L::Group("Local data", dt));

        TableLayoutPanel^ up = L::Form2();
        channel_ = L::Combo(140, gcnew array<String^>{"stable", "beta"});
        L::AddField(up, "Channel", channel_);
        autoUpd_ = L::Check("Install compatible signed parser/bridge fixes automatically (never changes mode, risk, sources or account)");
        L::AddField(up, "", autoUpd_);
        manifestUrl_ = L::Box(460);
        L::AddField(up, "Release manifest URL", manifestUrl_);
        Button^ saveUp = L::Btn("Save");
        saveUp->Click += gcnew EventHandler(this, &SettingsPage::OnSaveUpdates);
        Button^ check = L::Btn("Check now");
        check->Click += gcnew EventHandler(this, &SettingsPage::OnCheckUpdates);
        Button^ history = L::Btn("Update history / What's changed...");
        history->Click += gcnew EventHandler(this, &SettingsPage::OnUpdateHistory);
        up->Controls->Add(gcnew Label());
        up->Controls->Add(L::Row(saveUp, check, history));
        updStatus_ = L::Wrap("", "muted", 640);
        up->Controls->Add(gcnew Label());
        up->Controls->Add(updStatus_);
        stack->Controls->Add(L::Group("Updates", up));

        TableLayoutPanel^ ai = L::Form2();
        aiStore_ = L::Check("Keep returned AI assessments in the local database");
        L::AddField(ai, "", aiStore_);
        licenseKey_ = L::Box(360);
        L::AddField(ai, "License key", licenseKey_);
        licensePortal_ = L::Box(360);
        L::AddField(ai, "Free license portal URL", licensePortal_);
        Button^ savePortal = L::Btn("Save URL", nullptr, "The website where users generate a free license key. Shown as a link in the welcome dialog.");
        savePortal->Click += gcnew EventHandler(this, &SettingsPage::OnSavePortal);
        ai->Controls->Add(gcnew Label());
        ai->Controls->Add(savePortal);
        Button^ validate = L::Btn("Validate & save", "accent", "Check the key against the Pipkite backend. Only a key the backend accepts is saved.");
        validate->Click += gcnew EventHandler(this, &SettingsPage::OnValidateLicense);
        Button^ clearKey = L::Btn("Remove key", nullptr, "Clear the saved license key. The app keeps working; backend features (AI review, custom parsers, updates) stop until a valid key is entered.");
        clearKey->Click += gcnew EventHandler(this, &SettingsPage::OnClearLicense);
        ai->Controls->Add(gcnew Label());
        ai->Controls->Add(L::Row(validate, clearKey));
        licenseStatus_ = L::Wrap("", "muted", 600);
        ai->Controls->Add(gcnew Label());
        ai->Controls->Add(licenseStatus_);
        stack->Controls->Add(L::Group("AI and licensing", ai));

        // signal formats: sharing between users of the same channel, and AI help for formats no one has yet
        TableLayoutPanel^ fm = L::Form2();
        lookup_ = L::Check("Show formats other Pipkite users made for the same channel");
        L::Tip(lookup_, "When you open Detect format, Pipkite sends the channel's ID to find formats others made for it. You still check each one on your own messages.");
        L::AddField(fm, "", lookup_);
        share_ = L::Check("Offer formats I teach to other users of the same channel");
        L::Tip(share_, "Sends the format, the channel's title and examples with links, @names, phone numbers and emails removed. Others still check it before using it.");
        L::AddField(fm, "", share_);
        aiProvider_ = L::Combo(320, gcnew array<String^>{"Pipkite's AI (free, uses your license key)", "My own AI key"});
        aiProvider_->DropDownStyle = ComboBoxStyle::DropDownList;
        L::AddField(fm, "AI help for formats", aiProvider_,
                    "Who answers when you press Ask AI in Detect format. The AI only suggests words to add to a format; you test the result on the channel's "
                    "messages before using it, and trading never waits on an AI.");
        aiPreset_ = L::Combo(320, gcnew array<String^>{"OpenAI", "DeepSeek", "Other (OpenAI-compatible)"});
        aiPreset_->DropDownStyle = ComboBoxStyle::DropDownList;
        aiPreset_->SelectedIndexChanged += gcnew EventHandler(this, &SettingsPage::OnAiPreset);
        L::AddField(fm, "Your AI provider", aiPreset_);
        aiUrl_ = L::Box(360);
        L::AddField(fm, "AI address", aiUrl_, "The provider's API address, e.g. https://api.openai.com/v1 or https://api.deepseek.com. http:// works only for a model server on this PC.");
        aiModel_ = L::Box(220);
        L::AddField(fm, "Model", aiModel_, "The model name your provider lists, e.g. deepseek-chat.");
        aiKey_ = L::Box(360);
        aiKey_->UseSystemPasswordChar = true;
        aiKey_->AccessibleName = "Your AI key";
        L::AddField(fm, "Your AI key", aiKey_, "Stored with Windows data protection on this PC. Sent only to the AI address above.");
        Button^ saveFm = L::Btn("Save", "accent", "Save these format and AI settings (and the key, if you typed one).");
        saveFm->Click += gcnew EventHandler(this, &SettingsPage::OnSaveFormats);
        Button^ clearAiKey = L::Btn("Remove my AI key", nullptr, "Delete your AI key from this PC.");
        clearAiKey->Click += gcnew EventHandler(this, &SettingsPage::OnClearAiKey);
        fm->Controls->Add(gcnew Label());
        fm->Controls->Add(L::Row(saveFm, clearAiKey));
        aiStatus_ = L::Wrap("", "muted", 600);
        fm->Controls->Add(gcnew Label());
        fm->Controls->Add(aiStatus_);
        stack->Controls->Add(L::Group("Signal formats and AI help", fm));

        FlowLayoutPanel^ dg = gcnew FlowLayoutPanel();
        dg->FlowDirection = FlowDirection::TopDown;
        dg->AutoSize = true;
        Button^ health = L::Btn("Run health check", nullptr, "Check disk space, journaling, terminal permission and connection status.");
        health->Click += gcnew EventHandler(this, &SettingsPage::OnHealth);
        Button^ folder = L::Btn("Open data folder", nullptr, "Open the folder holding your database, journals and settings in File Explorer.");
        folder->Click += gcnew EventHandler(this, &SettingsPage::OnOpenFolder);
        Button^ restart = L::Btn("Restart engine (orderly)", nullptr, "Stop and restart the background engine after finishing in-flight work. Copying pauses briefly.");
        restart->Click += gcnew EventHandler(this, &SettingsPage::OnRestartEngine);
        Button^ exit = L::Btn("Exit Pipkite", "danger", "Stop the engine and close the app. New signals are not copied until you start it again.");
        exit->Click += gcnew EventHandler(this, &SettingsPage::OnExit);
        dg->Controls->Add(L::Row(health, folder, restart, exit));
        healthText_ = L::Wrap("", nullptr, 900);
        healthText_->Font = Theme::Mono;
        dg->Controls->Add(healthText_);
        dg->Controls->Add(L::Text("Pipkite 1.0.2", "muted"));
        stack->Controls->Add(L::Group("Diagnostics and connection", dg));
        Controls->Add(stack);
        Header("Settings", "Appearance, startup, local data, updates, signal formats and AI help.", this);
    }

    virtual void OnActivated() override {
        if (Client == nullptr || !Client->Connected) return;
        loading_ = true;
        Client->Call("settings.get", nullptr, gcnew ResponseHandler(this, &SettingsPage::OnSettings));
        Client->Call("updates.status", nullptr, gcnew ResponseHandler(this, &SettingsPage::OnUpdateStatus));
    }

    virtual void OnSnapshot(JObj^ s) override {
        PageBase::OnSnapshot(s);
        if (!Visible) return;
        JObj^ u = J::Obj(s, "updates");
        updStatus_->Text = "Installed " + J::Str(u, "installedVersion") + " - update check: " + J::Str(u, "state") + (J::Str(u, "detail") != "" ? " (" + J::Str(u, "detail") + ")" : "");
    }

private:
    ComboBox ^ theme_, ^channel_;
    CheckBox ^ tray_, ^startup_, ^notify_, ^autoUpd_, ^aiStore_, ^keepAwake_, ^setupList_, ^lookup_, ^share_;
    ComboBox ^ aiProvider_, ^aiPreset_;
    TextBox ^ aiUrl_, ^aiModel_, ^aiKey_;
    Label^ aiStatus_;
    NumericUpDown ^ rawDays_, ^metaDays_;
    TextBox ^ manifestUrl_, ^licenseKey_, ^licensePortal_;
    Label ^ dataDir_, ^updStatus_, ^healthText_, ^licenseStatus_;
    bool loading_;
    String^ dataDirPath_;

    void OnSettings(bool ok, Object^ result, String^ error) {
        if (!ok) return;
        JObj^ s = J::Obj(result);
        theme_->SelectedItem = J::Str(s, "ui.theme", "dark");
        tray_->Checked = J::Str(s, "ui.minimizeToTray", "true") == "true";
        startup_->Checked = J::Str(s, "ui.startWithWindows", "false") == "true";
        notify_->Checked = J::Str(s, "ui.notifications", "true") == "true";
        keepAwake_->Checked = J::Str(s, "engine.keepAwake", "true") == "true";
        setupList_->Checked = J::Str(s, "ui.setupHidden", "false") != "true";
        rawDays_->Value = Decimal(Math::Max(1.0, Double::Parse(J::Str(s, "retention.rawTextDays", "30"))));
        metaDays_->Value = Decimal(Math::Max(1.0, Double::Parse(J::Str(s, "retention.metadataDays", "180"))));
        channel_->SelectedItem = J::Str(s, "updates.channel", "stable");
        autoUpd_->Checked = J::Str(s, "updates.autoCompatible", "true") == "true";
        manifestUrl_->Text = J::Str(s, "updates.manifestUrl");
        aiStore_->Checked = J::Str(s, "ai.storeLocally", "true") == "true";
        licenseKey_->Text = J::Str(s, "license.key");
        licensePortal_->Text = J::Str(s, "license.portalUrl");
        licenseStatus_->Text = licenseKey_->Text != "" ? "A license key is saved. Use Validate & save to re-check it." : "No license key. Backend features are off until you enter a valid key.";
        lookup_->Checked = J::Str(s, "formats.lookup", "true") != "false";
        share_->Checked = J::Str(s, "formats.share", "false") == "true";
        aiProvider_->SelectedIndex = J::Str(s, "ai.formatProvider", "pipkite") == "own" ? 1 : 0;
        aiUrl_->Text = J::Str(s, "ai.ownBaseUrl");
        aiModel_->Text = J::Str(s, "ai.ownModel");
        aiPreset_->SelectedIndex = aiUrl_->Text->Contains("deepseek") ? 1 : aiUrl_->Text == "" || aiUrl_->Text->Contains("api.openai.com") ? 0 : 2;
        if (aiUrl_->Text == "") OnAiPreset(nullptr, nullptr);
        aiKey_->Text = "";
        aiStatus_->Text = J::Bool(s, "ai.ownKeySet") ? "Your AI key is saved on this PC." : "No AI key of your own saved.";
        dataDirPath_ = J::Str(s, "dataDir");
        dataDir_->Text = "Database folder: " + dataDirPath_;
        loading_ = false;
    }

    void Save(JObj^ values) {
        JObj^ p = J::New();
        p["values"] = values;
        Client->Call("settings.save", p, SimpleHandler());
    }

    void OnTheme(Object^, EventArgs^) {
        if (loading_ || ThemeChangeRequested == nullptr) return;
        ThemeChangeRequested(theme_->Text);
    }

    void OnSaveGeneral(Object^, EventArgs^) {
        JObj^ v = J::New();
        v["ui.theme"] = theme_->Text;
        v["ui.minimizeToTray"] = tray_->Checked ? "true" : "false";
        v["ui.startWithWindows"] = startup_->Checked ? "true" : "false";
        v["ui.notifications"] = notify_->Checked ? "true" : "false";
        v["engine.keepAwake"] = keepAwake_->Checked ? "true" : "false";
        v["ui.setupHidden"] = setupList_->Checked ? "false" : "true";
        DashboardPage::SetupHidden = !setupList_->Checked;
        v["ai.storeLocally"] = aiStore_->Checked ? "true" : "false";
        Save(v);
        try {
            RegistryKey^ run = Registry::CurrentUser->OpenSubKey("Software\\Microsoft\\Windows\\CurrentVersion\\Run", true);
            String^ launcher = IO::Path::Combine(IO::Path::GetDirectoryName(Application::ExecutablePath), "Pipkite.Launcher.exe");
            if (!IO::File::Exists(launcher)) launcher = Application::ExecutablePath;
            if (startup_->Checked) run->SetValue("Pipkite", "\"" + launcher + "\" --minimized");
            else run->DeleteValue("Pipkite", false);
            run->Close();
        } catch (Exception^ ex) {
            ShowError("Could not update the startup entry: " + ex->Message);
        }
        if (ThemeChangeRequested != nullptr) ThemeChangeRequested(theme_->Text);
    }

    void OnAiPreset(Object^, EventArgs^) {
        if (loading_ && aiUrl_->Text != "") return;  // keep what was saved
        if (aiPreset_->SelectedIndex == 0) {
            aiUrl_->Text = "https://api.openai.com/v1";
            if (aiModel_->Text == "" || aiModel_->Text == "deepseek-chat") aiModel_->Text = "gpt-4.1-mini";
        } else if (aiPreset_->SelectedIndex == 1) {
            aiUrl_->Text = "https://api.deepseek.com";
            if (aiModel_->Text == "" || aiModel_->Text == "gpt-4.1-mini") aiModel_->Text = "deepseek-chat";
        }
    }

    void OnSaveFormats(Object^, EventArgs^) {
        String^ url = aiUrl_->Text->Trim();
        if (aiProvider_->SelectedIndex == 1 && !(url->StartsWith("https://") || url->StartsWith("http://localhost") || url->StartsWith("http://127.0.0.1"))) {
            aiStatus_->Text = "The AI address must start with https:// (http:// only for a model server on this PC).";
            return;
        }
        JObj^ v = J::New();
        v["formats.lookup"] = lookup_->Checked ? "true" : "false";
        v["formats.share"] = share_->Checked ? "true" : "false";
        v["ai.formatProvider"] = aiProvider_->SelectedIndex == 1 ? "own" : "pipkite";
        v["ai.ownBaseUrl"] = url;
        v["ai.ownModel"] = aiModel_->Text->Trim();
        JObj^ p = J::New();
        p["values"] = v;
        Client->Call("settings.save", p, gcnew ResponseHandler(this, &SettingsPage::OnFormatsSaved));
    }

    void OnFormatsSaved(bool ok, Object^, String^ error) {
        if (!ok) {
            aiStatus_->Text = "Not saved: " + error;
            return;
        }
        if (aiKey_->Text->Trim() == "") {
            aiStatus_->Text = "Saved.";
            return;
        }
        // the address is saved first, so the engine can check the key against it
        JObj^ p = J::New();
        p["key"] = aiKey_->Text->Trim();
        aiKey_->Text = "";
        aiStatus_->Text = "Saving and checking your key...";
        Client->Call("ai.setOwnKey", p, gcnew ResponseHandler(this, &SettingsPage::OnAiKeySaved));
    }

    void OnAiKeySaved(bool ok, Object^ result, String^ error) {
        aiStatus_->Text = ok ? J::Str(J::Obj(result), "message") : "Key not saved: " + error;
        aiStatus_->ForeColor = ok && J::Bool(J::Obj(result), "checked") ? Theme::Success : ok ? Theme::Warning : Theme::Danger;
    }

    void OnClearAiKey(Object^, EventArgs^) {
        Client->Call("ai.clearOwnKey", nullptr, gcnew ResponseHandler(this, &SettingsPage::OnAiKeyCleared));
    }

    void OnAiKeyCleared(bool ok, Object^, String^ error) {
        aiStatus_->ForeColor = Theme::TextMuted;
        aiStatus_->Text = ok ? "Your AI key was removed from this PC." : "Couldn't remove the key: " + error;
    }

    void OnSaveRetention(Object^, EventArgs^) {
        JObj^ v = J::New();
        v["retention.rawTextDays"] = Decimal::ToInt32(rawDays_->Value).ToString();
        v["retention.metadataDays"] = Decimal::ToInt32(metaDays_->Value).ToString();
        Save(v);
    }

    void OnValidateLicense(Object^, EventArgs^) {
        String^ key = licenseKey_->Text->Trim();
        if (key == "") return ShowError("Enter a license key first.");
        licenseStatus_->ForeColor = Theme::TextMuted;
        licenseStatus_->Text = "Validating with the backend...";
        JObj^ p = J::New();
        p["key"] = key;
        Client->Call("license.validate", p, gcnew ResponseHandler(this, &SettingsPage::OnLicenseValidated));
    }

    void OnLicenseValidated(bool ok, Object^ result, String^ error) {
        if (!ok) {
            licenseStatus_->Text = "";
            return ShowError(error);
        }
        JObj^ r = J::Obj(result);
        if (!J::Bool(r, "valid")) {
            licenseStatus_->ForeColor = Theme::Danger;
            licenseStatus_->Text = "Key rejected: " + J::Str(r, "error", "not accepted by the backend") + ". It was not saved.";
            return;
        }
        // Only persist a key the backend accepted.
        JObj^ v = J::New();
        v["license.key"] = licenseKey_->Text->Trim();
        Save(v);
        JObj^ info = J::Obj(r, "info");
        String^ who = info != nullptr ? J::Str(info, "customerName") : "";
        String^ product = info != nullptr ? J::Str(info, "product") : "";
        String^ expires = info != nullptr ? J::Str(info, "expiresAt") : "";
        String^ detail = "License valid and saved.";
        if (product != "") detail += " Product: " + product + ".";
        if (who != "") detail += " Licensed to: " + who + ".";
        if (expires != "") detail += " Expires: " + expires + ".";
        detail += " Backend features are now enabled.";
        licenseStatus_->ForeColor = Theme::Success;  // valid -> green
        licenseStatus_->Text = detail;
    }

    void OnSavePortal(Object^, EventArgs^) {
        JObj^ v = J::New();
        v["license.portalUrl"] = licensePortal_->Text->Trim();
        Save(v);
        licenseStatus_->Text = "License portal URL saved. It appears as the free-key link in the welcome dialog.";
    }

    void OnClearLicense(Object^, EventArgs^) {
        licenseKey_->Text = "";
        JObj^ v = J::New();
        v["license.key"] = "";
        Save(v);
        licenseStatus_->ForeColor = Theme::TextMuted;
        licenseStatus_->Text = "License key removed. Backend features are off until a valid key is entered.";
    }

    void OnPurge(Object^, EventArgs^) { Client->Call("data.purge", nullptr, gcnew ResponseHandler(this, &SettingsPage::OnPurged)); }
    void OnPurged(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        ShowInfo("Retention", J::Stringify(result));
    }

    void Backup(String^ scope) {
        SaveFileDialog^ d = gcnew SaveFileDialog();
        d->Filter = "Pipkite backup (*.srbackup)|*.srbackup";
        d->FileName = "Pipkite-" + DateTime::Now.ToString("yyyyMMdd-HHmm") + (scope == "configuration" ? "-config" : "") + ".srbackup";
        if (d->ShowDialog(FindForm()) != DialogResult::OK) return;
        String^ pwd = Prompt::Ask(FindForm(), "Protect backup", "Optional password (8+ characters) for a portable encrypted backup. Leave empty for none:", "", true);
        if (pwd == nullptr) return;
        if (pwd != "" && pwd->Length < 8) return ShowError("The password must be at least 8 characters");
        JObj^ p = J::New();
        p["path"] = d->FileName;
        p["password"] = pwd;
        p["scope"] = scope;
        Client->Call("data.backup", p, gcnew ResponseHandler(this, &SettingsPage::OnBackedUp));
    }
    void OnExportConfig(Object^, EventArgs^) { Backup("configuration"); }
    void OnBackup(Object^, EventArgs^) { Backup("full"); }
    void OnBackedUp(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        ShowInfo("Backup", "Backup written (" + (J::Num(J::Obj(result), "bytes") / 1024).ToString("N0") + " KB).");
    }

    void OnImport(Object^, EventArgs^) {
        OpenFileDialog^ d = gcnew OpenFileDialog();
        d->Filter = "Pipkite backup (*.srbackup)|*.srbackup";
        if (d->ShowDialog(FindForm()) != DialogResult::OK) return;
        String^ pwd = Prompt::Ask(FindForm(), "Import", "Password (leave empty if the backup is not protected):", "", true);
        if (pwd == nullptr) return;
        JObj^ p = J::New();
        p["path"] = d->FileName;
        p["password"] = pwd;
        Client->Call("data.importPreview", p, gcnew ResponseHandler(this, &SettingsPage::OnImportPreview));
    }

    void OnImportPreview(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError("Import rejected: " + error);
        JObj^ r = J::Obj(result);
        JObj^ m = J::Obj(r, "manifest");
        JObj^ c = J::Obj(r, "counts");
        String^ text = "Backup from Pipkite " + J::Str(m, "appVersion") + " (schema " + J::Str(m, "schemaVersion") + "), created " + J::Time(J::Int(m, "createdAtMs")) +
                       "\nScope: " + J::Str(m, "scope") + "   Integrity: " + J::Str(r, "integrity", "n/a") + "\nSignals: " + J::Str(c, "signal_revisions", "-") +
                       "   Deals: " + J::Str(c, "broker_deals", "-") + "   Parsers: " + J::Str(c, "parser_versions", "-") +
                       "\n\nYES = merge configuration and parsers (existing local versions are never overwritten)\nNO = replace the whole local profile on next start "
                       "(a rollback copy is kept; refused while unresolved commands exist)\nCANCEL = do nothing\n\nCopying is paused during import; imported history never places orders.";
        DialogResult d = MessageBox::Show(FindForm(), text, "Import preview", MessageBoxButtons::YesNoCancel, MessageBoxIcon::Question);
        if (d == DialogResult::Cancel) return;
        JObj^ p = J::New();
        p["mode"] = d == DialogResult::Yes ? "config" : "full";
        Client->Call("data.importApply", p, gcnew ResponseHandler(this, &SettingsPage::OnImported));
    }

    void OnImported(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        ShowInfo("Import", J::Stringify(result));
    }

    void OnErase(Object^, EventArgs^) {
        String^ typed = Prompt::Ask(FindForm(), "Erase local history", "This deletes signals, messages, orders, deals and profit snapshots from this PC. Type ERASE to confirm:", "", false);
        if (typed != "ERASE") return;
        Client->Call("data.erase", nullptr, SimpleHandler());
    }

    void OnSaveUpdates(Object^, EventArgs^) {
        JObj^ v = J::New();
        v["updates.channel"] = channel_->Text;
        v["updates.autoCompatible"] = autoUpd_->Checked ? "true" : "false";
        v["updates.manifestUrl"] = manifestUrl_->Text->Trim();
        Save(v);
    }
    void OnCheckUpdates(Object^, EventArgs^) { Client->Call("updates.check", nullptr, SimpleHandler()); }
    void OnUpdateStatus(bool ok, Object^ result, String^) {}
    void OnUpdateHistory(Object^, EventArgs^) {
        JObj^ p = J::New();
        p["limit"] = 20;
        Client->Call("updates.history", p, gcnew ResponseHandler(this, &SettingsPage::OnHistory));
    }
    void OnHistory(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        WhatsChangedForm^ f = gcnew WhatsChangedForm(J::Arr(result), Client, true);
        f->ShowDialog(FindForm());
    }

    void OnHealth(Object^, EventArgs^) { Client->Call("diagnostics.health", nullptr, gcnew ResponseHandler(this, &SettingsPage::OnHealthResult)); }
    void OnHealthResult(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        JObj^ r = J::Obj(result);
        JObj^ h = J::Obj(r, "health");
        healthText_->Text = "Database integrity : " + J::Str(r, "integrity") + "\nJournal healthy    : " + J::Str(h, "journalHealthy") + " " + J::Str(h, "journalError") +
                            "\nDisk free          : " + J::Str(h, "diskFreeMb") + " MB\nTrusted keys       : " + J::Str(r, "trustedKeys") + "\nTelegram           : " +
                            J::Str(J::Obj(r, "telegram"), "state") + "\nBridge             : " + J::Str(J::Obj(r, "bridge"), "status") + "\nTerminal trading   : " +
                            J::Str(h, "terminalTradeAllowed") + "\nDispatch blocked   : " + J::Str(r, "dispatchBlockedReason", "no") + "\nData folder        : " + J::Str(r, "dataDir");
    }

    void OnOpenFolder(Object^, EventArgs^) {
        if (!String::IsNullOrEmpty(dataDirPath_)) Diagnostics::Process::Start("explorer.exe", "\"" + dataDirPath_ + "\"");
    }

    void OnRestartEngine(Object^, EventArgs^) {
        if (MessageBox::Show(FindForm(), "Restart the engine? It stops admissions, shuts down in order and starts again paused until reconciliation completes.", "Pipkite",
                             MessageBoxButtons::YesNo) == DialogResult::Yes)
            Client->Call("engine.shutdown", nullptr, SimpleHandler());  // the client relaunches it automatically
    }

    void OnExit(Object^, EventArgs^) {
        if (ExitRequested != nullptr) ExitRequested();
    }
};

}  // namespace PipkiteUI
