#pragma once
// History: searchable signals with state, reason and parser; virtualized (VirtualMode) so 100,000+ rows
// scroll smoothly with pages fetched on demand; detail view with the processing timeline; export.

#include "DashboardPage.h"

namespace PipkiteUI {

public ref class HistoryPage : public PageBase {
public:
    HistoryPage() {
        pages_ = gcnew Dictionary<int, array<Object^>^>();
        requested_ = gcnew HashSet<int>();
        FlowLayoutPanel^ bar = L::Row();
        bar->Padding = Windows::Forms::Padding(16, 0, 16, 4);
        search_ = L::Box(280);
        search_->AccessibleName = "Search history";
        search_->KeyDown += gcnew KeyEventHandler(this, &HistoryPage::OnSearchKey);
        state_ = L::Combo(150, gcnew array<String^>{"(any state)", "Dispatched", "Previewed", "Waiting", "Review", "Rejected", "Expired", "Historical", "Planned", "Validated"});
        Button^ go = L::Btn("Search", "accent", "Find past messages by text and state.");
        go->Click += gcnew EventHandler(this, &HistoryPage::OnSearch);
        Button^ csv = L::Btn("Export CSV...", nullptr, "Save the current results as a spreadsheet-friendly CSV file.");
        csv->Click += gcnew EventHandler(this, &HistoryPage::OnExportCsv);
        Button^ json = L::Btn("Export JSON...", nullptr, "Save the current results as a JSON file.");
        json->Click += gcnew EventHandler(this, &HistoryPage::OnExportJson);
        count_ = L::Text("", "muted");
        bar->Controls->Add(L::Text("Search"));
        bar->Controls->Add(search_);
        bar->Controls->Add(state_);
        bar->Controls->Add(go);
        bar->Controls->Add(csv);
        bar->Controls->Add(json);
        bar->Controls->Add(count_);

        grid_ = gcnew BufferedGrid();
        grid_->Dock = DockStyle::Fill;
        grid_->VirtualMode = true;
        grid_->AccessibleName = "Signal history";
        grid_->AddCol("at", "Time", 140);
        grid_->AddCol("source", "Source", 150);
        grid_->AddCol("symbol", "Symbol", 80);
        grid_->AddCol("side", "Side", 60);
        grid_->AddCol("state", "State", 90);
        grid_->AddCol("status", "Parse status", 95);
        grid_->AddCol("parser", "Parser", 150);
        grid_->AddCol("preview", "Message", 280);
        DataGridViewTextBoxColumn^ r = grid_->AddCol("reason", "Reason", 300);
        r->AutoSizeMode = DataGridViewAutoSizeColumnMode::Fill;
        grid_->CellValueNeeded += gcnew DataGridViewCellValueEventHandler(this, &HistoryPage::OnCellValueNeeded);
        grid_->CellDoubleClick += gcnew DataGridViewCellEventHandler(this, &HistoryPage::OnDoubleClick);
        grid_->CellFormatting += gcnew DataGridViewCellFormattingEventHandler(this, &HistoryPage::OnFormat);
        grid_->KeyDown += gcnew KeyEventHandler(this, &HistoryPage::OnGridKey);
        Panel^ body = gcnew Panel();
        body->Dock = DockStyle::Fill;
        body->Padding = Windows::Forms::Padding(16, 0, 16, 12);
        body->Controls->Add(grid_);
        Controls->Add(body);
        Controls->Add(bar);
        Header("History", "Every received message, how it was read and what was done. Old messages never become orders.", this);
    }

    virtual void OnActivated() override {
        if (Client != nullptr && Client->Connected) Reload();
    }

private:
    TextBox^ search_;
    ComboBox^ state_;
    Label^ count_;
    BufferedGrid^ grid_;
    Dictionary<int, array<Object^>^>^ pages_;
    HashSet<int>^ requested_;
    int generation_;
    literal int PageSize = 100;

    JObj^ Filter() {
        JObj^ p = J::New();
        p["text"] = search_->Text->Trim();
        p["state"] = state_->SelectedIndex > 0 ? state_->Text : "";
        return p;
    }

    void Reload() {
        ++generation_;
        pages_->Clear();
        requested_->Clear();
        Client->Call("history.count", Filter(), gcnew ResponseHandler(this, &HistoryPage::OnCount));
    }

    void OnCount(bool ok, Object^ result, String^ error) {
        if (!ok) return ShowError(error);
        Int64 n = J::Int(J::Obj(result), "count");
        grid_->RowCount = 0;
        grid_->RowCount = (int)Math::Min(n, (Int64)Int32::MaxValue);
        count_->Text = n.ToString("N0") + " record(s)";
    }

    ref class PageRequest {
    public:
        HistoryPage^ owner;
        int page;
        int generation;
        void Done(bool ok, Object^ result, String^ error) { owner->OnPage(page, generation, ok, result); }
    };

internal:
    void OnPage(int page, int generation, bool ok, Object^ result) {
        requested_->Remove(page);
        if (!ok || generation != generation_) return;
        pages_[page] = J::Arr(result);
        if (pages_->Count > 50) {  // bounded cache: the UI never retains unlimited history
            for each (int k in gcnew List<int>(pages_->Keys))
                if (Math::Abs(k - page) > 10) pages_->Remove(k);
        }
        grid_->Invalidate();
    }

private:
    JObj^ RowAt(int row) {
        int page = row / PageSize;
        array<Object^>^ items;
        if (pages_->TryGetValue(page, items)) {
            int i = row % PageSize;
            return i < items->Length ? J::Obj(items[i]) : nullptr;
        }
        if (!requested_->Contains(page)) {
            requested_->Add(page);
            JObj^ p = Filter();
            p["offset"] = page * PageSize;
            p["limit"] = PageSize;
            PageRequest^ r = gcnew PageRequest();
            r->owner = this;
            r->page = page;
            r->generation = generation_;
            Client->Call("history.page", p, gcnew ResponseHandler(r, &PageRequest::Done));
        }
        return nullptr;
    }

    void OnCellValueNeeded(Object^, DataGridViewCellValueEventArgs^ e) {
        JObj^ r = RowAt(e->RowIndex);
        if (r == nullptr) {
            e->Value = e->ColumnIndex == 0 ? "loading..." : "";
            return;
        }
        String^ col = grid_->Columns[e->ColumnIndex]->Name;
        if (col == "at") e->Value = J::Time(J::Int(r, "at"));
        else if (col == "preview") e->Value = J::Str(r, "preview")->Replace("\n", " | ");
        else e->Value = J::Str(r, col);
    }

    void OnFormat(Object^, DataGridViewCellFormattingEventArgs^ e) {
        String^ col = grid_->Columns[e->ColumnIndex]->Name;
        if (col == "state" || col == "status") e->CellStyle->ForeColor = Theme::StateColor(dynamic_cast<String^>(e->Value));
    }

    void Open(int row) {
        JObj^ r = RowAt(row);
        if (r != nullptr && DashboardPage::OpenDetail != nullptr) DashboardPage::OpenDetail(J::Str(r, "chatId"), J::Str(r, "messageId"), (int)J::Int(r, "revision"));
    }
    void OnDoubleClick(Object^, DataGridViewCellEventArgs^ e) { Open(e->RowIndex); }
    void OnGridKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode == Keys::Enter && grid_->CurrentCell != nullptr) {
            Open(grid_->CurrentCell->RowIndex);
            e->Handled = true;
        }
    }
    void OnSearch(Object^, EventArgs^) { Reload(); }
    void OnSearchKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode == Keys::Enter) {
            Reload();
            e->SuppressKeyPress = true;
        }
    }

    void Export(bool csv) {
        SaveFileDialog^ d = gcnew SaveFileDialog();
        d->Filter = csv ? "CSV (*.csv)|*.csv" : "JSON (*.json)|*.json";
        d->FileName = csv ? "pipkite-history.csv" : "pipkite-history.json";
        if (d->ShowDialog(FindForm()) != DialogResult::OK) return;
        JObj^ p = J::New();
        p["path"] = d->FileName;
        p["csv"] = csv;
        Client->Call("data.exportReport", p, gcnew ResponseHandler(this, &HistoryPage::OnExported));
    }
    void OnExportCsv(Object^, EventArgs^) { Export(true); }
    void OnExportJson(Object^, EventArgs^) { Export(false); }
    void OnExported(bool ok, Object^, String^ error) {
        if (!ok) return ShowError(error);
        ShowInfo("Export", "Report exported (no secrets are included).");
    }
};

}  // namespace PipkiteUI
