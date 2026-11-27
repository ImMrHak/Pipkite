#pragma once
// Everything that is set per Telegram channel, in one place: the broker symbol it trades, how it enters, and its own
// sizing and take-profit if it shouldn't follow Execution policy. Saved field by field (source.update), so other pages'
// changes to the same channel are kept.

#include "Controls.h"
#include "EngineClient.h"

namespace PipkiteUI {

// Searchable symbol picker: type to filter the full broker symbol list, double-click or Select to choose.
// With allowAny, the first entry clears the choice (Selected = "": each message's own symbol).
public ref class SymbolPickerDialog : public Form {
public:
    literal String^ AnySymbol = "(each message's own symbol)";
    String^ Selected;
    SymbolPickerDialog(System::Collections::Generic::List<String^>^ symbols, String^ current, bool allowAny) {
        all_ = symbols;
        allowAny_ = allowAny;
        Selected = nullptr;
        Text = "Choose symbol";
        FormBorderStyle = Windows::Forms::FormBorderStyle::FixedDialog;
        StartPosition = FormStartPosition::CenterParent;
        MaximizeBox = false;
        MinimizeBox = false;
        ClientSize = Drawing::Size(340, 440);
        Font = Theme::Body;
        search_ = gcnew TextBox();
        search_->SetBounds(16, 16, 308, 26);
        search_->AccessibleName = "Search symbols";
        search_->TextChanged += gcnew EventHandler(this, &SymbolPickerDialog::OnSearch);
        list_ = gcnew ListBox();
        list_->SetBounds(16, 50, 308, 330);
        list_->AccessibleName = "Broker symbols";
        list_->DoubleClick += gcnew EventHandler(this, &SymbolPickerDialog::OnPick);
        Button^ ok = L::Btn("Select", "accent");
        ok->SetBounds(16, 392, 90, 32);
        ok->Click += gcnew EventHandler(this, &SymbolPickerDialog::OnPick);
        Button^ cancel = L::Btn("Cancel");
        cancel->SetBounds(234, 392, 90, 32);
        cancel->DialogResult = Windows::Forms::DialogResult::Cancel;
        Controls->Add(search_);
        Controls->Add(list_);
        Controls->Add(ok);
        Controls->Add(cancel);
        AcceptButton = ok;
        CancelButton = cancel;
        Fill("");
        if (current != nullptr && current != "") search_->Text = current;
        Theme::Apply(this);
        BackColor = Theme::Bg;
        HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
        search_->Select();
    }

private:
    System::Collections::Generic::List<String^>^ all_;
    bool allowAny_;
    TextBox^ search_;
    ListBox^ list_;
    void Fill(String^ q) {
        String^ u = q->Trim()->ToUpperInvariant();
        list_->BeginUpdate();
        list_->Items->Clear();
        if (allowAny_) list_->Items->Add(AnySymbol);
        for each (String ^ s in all_)
            if (u == "" || s->ToUpperInvariant()->Contains(u)) list_->Items->Add(s);
        list_->EndUpdate();
        if (list_->Items->Count > 0) list_->SelectedIndex = allowAny_ && list_->Items->Count > 1 && u != "" ? 1 : 0;
    }
    void OnSearch(Object^, EventArgs^) { Fill(search_->Text); }
    void OnPick(Object^, EventArgs^) {
        if (list_->SelectedItem == nullptr) return;
        Selected = list_->SelectedItem->ToString() == AnySymbol ? "" : list_->SelectedItem->ToString();
        DialogResult = Windows::Forms::DialogResult::OK;
    }
};

public ref class ChannelDialog : public Form {
public:
    ChannelDialog(EngineClient^ client, JObj^ binding) {
        client_ = client;
        binding_ = binding;
        catalog_ = gcnew System::Collections::Generic::List<String^>();
        Text = "Channel settings - " + J::Str(binding, "title");
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        StartPosition = FormStartPosition::CenterParent;
        Size = Drawing::Size(820, 760);
        MinimumSize = Drawing::Size(640, 480);
        Font = Theme::Body;
        ShowInTaskbar = false;
        ShowIcon = false;
        FlowLayoutPanel^ s = L::Stack();
        s->Controls->Add(L::Wrap("Settings for this channel only. Execution policy applies to every channel, and Symbol limits to each symbol.", "muted", 720));

        TableLayoutPanel^ sym = L::Form2();
        symbol_ = L::Box(220);
        symbol_->ReadOnly = true;
        symbol_->TabStop = false;  // shows the choice; Choose... changes it
        Button^ choose = L::Btn("Choose...", nullptr, "Pick the broker symbol this channel trades, or let it trade each message's own symbol.");
        choose->Click += gcnew EventHandler(this, &ChannelDialog::OnChooseSymbol);
        L::AddField(sym, "Broker symbol", L::Row(symbol_, choose),
                    "Only for a channel that posts one instrument, e.g. gold as XAUUSD.m: its messages trade that symbol, and a message for another "
                    "symbol is skipped. Otherwise each message trades its own symbol, found in Symbol limits.");
        s->Controls->Add(L::Group("Symbol", sym));

        TableLayoutPanel^ en = L::Form2();
        entry_ = L::Combo(320, gcnew array<String^>{"Wait for the entry price", "Enter right away (NOW signals)", "Pending orders only (LIMIT / STOP)"});
        entry_->DropDownStyle = ComboBoxStyle::DropDownList;
        entry_->SelectedIndexChanged += gcnew EventHandler(this, &ChannelDialog::OnChoice);
        L::AddField(en, "How it enters", entry_,
                    "How a signal from this channel becomes an order. In every mode a signal needs an entry price, a stop loss and a take profit; "
                    "BUY NOW without a price is never traded. Price distances come from the symbol's Entry deviation in Symbol limits.");
        entryHint_ = L::Wrap("", "muted", 520);
        L::AddField(en, "", entryHint_);
        s->Controls->Add(L::Group("Entry", en));

        TableLayoutPanel^ sz = L::Form2();
        globalSizing_ = L::Check("Use the sizing from Execution policy");
        globalSizing_->CheckedChanged += gcnew EventHandler(this, &ChannelDialog::OnChoice);
        L::AddField(sz, "", globalSizing_);
        sizingMode_ = L::Combo(220, gcnew array<String^>{"Fixed lots", "Risk % of equity"});
        sizingMode_->DropDownStyle = ComboBoxStyle::DropDownList;
        sizingMode_->SelectedIndexChanged += gcnew EventHandler(this, &ChannelDialog::OnChoice);
        L::AddField(sz, "Sizing mode", sizingMode_, "Fixed lots: the same lot size for every signal of this channel. Risk %: lots sized so the loss at the stop loss is about that % of your equity.");
        lots_ = L::Num(0.01, 100, 0.01, 2, 0.01);
        L::AddField(sz, "Fixed total lots", lots_);
        risk_ = L::Num(0.01, 10, 0.5, 2, 0.05);
        L::AddField(sz, "Risk % per trade", risk_);
        s->Controls->Add(L::Group("Position sizing", sz));

        TableLayoutPanel^ tp = L::Form2();
        globalTargets_ = L::Check("Use the take-profit setting from Execution policy");
        globalTargets_->CheckedChanged += gcnew EventHandler(this, &ChannelDialog::OnChoice);
        L::AddField(tp, "", globalTargets_);
        targetMode_ = L::Combo(220, gcnew array<String^>{"One take-profit", "Split across take-profits"});
        targetMode_->DropDownStyle = ComboBoxStyle::DropDownList;
        targetMode_->SelectedIndexChanged += gcnew EventHandler(this, &ChannelDialog::OnChoice);
        L::AddField(tp, "Target mode", targetMode_, "One take-profit: the whole trade closes at one of the message's TPs. Split: the trade is divided into parts, each closing at its own TP.");
        tpIndex_ = L::Num(1, 20, 1, 0, 1);
        L::AddField(tp, "Selected TP", tpIndex_, "Which take-profit to use, by its position in the message (1 = first). With split targets, the one used when the trade is too small to split.");
        allocations_ = L::Box(200);
        L::AddField(tp, "Split allocations %", allocations_, "The percentage of the trade for each TP, e.g. 50,30,20. They must add up to 100.");
        fallback_ = L::Check("If the trade is too small to split, use the selected TP instead of skipping it");
        fallback_->CheckedChanged += gcnew EventHandler(this, &ChannelDialog::OnChoice);
        L::AddField(tp, "", fallback_);
        s->Controls->Add(L::Group("Take profit", tp));

        Button^ save = L::Btn("Save", "accent", "Save this channel's settings. Signals already waiting are checked again with them.");
        save->Click += gcnew EventHandler(this, &ChannelDialog::OnSave);
        Button^ cancel = L::Btn("Cancel");
        cancel->DialogResult = Windows::Forms::DialogResult::Cancel;
        status_ = L::Wrap("", "muted", 520);
        s->Controls->Add(L::Row(save, cancel, status_));
        Controls->Add(s);
        CancelButton = cancel;
        ShowBinding(binding);
        Theme::Apply(this);
        BackColor = Theme::Bg;
        HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
        client_->Call("mt5.status", nullptr, gcnew ResponseHandler(this, &ChannelDialog::OnStatus));  // broker symbols for the picker
    }

private:
    EngineClient^ client_;
    JObj^ binding_;
    System::Collections::Generic::List<String^>^ catalog_;
    String^ tradeSymbol_;
    TextBox ^ symbol_, ^allocations_;
    ComboBox ^ entry_, ^sizingMode_, ^targetMode_;
    CheckBox ^ globalSizing_, ^globalTargets_, ^fallback_;
    NumericUpDown ^ lots_, ^risk_, ^tpIndex_;
    Label ^ entryHint_, ^status_;

    static void SetNum(NumericUpDown^ n, double v) { n->Value = Decimal(Math::Max(Decimal::ToDouble(n->Minimum), Math::Min(Decimal::ToDouble(n->Maximum), v))); }

    void ShowBinding(JObj^ b) {
        tradeSymbol_ = J::Str(b, "tradeSymbol");
        ShowSymbol();
        String^ mode = J::Str(b, "entryMode", "wait_for_range");
        entry_->SelectedIndex = mode == "immediate_within_tolerance" ? 1 : mode == "explicit_pending_only" ? 2 : 0;
        JObj^ sizing = J::Obj(b, "sizing");
        globalSizing_->Checked = !J::Bool(b, "overrideSizing");
        sizingMode_->SelectedIndex = J::Str(sizing, "mode", "fixed_total_lots") == "risk_percent" ? 1 : 0;
        SetNum(lots_, J::Num(sizing, "lots", 0.01));
        SetNum(risk_, J::Num(sizing, "riskPercent", 0.5));
        JObj^ targets = J::Obj(b, "targets");
        globalTargets_->Checked = !J::Bool(b, "overrideTargets");
        targetMode_->SelectedIndex = J::Str(targets, "mode", "single") == "split" ? 1 : 0;
        SetNum(tpIndex_, J::Num(targets, "selectedIndex", 1));
        String^ al = "";
        for each (Object^ a in J::Arr(targets, "allocations")) al += (al == "" ? "" : ",") + J::Fmt(Convert::ToDouble(a));
        allocations_->Text = al;
        fallback_->Checked = J::Bool(targets, "singleTargetFallback");
        ShowApplicableFields();
    }

    void ShowSymbol() { symbol_->Text = String::IsNullOrEmpty(tradeSymbol_) ? "Each message's own symbol" : tradeSymbol_; }

    void OnChoice(Object^, EventArgs^) { ShowApplicableFields(); }

    // only the fields that apply to the current choices are shown
    void ShowApplicableFields() {
        const int mode = entry_->SelectedIndex;
        entryHint_->Text = mode == 1   ? "Only NOW / market messages that give an entry price. Entered at once if the price is within the symbol's Entry deviation "
                                         "of it, and if the message is no older than Max signal age (Execution policy)."
                           : mode == 2 ? "Only messages that say LIMIT or STOP. Placed as a pending order at the message's price, cancelled after Pending expiry "
                                         "(Execution policy). Your broker must support order expiry."
                                       : "Waits until the price is inside the message's entry range (a single price: within the symbol's Entry deviation), for up "
                                         "to Range expiry (Execution policy).";
        const bool ownSizing = !globalSizing_->Checked;
        L::ShowField(sizingMode_, ownSizing);
        L::ShowField(lots_, ownSizing && sizingMode_->SelectedIndex != 1);
        L::ShowField(risk_, ownSizing && sizingMode_->SelectedIndex == 1);
        const bool ownTargets = !globalTargets_->Checked;
        const bool split = targetMode_->SelectedIndex == 1;
        L::ShowField(targetMode_, ownTargets);
        L::ShowField(allocations_, ownTargets && split);
        L::ShowField(fallback_, ownTargets && split);
        L::ShowField(tpIndex_, ownTargets && (!split || fallback_->Checked));
    }

    void OnStatus(bool ok, Object^ result, String^) {
        if (!ok) return;
        JObj^ r = J::Obj(result);
        catalog_->Clear();
        array<Object^>^ names = J::Arr(r, "catalog");
        if (names->Length == 0) names = J::Arr(r, "symbols");
        for each (Object^ o in names) {
            String^ n = dynamic_cast<String^>(o) != nullptr ? o->ToString() : J::Str(J::Obj(o), "name");
            if (n != "") catalog_->Add(n);
        }
    }

    void OnChooseSymbol(Object^, EventArgs^) {
        if (catalog_->Count == 0) {
            status_->Text = "Connect MetaTrader first (MetaTrader page): its symbols are listed here.";
            return;
        }
        SymbolPickerDialog^ d = gcnew SymbolPickerDialog(catalog_, tradeSymbol_, true);
        if (d->ShowDialog(this) != System::Windows::Forms::DialogResult::OK || d->Selected == nullptr) return;
        tradeSymbol_ = d->Selected;
        ShowSymbol();
    }

    void OnSave(Object^, EventArgs^) {
        List<Object^>^ allocs = J::List();
        double sum = 0;
        const bool ownTargets = !globalTargets_->Checked, split = targetMode_->SelectedIndex == 1;
        for each (String^ part in allocations_->Text->Split(gcnew array<wchar_t>{',', ';', ' '}, StringSplitOptions::RemoveEmptyEntries)) {
            double v;
            if (!J::TryNum(part, v) || v <= 0) {
                if (!(ownTargets && split)) continue;  // the field is hidden and unused: a stray value doesn't block Save
                status_->Text = "Split allocations must be positive numbers separated by commas, e.g. 50,30,20.";
                return;
            }
            allocs->Add(v);
            sum += v;
        }
        if (ownTargets && split && (allocs->Count < 2 || Math::Abs(sum - 100) > 1e-6)) {
            status_->Text = "Split targets need at least two percentages that add up to 100.";
            return;
        }
        JObj^ p = J::New();
        p["chatId"] = J::Str(binding_, "chatId");
        p["tradeSymbol"] = tradeSymbol_ == nullptr ? "" : tradeSymbol_;
        p["entryMode"] = entry_->SelectedIndex == 1 ? "immediate_within_tolerance" : entry_->SelectedIndex == 2 ? "explicit_pending_only" : "wait_for_range";
        p["overrideSizing"] = !globalSizing_->Checked;
        JObj^ sizing = J::New();
        sizing["mode"] = sizingMode_->SelectedIndex == 1 ? "risk_percent" : "fixed_total_lots";
        sizing["lots"] = Decimal::ToDouble(lots_->Value);
        sizing["riskPercent"] = Decimal::ToDouble(risk_->Value);
        p["sizing"] = sizing;
        p["overrideTargets"] = ownTargets;
        JObj^ targets = J::New();
        targets["mode"] = split ? "split" : "single";
        targets["selectedIndex"] = Decimal::ToInt32(tpIndex_->Value);
        targets["allocations"] = allocs;
        targets["singleTargetFallback"] = fallback_->Checked;
        p["targets"] = targets;
        status_->Text = "Saving...";
        client_->Call("source.update", p, gcnew ResponseHandler(this, &ChannelDialog::OnSaved));
    }

    void OnSaved(bool ok, Object^, String^ error) {
        if (!ok) {
            status_->Text = "Not saved: " + error;
            return;
        }
        DialogResult = System::Windows::Forms::DialogResult::OK;
        Close();
    }
};

}  // namespace PipkiteUI
