#pragma once
// Find the format a channel writes its signals in. The user pastes an example and sees what every format
// (the ones shipped with the app, delivered ones, the channel's own) reads from it, and the same on the
// channel's recent messages. If none reads it right, the user marks the parts of the example to teach
// Pipkite. Nothing here trades: a channel only switches when the user presses "Use this format".

#include "Controls.h"
#include "EngineClient.h"

using namespace System::Text;

namespace PipkiteUI {

public ref class FormatDialog : public Form {
public:
    // example: a message to check right away (may be null)
    FormatDialog(EngineClient^ client, String^ chatId, String^ title, String^ example) {
        client_ = client;
        chatId_ = chatId;
        labels_ = gcnew List<Object^>();
        Text = "Find the signal format";
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        StartPosition = FormStartPosition::CenterParent;
        Size = Drawing::Size(1060, 860);
        MinimumSize = Drawing::Size(820, 560);
        Font = Theme::Body;
        ShowInTaskbar = false;
        FlowLayoutPanel^ s = L::Stack();
        s->Controls->Add(L::Text("How Pipkite reads " + title, "h2"));
        status_ = L::Wrap("Reading the channel's recent messages...", "muted", 980);
        s->Controls->Add(status_);

        // 1. the user's own example, read by every format
        s->Controls->Add(L::Text("Your example", "h2"));
        s->Controls->Add(L::Wrap("Paste a signal from this channel exactly as it was posted, then press Check example. Every format shows what it reads "
                                 "from it. Only a row marked Signal can ever be copied.",
                                 "muted", 980));
        example_ = gcnew TextBox();
        example_->Multiline = true;
        example_->ScrollBars = ScrollBars::Vertical;
        example_->Width = 980;
        example_->Height = 120;
        example_->Font = Theme::Mono;
        example_->AccessibleName = "Example message";
        example_->TextChanged += gcnew EventHandler(this, &FormatDialog::OnExampleChanged);
        s->Controls->Add(example_);
        Button^ check = L::Btn("Check example", "accent", "Show what every format reads from your example. Nothing is saved or traded.");
        check->Click += gcnew EventHandler(this, &FormatDialog::OnCheck);
        s->Controls->Add(L::Row(check));
        exampleGrid_ = NewGrid("format", "Format", 230, 170, "Paste an example and press Check example.", "What each format reads from your example");
        exampleGrid_->SelectionChanged += gcnew EventHandler(this, &FormatDialog::OnExampleRowSelected);
        s->Controls->Add(exampleGrid_);
        use_ = UseButton();
        s->Controls->Add(L::Row(use_));

        // 2. teach, on the same example
        s->Controls->Add(L::Text("None reads it right? Teach Pipkite", "h2"));
        s->Controls->Add(L::Wrap("In your example above, select each part and press its button: the symbol, the Buy or Sell word, the entry, the SL "
                                 "and every TP. Then test it on the recent messages before using it.",
                                 "muted", 980));
        FlowLayoutPanel^ fields = L::Row();
        for each (String^ f in gcnew array<String^>{"Symbol", "Buy", "Sell", "Entry", "SL", "TP"}) {
            Button^ b = L::Btn(f, nullptr, "Mark the selected text as the " + f + (f == "TP" ? " (once per target)" : ""));
            b->Name = f->ToLowerInvariant();
            b->Click += gcnew EventHandler(this, &FormatDialog::OnMark);
            fields->Controls->Add(b);
        }
        canonical_ = L::Box(110);
        canonical_->AccessibleName = "Symbol means";
        L::Tip(canonical_, "The symbol name Pipkite uses for what you marked, e.g. XAUUSD for GOLD. Leave empty to use the marked text.");
        fields->Controls->Add(L::Text("Symbol means:"));
        fields->Controls->Add(canonical_);
        s->Controls->Add(fields);
        marks_ = L::Wrap("Nothing marked yet.", "muted", 980);
        s->Controls->Add(marks_);
        Button^ test = L::Btn("Test on recent messages", nullptr, "Build a format from your marks and show what it reads. Nothing is saved yet.");
        test->Click += gcnew EventHandler(this, &FormatDialog::OnTest);
        Button^ clear = L::Btn("Clear marks");
        clear->Click += gcnew EventHandler(this, &FormatDialog::OnClearMarks);
        s->Controls->Add(L::Row(test, clear));

        // 3. the channel's recent messages, read by one format at a time
        s->Controls->Add(L::Text("Recent messages in the channel", "h2"));
        formats_ = L::Combo(660, gcnew array<String^>{});
        formats_->DropDownStyle = ComboBoxStyle::DropDownList;
        formats_->AccessibleName = "Format";
        formats_->SelectedIndexChanged += gcnew EventHandler(this, &FormatDialog::OnFormatChanged);
        s->Controls->Add(L::Row(L::Text("Format:"), formats_));
        recentGrid_ = NewGrid("message", "Message", 250, 250, "No recent text messages (Telegram not signed in, or nothing posted yet).",
                              "What the format reads in each recent message");
        recentGrid_->SelectionChanged += gcnew EventHandler(this, &FormatDialog::OnRecentRowSelected);
        s->Controls->Add(recentGrid_);
        s->Controls->Add(L::Wrap("Click a message to copy it into Your example.", "muted", 980));
        use2_ = UseButton();
        Button^ close = L::Btn("Close");
        close->DialogResult = Windows::Forms::DialogResult::Cancel;
        s->Controls->Add(L::Row(use2_, close));
        Controls->Add(s);
        CancelButton = close;
        Theme::Apply(this);
        BackColor = Theme::Bg;
        HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
        if (!String::IsNullOrWhiteSpace(example)) example_->Text = example->Replace("\r\n", "\n")->Replace("\n", "\r\n");
        Detect(nullptr);
    }

private:
    EngineClient^ client_;
    String^ chatId_;
    Label ^ status_, ^marks_;
    ComboBox^ formats_;
    BufferedGrid ^ exampleGrid_, ^recentGrid_;
    Button ^ use_, ^use2_;
    TextBox ^ example_, ^canonical_;
    List<Object^>^ labels_;
    array<Object^>^ texts_;
    array<Object^>^ candidates_;
    String^ current_;
    JObj^ taught_;         // template built from the marks, when the lists show it
    JObj^ pendingTaught_;  // template sent with the request in flight
    String^ pendingExample_;
    bool hasExample_;      // texts_[0] is the user's example
    bool filling_;

    BufferedGrid^ NewGrid(String^ firstName, String^ firstHeader, int firstWidth, int height, String^ empty, String^ accessible) {
        BufferedGrid^ g = gcnew BufferedGrid();
        g->Width = 980;
        g->Height = height;
        g->AddCol(firstName, firstHeader, firstWidth);
        g->AddCol("read", "Read as", 80);
        g->AddCol("symbol", "Symbol", 80);
        g->AddCol("side", "Side", 55);
        g->AddCol("entry", "Entry", 120);
        g->AddCol("sl", "SL", 80);
        g->AddCol("tp", "TPs", 150);
        DataGridViewTextBoxColumn^ note = g->AddCol("note", "Note", 200);
        note->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        g->EmptyText = empty;
        g->AccessibleName = accessible;
        return g;
    }

    Button^ UseButton() {
        Button^ b = L::Btn("Use this format", "accent",
                           "Read this channel with the selected format from now on. Only messages read as Signal are ever copied; Review and Ignored never trade.");
        b->Enabled = false;
        b->Click += gcnew EventHandler(this, &FormatDialog::OnUse);
        return b;
    }

    void Detect(JObj^ tmpl) {
        JObj^ p = J::New();
        p["chatId"] = chatId_;
        p["limit"] = 8;
        if (tmpl != nullptr) p["template"] = tmpl;
        pendingExample_ = example_->Text->Trim() == "" ? nullptr : example_->Text;
        if (pendingExample_ != nullptr) p["samples"] = gcnew array<Object^>{pendingExample_};
        pendingTaught_ = tmpl;
        use_->Enabled = false;
        use2_->Enabled = false;
        client_->Call("source.detectFormat", p, gcnew ResponseHandler(this, &FormatDialog::OnDetected));
    }

    void OnCheck(Object^, EventArgs^) {
        if (example_->Text->Trim() == "") {
            status_->Text = "Paste an example signal first.";
            return;
        }
        status_->Text = "Checking your example with every format...";
        Detect(nullptr);
    }

    int RecentStart() { return hasExample_ ? 1 : 0; }

    void OnDetected(bool ok, Object^ result, String^ error) {
        if (!ok) {
            status_->Text = "Couldn't check the format: " + error;
            return;
        }
        JObj^ r = J::Obj(result);
        texts_ = J::Arr(r, "texts");
        candidates_ = J::Arr(r, "candidates");
        current_ = J::Str(r, "current");
        taught_ = pendingTaught_;
        hasExample_ = pendingExample_ != nullptr && texts_->Length > 0 && texts_[0]->ToString() == pendingExample_;
        const int recent = texts_->Length - RecentStart();

        filling_ = true;
        formats_->Items->Clear();
        exampleGrid_->Rows->Clear();
        for each (Object^ o in candidates_) {
            JObj^ c = J::Obj(o);
            array<Object^>^ rows = J::Arr(c, "samples");
            int signals = 0;
            for (int i = RecentStart(); i < rows->Length; ++i)
                if (J::Str(J::Obj(rows[i]), "outcome") == "signal") ++signals;
            String^ name = J::Str(c, "name") + (J::Str(c, "id") == current_ ? " (in use)" : "");
            formats_->Items->Add(name + " - " + signals + " of " + recent + " recent messages read as signals");
            if (hasExample_ && rows->Length > 0) AddRow(exampleGrid_, name, J::Obj(rows[0]), nullptr);
        }
        exampleGrid_->ClearSelection();
        exampleGrid_->Invalidate();
        filling_ = false;

        if (taught_ != nullptr)
            status_->Text = "Your taught format, on your example and " + recent + " recent messages. If every Signal row is right, press Use this format.";
        else if (hasExample_)
            status_->Text = "Every format on your example (top) and on " + recent + " recent messages (bottom). Pick the one that reads them right.";
        else if (pendingExample_ != nullptr)
            status_->Text = "Your example is empty or too long to check (over 1200 bytes).";
        else if (recent == 0)
            status_->Text = "No recent text messages were found. Paste an example under Your example and press Check example.";
        else
            status_->Text = "Tried every format on " + recent + " recent messages, best match first. Paste one of your signals under Your example to "
                            "check it too.";
        if (formats_->Items->Count > 0) formats_->SelectedIndex = 0;
        else OnFormatChanged(nullptr, nullptr);
    }

    JObj^ SelectedCandidate() {
        int i = formats_->SelectedIndex;
        return candidates_ != nullptr && i >= 0 && i < candidates_->Length ? J::Obj(candidates_[i]) : nullptr;
    }

    static String^ FirstLine(String^ t) {
        for each (String^ line in t->Split('\n')) {
            String^ l = line->Trim();
            if (l != "") return l->Length > 70 ? l->Substring(0, 70) + "..." : l;
        }
        return "";
    }

    void AddRow(BufferedGrid^ g, String^ first, JObj^ row, String^ tag) {
        String^ outcome = J::Str(row, "outcome");
        bool signal = outcome == "signal";
        String^ entry = "";
        if (J::Has(row, "entryLow")) {
            double lo = J::Num(row, "entryLow"), hi = J::Num(row, "entryHigh");
            entry = lo == hi ? J::Fmt(lo) : J::Fmt(lo) + " - " + J::Fmt(hi);
        } else if (signal) {
            entry = "market";
        }
        StringBuilder^ tps = gcnew StringBuilder();
        for each (Object^ tp in J::Arr(row, "takeProfits")) tps->Append((tps->Length > 0 ? ", " : "") + J::Fmt(Convert::ToDouble(tp)));
        int idx = g->Rows->Add(gcnew array<Object^>{first, signal ? "Signal" : outcome == "review" ? "Review" : "Ignored", signal ? J::Str(row, "symbol") : "",
                                                    signal ? J::Str(row, "side") : "", entry, J::Has(row, "stopLoss") && signal ? J::Fmt(J::Num(row, "stopLoss")) : "",
                                                    signal ? tps->ToString() : "", J::Str(row, "reason")});
        g->Rows[idx]->Tag = tag;
        g->Rows[idx]->DefaultCellStyle->ForeColor = signal ? Theme::Success : outcome == "review" ? Theme::Warning : Theme::TextMuted;
    }

    void OnFormatChanged(Object^, EventArgs^) {
        filling_ = true;
        recentGrid_->Rows->Clear();
        JObj^ c = SelectedCandidate();
        if (c != nullptr) {
            array<Object^>^ rows = J::Arr(c, "samples");
            for (int i = RecentStart(); i < rows->Length && i < texts_->Length; ++i) AddRow(recentGrid_, FirstLine(texts_[i]->ToString()), J::Obj(rows[i]), texts_[i]->ToString());
        }
        recentGrid_->ClearSelection();
        recentGrid_->Invalidate();
        exampleGrid_->ClearSelection();
        if (formats_->SelectedIndex >= 0 && formats_->SelectedIndex < exampleGrid_->Rows->Count) exampleGrid_->Rows[formats_->SelectedIndex]->Selected = true;
        filling_ = false;
        // a format can be used once it reads at least one message (the example or a recent one) as a complete signal
        use_->Enabled = c != nullptr && J::Int(c, "signals") > 0;
        use2_->Enabled = use_->Enabled;
    }

    void OnExampleRowSelected(Object^, EventArgs^) {
        if (filling_ || exampleGrid_->SelectedRows->Count == 0) return;
        formats_->SelectedIndex = exampleGrid_->SelectedRows[0]->Index;  // same order as the format list
    }

    void OnRecentRowSelected(Object^, EventArgs^) {
        if (filling_ || recentGrid_->SelectedRows->Count == 0) return;
        String^ text = dynamic_cast<String^>(recentGrid_->SelectedRows[0]->Tag);
        if (text != nullptr) example_->Text = text->Replace("\r\n", "\n")->Replace("\n", "\r\n");
    }

    void OnUse(Object^, EventArgs^) {
        JObj^ c = SelectedCandidate();
        if (c == nullptr) return;
        JObj^ p = J::New();
        p["chatId"] = chatId_;
        use_->Enabled = false;
        use2_->Enabled = false;
        if (taught_ != nullptr) {
            // saved as this channel's own parser; the example becomes its test message
            p["template"] = taught_;
            p["sample"] = example_->Text;
            client_->Call("parser.adopt", p, gcnew ResponseHandler(this, &FormatDialog::OnUsed));
        } else {
            p["chatIds"] = gcnew array<Object^>{chatId_};
            p["template"] = J::Str(c, "id");
            client_->Call("sources.bulkApply", p, gcnew ResponseHandler(this, &FormatDialog::OnUsed));
        }
    }

    void OnUsed(bool ok, Object^, String^ error) {
        if (!ok) {
            use_->Enabled = true;
            use2_->Enabled = true;
            status_->Text = "Not saved: " + error;
            return;
        }
        DialogResult = Windows::Forms::DialogResult::OK;
        Close();
    }

    void OnExampleChanged(Object^, EventArgs^) {
        if (labels_->Count > 0) OnClearMarks(nullptr, nullptr);  // marks point into the old text
    }

    void OnClearMarks(Object^, EventArgs^) {
        labels_->Clear();
        marks_->Text = "Nothing marked yet.";
    }

    void OnMark(Object^ sender, EventArgs^) {
        String^ field = safe_cast<Control^>(sender)->Name;
        String^ text = example_->Text;
        int start = example_->SelectionStart, length = example_->SelectionLength;
        if (length == 0) {
            marks_->Text = "Select the " + field + " in Your example first, then press its button.";
            return;
        }
        JObj^ l = J::New();
        l["field"] = field;
        l["begin"] = Encoding::UTF8->GetByteCount(text->Substring(0, start));  // the engine works in UTF-8 byte offsets
        l["end"] = Encoding::UTF8->GetByteCount(text->Substring(0, start + length));
        String^ shown = text->Substring(start, length)->Trim();
        if (field == "symbol" && canonical_->Text->Trim() != "") {
            l["canonical"] = canonical_->Text->Trim()->ToUpperInvariant();
            shown += " = " + canonical_->Text->Trim()->ToUpperInvariant();
        }
        labels_->Add(l);
        if (labels_->Count == 1) marks_->Text = "Marked: ";
        else marks_->Text += "  ·  ";
        marks_->Text += field->ToUpperInvariant() + " " + shown;
    }

    void OnTest(Object^, EventArgs^) {
        if (example_->Text->Trim() == "" || labels_->Count == 0) {
            marks_->Text = "Put a signal in Your example and mark at least one part of it first.";
            return;
        }
        JObj^ p = J::New();
        p["chatId"] = chatId_;
        p["sample"] = example_->Text;
        p["labels"] = labels_->ToArray();
        status_->Text = "Building a format from your marks...";
        client_->Call("parser.learn", p, gcnew ResponseHandler(this, &FormatDialog::OnLearned));
    }

    void OnLearned(bool ok, Object^ result, String^ error) {
        if (!ok) {
            status_->Text = "Couldn't learn from the marks: " + error;
            return;
        }
        JObj^ r = J::Obj(result);
        StringBuilder^ notes = gcnew StringBuilder();
        for each (Object^ o in J::Arr(r, "learned")) notes->Append(o->ToString() + ". ");
        for each (Object^ o in J::Arr(r, "warnings")) notes->Append(o->ToString() + ". ");
        if (notes->Length > 0) marks_->Text = notes->ToString();
        status_->Text = "Testing your format...";
        Detect(J::Obj(r, "template"));
    }
};

}  // namespace PipkiteUI
