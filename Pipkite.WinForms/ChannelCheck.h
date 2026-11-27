#pragma once
// What Pipkite reads in one channel, in plain words. Every format is tried on the channel's latest messages
// (source.detectFormat, read-only); the panel says how well the channel's format (or, without one, the best match)
// reads them and shows its latest signals as Pipkite reads them. Start copying uses the format shown and gives the OK
// through source.confirmFormat, the only place the OK is recorded: nothing is picked or copied before that click.
// "Not right?" opens the full format dialog to pick another format or teach one.

#include "FormatDialog.h"

namespace PipkiteUI {

public ref class ChannelCheck : public FlowLayoutPanel {
public:
    event EventHandler^ Changed;  // the channel's format or OK changed: the page reloads its channels

    ChannelCheck(int width) {
        FlowDirection = Windows::Forms::FlowDirection::TopDown;
        WrapContents = false;
        AutoSize = true;
        AutoSizeMode = Windows::Forms::AutoSizeMode::GrowAndShrink;
        Margin = Windows::Forms::Padding(0, 8, 0, 4);
        Visible = false;
        verdict_ = L::Wrap("", "h2", width);
        Controls->Add(verdict_);
        heads_ = gcnew array<Label^>(kCards);
        reads_ = gcnew array<Label^>(kCards);
        for (int i = 0; i < kCards; ++i) {
            heads_[i] = L::Wrap("", "muted", width);
            heads_[i]->Margin = Windows::Forms::Padding(0, 8, 8, 0);
            reads_[i] = L::Wrap("", nullptr, width);
            reads_[i]->Margin = Windows::Forms::Padding(0, 2, 8, 2);
            Controls->Add(heads_[i]);
            Controls->Add(reads_[i]);
        }
        start_ = L::Btn("Start copying", "accent",
                        "Copy this channel's signals from now on, read as shown here. Execution policy and Symbol limits still decide each trade.");
        start_->Click += gcnew EventHandler(this, &ChannelCheck::OnStart);
        fix_ = L::Btn("Not right?", nullptr, "See what every format reads in this channel and pick another, or show Pipkite which part of a signal is which.");
        fix_->Click += gcnew EventHandler(this, &ChannelCheck::OnFix);
        again_ = L::Btn("Check again", nullptr, "Read the channel's latest messages again.");
        again_->Click += gcnew EventHandler(this, &ChannelCheck::OnAgain);
        Controls->Add(L::Row(start_, fix_, again_));
        note_ = L::Wrap("", "muted", width);
        Controls->Add(note_);
    }

    // Shows what Pipkite reads in this channel (an entry of sources.list). Asks the engine only for another channel
    // or on Check again; the same channel is just shown again with its new state.
    void ShowChannel(EngineClient^ client, JObj^ binding) {
        client_ = client;
        const bool same = binding_ != nullptr && J::Str(binding_, "chatId") == J::Str(binding, "chatId");
        binding_ = binding;
        Visible = true;
        if (same && busy_) return;  // its readings are on the way
        if (same && result_ != nullptr) Render();
        else Ask();
    }
    // the channel's format changed elsewhere: read it again
    void Recheck() {
        if (binding_ != nullptr) Ask();
    }

private:
    literal int kCards = 3;
    EngineClient^ client_;
    JObj ^ binding_, ^asked_, ^result_, ^shown_;
    Label ^ verdict_, ^note_;
    array<Label^> ^ heads_, ^reads_;
    Button ^ start_, ^fix_, ^again_;
    String ^ confirming_, ^startChat_, ^startFormat_, ^failed_;  // startChat_: the channel whose Start copying is under way
    bool busy_;

    void Ask() {
        result_ = nullptr;
        shown_ = nullptr;
        failed_ = nullptr;
        verdict_->Text = "Reading the latest messages of " + J::Str(binding_, "title") + "...";
        for (int i = 0; i < kCards; ++i) {
            heads_[i]->Visible = false;
            reads_[i]->Visible = false;
        }
        start_->Visible = false;
        fix_->Enabled = false;
        again_->Enabled = false;
        note_->Text = "";
        if (busy_) return;  // the answer on its way is for the channel shown before; this one is asked right after
        busy_ = true;
        asked_ = binding_;
        JObj^ p = J::New();
        p["chatId"] = J::Str(binding_, "chatId");
        // test-only (PIPKITE_TEST_HOOKS=1): messages checked as if the channel had posted them ("|" between them), for screenshots
        String^ test = Environment::GetEnvironmentVariable("PIPKITE_TEST_HOOKS") == "1" ? Environment::GetEnvironmentVariable("PIPKITE_TEST_SAMPLES") : nullptr;
        if (test != nullptr) p["samples"] = test->Replace("\\n", "\n")->Split('|');
        client_->Call("source.detectFormat", p, gcnew ResponseHandler(this, &ChannelCheck::OnDetected));
    }

    void OnDetected(bool ok, Object^ result, String^ error) {
        busy_ = false;
        if (J::Str(asked_, "chatId") != J::Str(binding_, "chatId")) return Ask();  // another channel was picked meanwhile
        fix_->Enabled = true;
        again_->Enabled = true;
        if (!ok) {
            verdict_->Text = "Couldn't read this channel: " + error;
            return;
        }
        result_ = J::Obj(result);
        Render();
    }

    void Render() {
        array<Object^>^ texts = J::Arr(result_, "texts");
        array<Object^>^ cands = J::Arr(result_, "candidates");
        String^ current = J::Str(result_, "current");
        // the channel's own format when it has one (the user picked it, the engine always lists it), else the best match
        shown_ = nullptr;
        for each (Object^ o in cands)
            if (current != "" && J::Str(J::Obj(o), "id") == current) shown_ = J::Obj(o);
        if (shown_ == nullptr && current == "" && cands->Length > 0) shown_ = J::Obj(cands[0]);
        int good = 0, look = 0, card = 0;
        array<Object^>^ rows = J::Arr(shown_, "samples");
        for (int i = 0; i < rows->Length && i < texts->Length; ++i) {  // newest first: the latest signals are shown
            JObj^ row = J::Obj(rows[i]);
            const bool signal = J::Str(row, "outcome") == "signal", warn = signal && J::Bool(row, "priceWarning");
            if (signal && !warn) ++good;
            else if (signal || (J::Str(row, "outcome") == "review" && J::Str(row, "kind") != "Management")) ++look;
            else continue;  // chatter, results and trade management ("close now, set breakeven") are not signals
            if (card == kCards) continue;
            heads_[card]->Text = FirstLine(texts[i]->ToString());
            reads_[card]->Text = Reading(row);
            reads_[card]->ForeColor = signal && !warn ? Theme::Success : Theme::Warning;
            heads_[card]->Visible = true;
            reads_[card]->Visible = true;
            ++card;
        }
        for (int i = card; i < kCards; ++i) {
            heads_[i]->Visible = false;
            reads_[i]->Visible = false;
        }

        const int total = texts->Length;
        String^ last = total == 1 ? "its last message" : "its last " + total + " messages";
        const bool paused = !J::Bool(binding_, "enabled", true);
        const bool copying = current != "" && !J::Bool(binding_, "formatPending");
        String^ v;
        if (total == 0) v = "Pipkite found no recent messages in this channel.";
        else if (good == 0 && look == 0) v = "No signals in " + last + " yet.";
        else if (good == 0) v = "Pipkite can't read this channel's signals yet.";
        else if (look == 0) v = "Pipkite understands this channel: it read " + good + (good == 1 ? " signal" : " signals") + " in " + last + ".";
        else v = "Pipkite read " + good + " of " + (good + look) + " signals in " + last + ". " + (look == 1 ? "One needs a look." : "The others need a look.");
        verdict_->Text = (paused ? "Paused. " : copying ? "Copying. " : "") + v;

        // with its own format the channel only waits for the OK (its signals can also be checked in History); without
        // one it gets the format shown here, and only if that reads at least one signal right
        start_->Visible = shown_ != nullptr && !paused && !copying && (current != "" || good > 0);
        start_->Enabled = startChat_ == nullptr;
        const bool better = shown_ != nullptr && cands->Length > 0 && J::Int(J::Obj(cands[0]), "signals") > J::Int(shown_, "signals");
        fix_->Text = good == 0 && !better ? "Show Pipkite a signal..." : "Not right?";
        fix_->AccessibleName = fix_->Text;
        String^ n;
        if (failed_ != nullptr) n = "Not started: " + failed_;
        else if (paused) n = "Tick it in Telegram sources to copy it again.";
        else if (copying) n = "New signals it reads are copied with your Execution policy and Symbol limits.";
        else if (start_->Visible) n = good == 0 ? "Check its signals in History first. Nothing is copied until you press Start copying." : "Nothing is copied until you press Start copying.";
        else if (total == 0)  // not signed in, a channel gone quiet, or its comments or direct-messages chat ticked instead
            n = "Telegram sent no recent messages for this chat. Check that Telegram is signed in and that this is the chat where the "
                "signals are posted, then press Check again.";
        else n = "Show Pipkite one of its signals, or ask the Pipkite team for a parser (Signal rules > Advanced tools).";
        if (better) n += " Another format reads more of its messages: press Not right? to compare.";
        note_->Text = n;
    }

    static String^ FirstLine(String^ t) {
        for each (String^ line in t->Split('\n')) {
            String^ l = line->Trim();
            if (l != "") return l->Length > 90 ? l->Substring(0, 90) + "..." : l;
        }
        return "";
    }

    // what Pipkite would trade, in the words of a signal
    static String^ Reading(JObj^ row) {
        if (J::Str(row, "outcome") != "signal") {
            String^ why = J::Str(row, "reason");
            return "Needs a look: " + (why == "" ? "not a complete signal" : why);
        }
        StringBuilder^ t = gcnew StringBuilder(J::Str(row, "side") + " " + J::Str(row, "symbol"));
        if (J::Has(row, "entryLow")) {
            const double lo = J::Num(row, "entryLow"), hi = J::Num(row, "entryHigh");
            t->Append(" · entry " + (lo == hi ? J::Fmt(lo) : J::Fmt(lo) + " - " + J::Fmt(hi)));
        }
        if (J::Has(row, "stopLoss")) t->Append(" · stop " + J::Fmt(J::Num(row, "stopLoss")));
        array<Object^>^ tps = J::Arr(row, "takeProfits");
        for (int i = 0; i < tps->Length; ++i) t->Append((i == 0 ? " · take profit " : ", ") + J::Fmt(Convert::ToDouble(tps[i])));
        if (J::Bool(row, "priceWarning")) t->Append(" · " + J::Str(row, "reason"));
        return t->ToString();
    }

    void OnAgain(Object^, EventArgs^) { Ask(); }

    void OnFix(Object^, EventArgs^) {
        FormatDialog^ d = gcnew FormatDialog(client_, J::Str(binding_, "chatId"), J::Str(binding_, "title"), nullptr);
        if (d->ShowDialog(FindForm()) != Windows::Forms::DialogResult::OK) return;
        Ask();                             // the format picked there waits for Start copying here
        Changed(this, EventArgs::Empty);  // the page reloads the channel, with its new format
    }

    void OnStart(Object^, EventArgs^) {
        if (shown_ == nullptr || startChat_ != nullptr) return;
        // channel and format are fixed at the click: the replies confirm this channel only, whichever one is shown by then
        startChat_ = J::Str(binding_, "chatId");
        startFormat_ = J::Str(shown_, "id") + "@" + J::Str(shown_, "version");
        failed_ = nullptr;
        start_->Enabled = false;
        if (J::Str(result_, "current") != "") return Confirm(startFormat_);  // its own format: only the OK is missing
        JObj^ p = J::New();
        p["chatId"] = startChat_;
        if (J::Bool(shown_, "shared")) {
            // made by other users for this channel: saved as its own format, with a message it reads as its test
            p["sharedId"] = J::Str(shown_, "id");
            p["sample"] = SignalText();
            client_->Call("parser.adopt", p, gcnew ResponseHandler(this, &ChannelCheck::OnAdopted));
        } else {
            p["chatIds"] = gcnew array<Object^>{startChat_};
            p["template"] = J::Str(shown_, "id");
            client_->Call("sources.bulkApply", p, gcnew ResponseHandler(this, &ChannelCheck::OnApplied));
        }
    }

    // a message the format reads as a complete signal at a believable price
    String^ SignalText() {
        array<Object^>^ rows = J::Arr(shown_, "samples");
        array<Object^>^ texts = J::Arr(result_, "texts");
        for (int i = 0; i < rows->Length && i < texts->Length; ++i)
            if (J::Str(J::Obj(rows[i]), "outcome") == "signal" && !J::Bool(J::Obj(rows[i]), "priceWarning")) return texts[i]->ToString();
        return "";
    }

    void OnApplied(bool ok, Object^, String^ error) {
        if (!ok) return Failed(error);
        Confirm(startFormat_);
    }
    void OnAdopted(bool ok, Object^ result, String^ error) {
        if (!ok) return Failed(error);
        Confirm(J::Str(J::Obj(result), "id") + "@" + J::Str(J::Obj(result), "version"));
    }
    void Confirm(String^ format) {
        confirming_ = format;
        JObj^ p = J::New();
        p["chatId"] = startChat_;
        p["format"] = format;  // the version whose readings the user saw; a newer one must be checked first
        client_->Call("source.confirmFormat", p, gcnew ResponseHandler(this, &ChannelCheck::OnConfirmed));
    }
    void OnConfirmed(bool ok, Object^, String^ error) {
        if (!ok) return Failed(error);
        if (Finish()) {
            // shown as copying right away; the page reloads the channel from the engine
            binding_["formatPending"] = false;
            binding_["activeFormat"] = confirming_;
            if (result_ != nullptr && shown_ != nullptr) {
                result_["current"] = J::Str(shown_, "id");
                Render();
            }
        }
        Changed(this, EventArgs::Empty);
    }
    void Failed(String^ error) {
        if (Finish()) {
            failed_ = error;  // kept when the page reloads the channel, until the next check or start
            if (result_ != nullptr) Render();
        }
        Changed(this, EventArgs::Empty);  // a format may have been saved before the OK failed
    }
    // ends the start; true when its channel is still the one shown
    bool Finish() {
        const bool same = J::Str(binding_, "chatId") == startChat_;
        startChat_ = nullptr;
        start_->Enabled = true;
        return same;
    }
};

}  // namespace PipkiteUI
