#pragma once
// One-time page tour: the window dims, a ring marks one control at a time and a card explains it.
// Steps are the controls' own tooltips, so every button with help text is covered, including new ones.

#include "Controls.h"

namespace PipkiteUI {

public ref class TourStep {
public:
    Control^ Target;
    String^ Title;
    String^ Body;
    TourStep(Control^ target, String^ title, String^ body) {
        Target = target;
        Title = title;
        Body = body;
    }
};

public delegate void TourFinished(bool completed);

// Borderless window over the main form that never takes focus (the dim layer and the ring).
ref class TourLayer : public Form {
public:
    TourLayer() {
        FormBorderStyle = Windows::Forms::FormBorderStyle::None;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition::Manual;
    }

protected:
    property bool ShowWithoutActivation {
        virtual bool get() override { return true; }
    }
};

public ref class Tour : public Form {
public:
    // Every visible control under `root` that has a tooltip, top to bottom, then left to right.
    static List<TourStep^>^ StepsFrom(Control^ root) {
        List<Control^>^ found = gcnew List<Control^>();
        Collect(root, found);
        found->Sort(gcnew Comparison<Control^>(&Tour::ByPosition));
        List<TourStep^>^ steps = gcnew List<TourStep^>();
        for each (Control^ c in found) steps->Add(gcnew TourStep(c, TitleOf(c), L::TipOf(c)));
        return steps;
    }

    static Tour^ Start(Form^ owner, String^ page, List<TourStep^>^ steps, TourFinished^ finished) {
        Tour^ t = gcnew Tour(owner, page, steps, finished);
        t->ShowStep(0);
        return t;
    }

    // Close without marking the tour as seen (window hidden or minimized).
    void Cancel() { End(false); }

protected:
    virtual void OnKeyDown(KeyEventArgs^ e) override {
        if (e->KeyCode == Keys::Escape) End(true);
        else if (e->KeyCode == Keys::Right) Go(1);
        else if (e->KeyCode == Keys::Left) Go(-1);
        else return Form::OnKeyDown(e);
        e->Handled = true;
    }

    virtual void OnPaint(PaintEventArgs^ e) override {
        Form::OnPaint(e);
        e->Graphics->DrawRectangle(Theme::BorderPen, 0, 0, ClientSize.Width - 1, ClientSize.Height - 1);
    }

    virtual void OnHandleCreated(EventArgs^ e) override {
        Form::OnHandleCreated(e);
        int round = 2;  // DWMWCP_ROUND: rounded corners on Windows 11, ignored before
        NativeUi::DwmSetWindowAttribute(Handle, 33, round, 4);
    }

private:
    Form^ owner_;
    String^ page_;
    List<TourStep^>^ steps_;
    TourFinished^ finished_;
    int index_;
    bool ended_;
    TourLayer ^ shade_, ^ring_;
    Label ^ counter_, ^title_, ^body_;
    Button ^ back_, ^next_;
    EventHandler^ follow_;

    Tour(Form^ owner, String^ page, List<TourStep^>^ steps, TourFinished^ finished) {
        owner_ = owner;
        page_ = page;
        steps_ = steps;
        finished_ = finished;
        FormBorderStyle = Windows::Forms::FormBorderStyle::None;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition::Manual;
        KeyPreview = true;
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        AutoSize = true;
        AutoSizeMode = Windows::Forms::AutoSizeMode::GrowAndShrink;
        Font = Theme::Body;
        Tag = "surface";
        AccessibleName = "Page tour";

        FlowLayoutPanel^ col = gcnew FlowLayoutPanel();
        col->FlowDirection = FlowDirection::TopDown;
        col->WrapContents = false;
        col->AutoSize = true;
        col->AutoSizeMode = Windows::Forms::AutoSizeMode::GrowAndShrink;
        col->Margin = Windows::Forms::Padding(0);
        col->Padding = Windows::Forms::Padding(20, 16, 20, 12);
        counter_ = L::Text("", "muted");
        counter_->Font = Theme::Small;
        counter_->Margin = Windows::Forms::Padding(0, 0, 0, 6);
        title_ = L::Wrap("", "h2", 360);
        title_->Margin = Windows::Forms::Padding(0, 0, 0, 6);
        body_ = L::Wrap("", nullptr, 360);
        body_->Margin = Windows::Forms::Padding(0, 0, 0, 16);

        Button^ skip = L::Btn("Skip tour", nullptr, "Close this tour. It won't show again on this page.");
        skip->Click += gcnew EventHandler(this, &Tour::OnSkip);
        back_ = L::Btn("Back", nullptr, "Previous step (Left arrow).");
        back_->Click += gcnew EventHandler(this, &Tour::OnBack);
        next_ = L::Btn("Next", "accent", "Next step (Right arrow or Enter).");
        next_->Click += gcnew EventHandler(this, &Tour::OnNext);
        next_->Margin = Windows::Forms::Padding(0, 4, 0, 4);
        FlowLayoutPanel^ right = L::Row(back_, next_);
        right->Dock = DockStyle::None;
        right->WrapContents = false;
        right->Anchor = AnchorStyles::Right;
        TableLayoutPanel^ buttons = gcnew TableLayoutPanel();
        buttons->ColumnCount = 2;
        buttons->AutoSize = true;
        buttons->AutoSizeMode = Windows::Forms::AutoSizeMode::GrowAndShrink;
        buttons->MinimumSize = Drawing::Size(360, 0);
        buttons->Margin = Windows::Forms::Padding(0);
        buttons->ColumnStyles->Add(gcnew ColumnStyle(SizeType::Percent, 100));
        buttons->ColumnStyles->Add(gcnew ColumnStyle(SizeType::AutoSize));
        buttons->Controls->Add(skip, 0, 0);
        buttons->Controls->Add(right, 1, 0);

        col->Controls->Add(counter_);
        col->Controls->Add(title_);
        col->Controls->Add(body_);
        col->Controls->Add(buttons);
        Controls->Add(col);
        AcceptButton = next_;
        Theme::Apply(this);

        shade_ = gcnew TourLayer();
        shade_->BackColor = Color::Black;
        shade_->Opacity = Theme::Dark ? 0.6 : 0.45;
        ring_ = gcnew TourLayer();
        ring_->BackColor = Theme::Accent;

        follow_ = gcnew EventHandler(this, &Tour::OnOwnerMoved);
        owner_->Move += follow_;
        owner_->Resize += follow_;
        owner_->VisibleChanged += follow_;
    }

    static void Collect(Control^ c, List<Control^>^ found) {
        for each (Control^ child in c->Controls) {
            if (!child->Visible) continue;
            // "?" markers repeat their field's help, and the sidebar buttons only hold a shortcut.
            const bool skip = dynamic_cast<Label^>(child) != nullptr || dynamic_cast<NavButton^>(child) != nullptr;
            if (!skip && !String::IsNullOrWhiteSpace(L::TipOf(child))) {
                found->Add(child);
                continue;  // its inner parts are the same control
            }
            Collect(child, found);
        }
    }

    // Rows of about 16 px, then left to right. A key, not a tolerance, so the sort stays consistent.
    static int ByPosition(Control^ a, Control^ b) {
        Rectangle ra = a->RectangleToScreen(a->ClientRectangle), rb = b->RectangleToScreen(b->ClientRectangle);
        const int rowA = (ra.Top + ra.Height / 2) / 16, rowB = (rb.Top + rb.Height / 2) / 16;
        return rowA != rowB ? rowA.CompareTo(rowB) : ra.Left.CompareTo(rb.Left);
    }

    static String^ TitleOf(Control^ c) {
        if (dynamic_cast<ButtonBase^>(c) != nullptr) return c->Text->Replace("&&", "&");
        if (!String::IsNullOrWhiteSpace(c->AccessibleName)) return c->AccessibleName;
        const bool input = dynamic_cast<TextBoxBase^>(c) != nullptr || dynamic_cast<ComboBox^>(c) != nullptr || dynamic_cast<UpDownBase^>(c) != nullptr ||
                           dynamic_cast<DataGridView^>(c) != nullptr;
        return input ? "" : c->Text;  // never repeat what was typed into a box
    }

    void OnSkip(Object^, EventArgs^) { End(true); }
    void OnBack(Object^, EventArgs^) { Go(-1); }
    void OnNext(Object^, EventArgs^) { Go(1); }

    void Go(int step) {
        int i = index_ + step;
        // A control can hide while the tour runs (data changed); skip it.
        while (i >= 0 && i < steps_->Count && !steps_[i]->Target->Visible) i += step;
        if (i >= steps_->Count) return End(true);
        if (i >= 0) ShowStep(i);
    }

    void ShowStep(int i) {
        index_ = i;
        TourStep^ s = steps_[i];
        counter_->Text = page_ + "  ·  " + (i + 1) + " of " + steps_->Count;
        title_->Text = s->Title;
        title_->Visible = s->Title != "";
        body_->Text = s->Body;
        back_->Enabled = i > 0;
        next_->Text = i == steps_->Count - 1 ? "Done" : "Next";
        for (Control^ p = s->Target->Parent; p != nullptr; p = p->Parent) {
            ScrollableControl^ sc = dynamic_cast<ScrollableControl^>(p);
            if (sc != nullptr && sc->AutoScroll) {
                sc->ScrollControlIntoView(s->Target);
                break;
            }
        }
        Place();
        next_->Focus();
    }

    // Visible part of the target, in screen coordinates.
    Rectangle TargetRect() {
        Control^ t = steps_[index_]->Target;
        Rectangle r = t->RectangleToScreen(t->ClientRectangle);
        for (Control^ p = t->Parent; p != nullptr; p = p->Parent) r = Rectangle::Intersect(r, p->RectangleToScreen(p->ClientRectangle));
        return r;
    }

    void Place() {
        if (ended_) return;
        Rectangle area = owner_->RectangleToScreen(owner_->ClientRectangle);
        Rectangle hole = TargetRect();
        const int pad = Theme::Scale(owner_, 6), ringW = Theme::Scale(owner_, 2), gap = Theme::Scale(owner_, 14);
        hole.Inflate(pad, pad);

        // dim layer with a hole over the target; the target stays clickable through the hole
        shade_->Bounds = area;
        Drawing::Region^ dim = gcnew Drawing::Region(Rectangle(0, 0, area.Width, area.Height));
        Rectangle local(hole.X - area.X, hole.Y - area.Y, hole.Width, hole.Height);
        if (!hole.IsEmpty) dim->Exclude(Theme::RoundRect(RectangleF((float)local.X, (float)local.Y, (float)local.Width, (float)local.Height), (float)Theme::Radius));
        shade_->Region = dim;

        // ring around the hole
        if (hole.IsEmpty) ring_->Hide();
        else {
            Rectangle outer = hole;
            outer.Inflate(ringW, ringW);
            ring_->Bounds = outer;
            Drawing::Region^ shape = gcnew Drawing::Region(Theme::RoundRect(RectangleF(0, 0, (float)outer.Width, (float)outer.Height), (float)(Theme::Radius + ringW)));
            shape->Exclude(Theme::RoundRect(RectangleF((float)ringW, (float)ringW, (float)hole.Width, (float)hole.Height), (float)Theme::Radius));
            ring_->Region = shape;
        }

        if (!shade_->Visible) shade_->Show(owner_);
        if (!hole.IsEmpty && !ring_->Visible) {
            if (ring_->Owner == nullptr) ring_->Show(owner_);
            else ring_->Show();
        }
        if (!Visible) Show(owner_);
        PerformLayout();

        // card below the target, else above, else beside it; always inside the window
        Drawing::Size size = Size;
        Point at(hole.Left, hole.Bottom + gap);
        if (at.Y + size.Height > area.Bottom) at = Point(hole.Left, hole.Top - gap - size.Height);
        if (at.Y < area.Top) {
            at = Point(hole.Right + gap, hole.Top);
            if (at.X + size.Width > area.Right) at.X = hole.Left - gap - size.Width;
        }
        at.X = Math::Max(area.Left + gap, Math::Min(at.X, area.Right - size.Width - gap));
        at.Y = Math::Max(area.Top + gap, Math::Min(at.Y, area.Bottom - size.Height - gap));
        Location = at;
        ring_->BringToFront();
        BringToFront();
        Activate();
    }

    void OnOwnerMoved(Object^, EventArgs^) {
        if (ended_) return;
        if (!owner_->Visible || owner_->WindowState == FormWindowState::Minimized) return End(false);
        Place();
    }

    void End(bool completed) {
        if (ended_) return;
        ended_ = true;
        owner_->Move -= follow_;
        owner_->Resize -= follow_;
        owner_->VisibleChanged -= follow_;
        shade_->Close();
        ring_->Close();
        Close();
        if (finished_ != nullptr) finished_(completed);
    }
};

}  // namespace PipkiteUI
