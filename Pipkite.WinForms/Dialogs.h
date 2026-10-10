#pragma once
// Signal detail (original message with highlighted evidence, transformations, policy/AI decisions and
// the processing timeline) and the "What's changed" view built only from signed release metadata.

#include "Controls.h"
#include "EngineClient.h"

using namespace System::Text;

namespace PipkiteUI {

public ref class SignalDetailForm : public Form {
public:
    SignalDetailForm(EngineClient^ client, String^ chatId, String^ messageId, int revision) {
        Text = "Signal detail";
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        StartPosition = FormStartPosition::CenterParent;
        Size = Drawing::Size(1100, 760);
        MinimumSize = Drawing::Size(800, 520);
        Font = Theme::Body;
        KeyPreview = true;
        FlowLayoutPanel^ s = L::Stack();
        header_ = L::Text("Loading...", "h2");
        s->Controls->Add(header_);
        flags_ = L::Text("", "muted");
        s->Controls->Add(flags_);
        s->Controls->Add(L::Text("Original message (evidence highlighted)", "h2"));
        text_ = gcnew RichTextBox();
        text_->ReadOnly = true;
        text_->Width = 1020;
        text_->Height = 150;
        text_->Font = Theme::Mono;
        text_->AccessibleName = "Original message";
        s->Controls->Add(text_);
        s->Controls->Add(L::Text("Interpretation and decision", "h2"));
        interp_ = L::Wrap("", nullptr, 1020);
        s->Controls->Add(interp_);
        s->Controls->Add(L::Text("Processing timeline", "h2"));
        timeline_ = gcnew BufferedGrid();
        timeline_->Width = 1020;
        timeline_->Height = 190;
        timeline_->AddCol("stage", "Stage", 170);
        timeline_->AddCol("time", "Local time", 170);
        timeline_->AddCol("delta", "+ms since receipt", 130);
        DataGridViewTextBoxColumn^ d = timeline_->AddCol("detail", "Detail", 520);
        d->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        s->Controls->Add(timeline_);
        s->Controls->Add(L::Text("Commands and broker transactions", "h2"));
        commands_ = gcnew BufferedGrid();
        commands_->Width = 1020;
        commands_->Height = 170;
        commands_->AddCol("child", "Child", 50);
        commands_->AddCol("state", "State", 110);
        commands_->AddCol("action", "Action", 90);
        commands_->AddCol("volume", "Requested / filled", 130);
        commands_->AddCol("ticket", "Ticket", 100);
        DataGridViewTextBoxColumn^ ev = commands_->AddCol("events", "State history", 520);
        ev->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        s->Controls->Add(commands_);
        s->Controls->Add(L::Wrap("OrderSendAsync acceptance means submission, not a fill. Simulated terminals are labeled and their fills are not broker fills.", "muted", 1020));
        Controls->Add(s);
        Theme::Apply(this);
        BackColor = Theme::Bg;
        HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
        JObj^ p = J::New();
        p["chatId"] = chatId;
        p["messageId"] = messageId;
        p["revision"] = revision;
        client->Call("history.detail", p, gcnew ResponseHandler(this, &SignalDetailForm::OnDetail));
    }

protected:
    virtual void OnKeyDown(KeyEventArgs^ e) override {
        if (e->KeyCode == Keys::Escape) Close();
        Form::OnKeyDown(e);
    }

private:
    Label ^ header_, ^flags_, ^interp_;
    RichTextBox^ text_;
    BufferedGrid ^ timeline_, ^commands_;

    static int ByteToChar(String^ s, int byteOffset) {
        int bytes = 0;
        for (int i = 0; i < s->Length; ++i) {
            if (bytes >= byteOffset) return i;
            int w = Char::IsHighSurrogate(s[i]) && i + 1 < s->Length ? 2 : 1;
            bytes += Encoding::UTF8->GetByteCount(s->ToCharArray(i, w));
            i += w - 1;
        }
        return s->Length;
    }

    void OnDetail(bool ok, Object^ result, String^ error) {
        if (!ok) {
            header_->Text = "Could not load: " + error;
            return;
        }
        JObj^ r = J::Obj(result);
        JObj^ s = J::Obj(r, "signal");
        JObj^ d = J::Obj(r, "decision");
        header_->Text = J::Str(s, "action", "(no action)") + " " + J::Str(s, "symbol") + "  -  " + J::Str(r, "state") + "  -  parse " + J::Str(s, "status");
        header_->ForeColor = Theme::StateColor(J::Str(r, "state"));
        flags_->Text = "Published " + J::Time(J::Int(r, "publishedAtMs")) + "   received " + J::Time(J::Int(r, "receivedAtMs")) + "   parser " + J::Str(s, "parserId") + "@" +
                       J::Str(s, "parserVersion") + (J::Bool(r, "simulated") ? "   SIMULATED MESSAGE" : "") + (J::Bool(r, "historical") ? "   HISTORICAL (never executed)" : "");
        String^ raw = J::Str(r, "rawText")->Replace("\r\n", "\n");
        text_->Text = raw;
        for each (Object^ o in J::Arr(s, "evidence")) {
            JObj^ e = J::Obj(o);
            int b = ByteToChar(raw, (int)J::Int(e, "begin"));
            int en = ByteToChar(raw, (int)J::Int(e, "end"));
            if (en <= b) continue;
            text_->Select(b, en - b);
            String^ f = J::Str(e, "field");
            text_->SelectionBackColor = f == "sl" ? Color::FromArgb(130, 200, 70, 70) : f->StartsWith("tp") ? Color::FromArgb(130, 60, 170, 90) : f == "entry" ? Color::FromArgb(120, 60, 160, 170)
                                                                                                                                              : Color::FromArgb(110, 74, 144, 226);
        }
        text_->Select(0, 0);
        StringBuilder^ sb = gcnew StringBuilder();
        JObj^ entry = J::Obj(s, "entry");
        sb->Append("Entry: " + (entry == nullptr ? "-" : J::Fmt(J::Num(entry, "low")) + " - " + J::Fmt(J::Num(entry, "high"))) + "   SL: " + (J::Has(s, "stopLoss") ? J::Fmt(J::Num(s, "stopLoss")) : "-") + "   TPs: ");
        for each (Object^ t in J::Arr(s, "targets")) sb->Append(J::Fmt(J::Num(J::Obj(t), "price")) + " ");
        sb->AppendLine();
        for each (Object^ g in J::Arr(s, "diagnostics")) sb->AppendLine("Parser: " + Convert::ToString(g));
        sb->AppendLine("Decision: " + J::Str(d, "kind") + " - " + J::Str(d, "reason"));
        String^ checks = "";
        for each (Object^ c in J::Arr(d, "checks")) checks += Convert::ToString(c) + "; ";
        if (checks != "") sb->AppendLine("Checks passed: " + checks);
        JObj^ plan = J::Obj(d, "plan");
        if (plan != nullptr) {
            sb->Append("Plan: " + J::Str(plan, "action") + " " + J::Str(plan, "symbol") + " total " + J::Fmt(J::Num(plan, "totalVolume"), 2) + " lots (" + J::Str(plan, "sizingMode") +
                       "), SL " + J::Fmt(J::Num(plan, "stopLoss")) + ", planned risk " + J::Fmt(J::Num(plan, "plannedRiskMoney"), 2) + ", config v" + J::Str(plan, "configVersion") + ", children: ");
            for each (Object^ c in J::Arr(plan, "children")) {
                JObj^ ch = J::Obj(c);
                sb->Append(J::Fmt(J::Num(ch, "volume"), 2) + "@TP" + J::Str(ch, "tpIndex") + " ");
            }
            sb->AppendLine();
        }
        JObj^ ai = J::Obj(r, "ai");
        if (ai != nullptr) {
            sb->Append("AI (" + J::Str(ai, "provider") + " " + J::Str(ai, "modelVersion") + "): " + J::Str(ai, "verdict"));
            if (J::Has(ai, "score")) sb->Append(" score " + J::Fmt(J::Num(ai, "score"), 2) + (J::Bool(ai, "scoreCalibrated") ? " (calibrated)" : ""));
            for each (Object^ x in J::Arr(ai, "reasons")) sb->Append(" | " + Convert::ToString(x));
            if (J::Str(ai, "unavailableReason") != "") sb->Append(" | " + J::Str(ai, "unavailableReason"));
            sb->AppendLine();
            sb->AppendLine("AI assessments are estimates, not guaranteed predictions of profitable trades.");
        }
        interp_->Text = sb->ToString();
        timeline_->Rows->Clear();
        Int64 t0 = 0;
        for each (Object^ o in J::Arr(r, "timeline")) {
            JObj^ t = J::Obj(o);
            Int64 mono = J::Int(t, "monoUs");
            if (t0 == 0 && mono > 0) t0 = mono;
            timeline_->Rows->Add(gcnew array<Object^>{J::Str(t, "stage"), J::Time(J::Int(t, "atMs")), (mono > 0 && t0 > 0) ? J::Fmt((mono - t0) / 1000.0, 2) : "", J::Str(t, "detail")});
        }
        commands_->Rows->Clear();
        for each (Object^ o in J::Arr(r, "commands")) {
            JObj^ c = J::Obj(o);
            String^ hist = "";
            for each (Object^ e in J::Arr(c, "events")) {
                JObj^ ev = J::Obj(e);
                String^ reason = J::Str(J::Obj(ev, "detail"), "reason");
                hist += J::Str(ev, "to") + (reason != "" ? " (" + reason + ")" : "") + " > ";
            }
            commands_->Rows->Add(gcnew array<Object^>{(J::Int(c, "child") + 1).ToString(), J::Str(c, "state"), J::Str(c, "action"),
                                                       J::Fmt(J::Num(c, "volume"), 2) + " / " + J::Fmt(J::Num(c, "filledVolume"), 2), J::Str(c, "orderTicket"), hist});
        }
    }
};

public ref class WhatsChangedForm : public Form {
public:
    WhatsChangedForm(array<Object^>^ entries, EngineClient^ client, bool all) {
        entries_ = entries;
        client_ = client;
        Text = "What's changed";
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        StartPosition = FormStartPosition::CenterParent;
        Size = Drawing::Size(1000, 680);
        Font = Theme::Body;
        SplitContainer^ split = gcnew SplitContainer();
        split->Dock = DockStyle::Fill;
        split->Orientation = Orientation::Horizontal;
        split->SplitterDistance = 180;
        list_ = gcnew NeoListBox();
        list_->Dock = DockStyle::Fill;
        list_->AccessibleName = "Update history";
        for each (Object^ o in entries) {
            JObj^ e = J::Obj(o);
            list_->Items->Add(J::Time(J::Int(e, "at")) + "   " + J::Str(e, "component") + "   " + J::Str(e, "from") + " -> " + J::Str(e, "to") + "   [" + J::Str(e, "state") + "]" +
                              (J::Bool(e, "restartRequired") ? "   restart required" : ""));
        }
        if (entries->Length == 0) list_->Items->Add("No updates or parser activations recorded yet.");
        list_->SelectedIndexChanged += gcnew EventHandler(this, &WhatsChangedForm::OnSelect);
        split->Panel1->Controls->Add(list_);
        FlowLayoutPanel^ s = L::Stack();
        summary_ = L::Wrap("", nullptr, 920);
        s->Controls->Add(summary_);
        changes_ = gcnew BufferedGrid();
        changes_->Width = 920;
        changes_->Height = 170;
        changes_->AddCol("kind", "Type", 90);
        changes_->AddCol("fix", "Fix / feature", 220);
        changes_->AddCol("before", "Before", 200);
        changes_->AddCol("after", "After", 200);
        DataGridViewTextBoxColumn^ im = changes_->AddCol("impact", "Impact", 200);
        im->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        s->Controls->Add(changes_);
        s->Controls->Add(L::Text("Actual parser output on saved samples (before -> after)", "h2"));
        diffs_ = gcnew BufferedGrid();
        diffs_->Width = 920;
        diffs_->Height = 170;
        diffs_->AddCol("sample", "Sample", 260);
        diffs_->AddCol("changed", "Changed fields", 150);
        diffs_->AddCol("before", "Before", 250);
        DataGridViewTextBoxColumn^ af = diffs_->AddCol("after", "After", 250);
        af->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        s->Controls->Add(diffs_);
        s->Controls->Add(L::Wrap("Descriptions come from signed release metadata only; nothing is inferred from binary hashes. A failed update is labeled failed and "
                                 "its fixes are never shown as installed.",
                                 "muted", 920));
        split->Panel2->Controls->Add(s);
        Controls->Add(split);
        Theme::Apply(this);
        BackColor = Theme::Bg;
        HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
        if (entries->Length > 0) list_->SelectedIndex = 0;
    }

private:
    array<Object^>^ entries_;
    EngineClient^ client_;
    ListBox^ list_;
    Label^ summary_;
    BufferedGrid ^ changes_, ^diffs_;

    void OnSelect(Object^, EventArgs^) {
        int i = list_->SelectedIndex;
        if (i < 0 || i >= entries_->Length) return;
        JObj^ e = J::Obj(entries_[i]);
        JObj^ n = J::Obj(e, "notes");
        String^ state = J::Str(e, "state");
        JObj^ fx = J::Obj(n, "fixtures");
        summary_->Text = "Component: " + J::Str(e, "component") + "\nInstalled version -> Active version: " + (J::Str(e, "from") == "" ? "(none)" : J::Str(e, "from")) + " -> " +
                         J::Str(e, "to") + "\nState: " + state + (state == "failed" ? "  -  the previous version remains active; these changes are NOT installed" : "") +
                         "\nActivation time: " + J::Time(J::Int(e, "at")) + "   Restart needed: " + (J::Bool(e, "restartRequired") ? "yes" : "no") +
                         "   Rollback available: " + (J::Bool(n, "rollbackAvailable", state == "activated") ? "yes" : "no") +
                         (fx != nullptr ? "\nFixtures: " + J::Str(fx, "passed") + " passed, " + J::Str(fx, "failed") + " failed" : "") +
                         (J::Str(n, "error") != "" ? "\nError: " + J::Str(n, "error") : "");
        changes_->Rows->Clear();
        if (state != "failed") {
            for each (Object^ o in J::Arr(n, "changelog")) {
                JObj^ c = J::Obj(o);
                changes_->Rows->Add(gcnew array<Object^>{J::Str(c, "kind"), J::Str(c, "title"), J::Str(c, "before"), J::Str(c, "after"), J::Str(c, "impact")});
            }
        }
        diffs_->Rows->Clear();
        for each (Object^ o in J::Arr(n, "parserDiffs")) {
            JObj^ d = J::Obj(o);
            String^ changed = "";
            for each (Object^ c in J::Arr(d, "changed")) changed += Convert::ToString(c) + " ";
            diffs_->Rows->Add(gcnew array<Object^>{J::Str(d, "sample")->Replace("\n", " | "), changed, J::Stringify(J::Get(d, "before")), J::Stringify(J::Get(d, "after"))});
        }
        if (!J::Bool(e, "notesShown") && client_ != nullptr) {
            JObj^ p = J::New();
            p["id"] = J::Int(e, "id");
            client_->Call("updates.markShown", p);
        }
    }
};

}  // namespace PipkiteUI
