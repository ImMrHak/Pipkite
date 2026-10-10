#pragma once
// Shared controls built on the Theme tokens. Design rules: flat surfaces, one accent color, no gradients
// or glows, sentence-case text, 30 px controls, 8 px radius cards. Performance rules: no timers or
// animation, double-buffered painting, cached pens/brushes, Invalidate() only when a value changes.
// Every control keeps an accessible name and a visible keyboard focus cue.

#include "Json.h"
#include "Theme.h"

using namespace System;
using namespace System::Collections::Generic;
using namespace System::ComponentModel;
using namespace System::Drawing;
using namespace System::Drawing::Drawing2D;
using namespace System::Windows::Forms;

namespace PipkiteUI {

inline float Dpi(Control^ c, float v) { return v * c->DeviceDpi / 96.0f; }

// Themed context menus (tray, table column chooser).
public ref class ThemedMenuRenderer : public ToolStripProfessionalRenderer {
protected:
    virtual void OnRenderToolStripBackground(ToolStripRenderEventArgs^ e) override { e->Graphics->FillRectangle(Theme::SurfaceBrush, e->AffectedBounds); }
    virtual void OnRenderToolStripBorder(ToolStripRenderEventArgs^ e) override {
        e->Graphics->DrawRectangle(Theme::InputPen, 0, 0, e->ToolStrip->Width - 1, e->ToolStrip->Height - 1);
    }
    virtual void OnRenderImageMargin(ToolStripRenderEventArgs^ e) override { e->Graphics->FillRectangle(Theme::SurfaceBrush, e->AffectedBounds); }
    virtual void OnRenderMenuItemBackground(ToolStripItemRenderEventArgs^ e) override {
        Rectangle r(2, 1, e->Item->Width - 4, e->Item->Height - 2);
        e->Graphics->FillRectangle(e->Item->Selected && e->Item->Enabled ? Theme::HoverBrush : Theme::SurfaceBrush, r);
    }
    virtual void OnRenderItemText(ToolStripItemTextRenderEventArgs^ e) override {
        e->TextColor = e->Item->Enabled ? Theme::Text : Theme::TextMuted;
        ToolStripProfessionalRenderer::OnRenderItemText(e);
    }
    virtual void OnRenderSeparator(ToolStripSeparatorRenderEventArgs^ e) override {
        const int y = e->Item->Height / 2;
        e->Graphics->DrawLine(Theme::BorderPen, 8, y, e->Item->Width - 8, y);
    }
    virtual void OnRenderItemCheck(ToolStripItemImageRenderEventArgs^ e) override {
        Rectangle r = e->ImageRectangle;
        const int s = Math::Min(r.Width, r.Height);
        Theme::PaintCheck(e->Graphics, Rectangle(r.X + (r.Width - s) / 2, r.Y + (r.Height - s) / 2, s, s), true, true, false);
    }
};

// Accessibility object for owner-drawn buttons and check boxes: Windows' generic proxy reports owner-drawn
// BUTTON windows as a plain pane, so screen readers and UI Automation get the role, state and default
// action (Press / Check) from here instead.
public ref class OwnerDrawnAccessible : public Control::ControlAccessibleObject {
public:
    OwnerDrawnAccessible(Control^ owner, Windows::Forms::AccessibleRole role) : Control::ControlAccessibleObject(owner), role_(role) {}
    property Windows::Forms::AccessibleRole Role {
        virtual Windows::Forms::AccessibleRole get() override { return role_; }
    }
    property AccessibleStates State {
        virtual AccessibleStates get() override {
            AccessibleStates s = Control::ControlAccessibleObject::State | AccessibleStates::Focusable;
            CheckBox^ cb = dynamic_cast<CheckBox^>(Owner);
            if (cb != nullptr && cb->Checked) s = s | AccessibleStates::Checked;
            if (Owner->Focused) s = s | AccessibleStates::Focused;
            if (!Owner->Enabled) s = s | AccessibleStates::Unavailable;
            return s;
        }
    }
    property String^ DefaultAction {
        virtual String^ get() override { return role_ == Windows::Forms::AccessibleRole::CheckButton ? "Toggle" : "Press"; }
    }
    virtual void DoDefaultAction() override {
        ButtonBase^ b = dynamic_cast<ButtonBase^>(Owner);
        if (b == nullptr || !b->Enabled) return;
        CheckBox^ cb = dynamic_cast<CheckBox^>(b);
        if (cb != nullptr) cb->Checked = !cb->Checked;
        else safe_cast<Button^>(b)->PerformClick();
    }

private:
    Windows::Forms::AccessibleRole role_;
};

// Sidebar navigation item: icon, label, soft accent fill and a short accent bar when selected.
public ref class NavButton : public Button {
public:
    String^ Glyph;
    bool Selected;
    NavButton(String^ glyph, String^ text) {
        Glyph = glyph;
        Text = text;
        AccessibleName = text;
        Height = 40;
        Dock = DockStyle::Top;
        FlatStyle = Windows::Forms::FlatStyle::Flat;
        FlatAppearance->BorderSize = 0;
        Cursor = Cursors::Hand;
        Tag = "keep";
        AccessibleRole = Windows::Forms::AccessibleRole::PushButton;
        SetStyle(ControlStyles::OptimizedDoubleBuffer | ControlStyles::AllPaintingInWmPaint | ControlStyles::UserPaint, true);
    }

protected:
    virtual AccessibleObject^ CreateAccessibilityInstance() override { return gcnew OwnerDrawnAccessible(this, Windows::Forms::AccessibleRole::PushButton); }
    virtual void OnPaint(PaintEventArgs^ e) override {
        Graphics^ g = e->Graphics;
        g->Clear(Theme::Surface);
        g->SmoothingMode = SmoothingMode::AntiAlias;
        RectangleF pill(Dpi(this, 10), Dpi(this, 2), Width - Dpi(this, 20), Height - Dpi(this, 4));
        if (Selected || hover_) g->FillPath(Selected ? Theme::AccentSoftBrush : Theme::HoverBrush, Theme::RoundRect(pill, Dpi(this, 6)));
        if (Selected) {
            const float bh = Dpi(this, 16);
            g->FillPath(Theme::AccentBrush, Theme::RoundRect(RectangleF(pill.X, pill.Y + (pill.Height - bh) / 2, Dpi(this, 3), bh), Dpi(this, 1.5f)));
        }
        const int pad = (int)(pill.X + Dpi(this, 14));
        TextRenderer::DrawText(g, Glyph, Theme::Icon, Rectangle(pad, 0, (int)Dpi(this, 22), Height), Selected ? Theme::Accent : Theme::TextMuted,
                               TextFormatFlags::VerticalCenter | TextFormatFlags::Left);
        TextRenderer::DrawText(g, Text, Selected ? Theme::BodyBold : Theme::Body, Rectangle(pad + (int)Dpi(this, 30), 0, Width - pad - (int)Dpi(this, 40), Height),
                               Selected ? Theme::Text : Theme::TextMuted, TextFormatFlags::VerticalCenter | TextFormatFlags::Left | TextFormatFlags::EndEllipsis);
        if (Focused && ShowFocusCues) g->DrawPath(Theme::FocusPen, Theme::RoundRect(pill, Dpi(this, 6)));
    }
    virtual void OnMouseEnter(EventArgs^ e) override {
        Button::OnMouseEnter(e);
        hover_ = true;
        Invalidate();
    }
    virtual void OnMouseLeave(EventArgs^ e) override {
        Button::OnMouseLeave(e);
        hover_ = false;
        Invalidate();
    }

private:
    bool hover_;
};

// Button. Tag "accent" = primary (solid accent), "danger" = destructive (red text), otherwise secondary.
// Always ControlHeight tall; width follows the text.
public ref class NeoButton : public Button {
public:
    NeoButton() {
        SetStyle(ControlStyles::OptimizedDoubleBuffer | ControlStyles::AllPaintingInWmPaint | ControlStyles::UserPaint | ControlStyles::SupportsTransparentBackColor, true);
        Cursor = Cursors::Hand;
        FlatStyle = Windows::Forms::FlatStyle::Flat;
        FlatAppearance->BorderSize = 0;
        AccessibleRole = Windows::Forms::AccessibleRole::PushButton;  // owner-drawn buttons otherwise surface as a pane
    }
    virtual Drawing::Size GetPreferredSize(Drawing::Size) override {
        String^ kind = dynamic_cast<String^>(Tag);
        Drawing::Size t = TextRenderer::MeasureText(Text, kind == "accent" ? Theme::BodyBold : Theme::Body);
        return Drawing::Size(t.Width + Theme::Scale(this, 30), Theme::Scale(this, Theme::ControlHeight));
    }

protected:
    virtual AccessibleObject^ CreateAccessibilityInstance() override { return gcnew OwnerDrawnAccessible(this, Windows::Forms::AccessibleRole::PushButton); }
    virtual void OnPaint(PaintEventArgs^ e) override {
        Graphics^ g = e->Graphics;
        g->Clear(Parent != nullptr ? Parent->BackColor : Theme::Bg);
        g->SmoothingMode = SmoothingMode::AntiAlias;
        String^ kind = dynamic_cast<String^>(Tag);
        RectangleF r(0.5f, 0.5f, Width - 1.0f, Height - 1.0f);
        GraphicsPath^ path = Theme::RoundRect(r, Dpi(this, 6));
        Color text;
        if (kind == "accent") {
            g->FillPath(Enabled ? (hover_ || pressed_ ? gcnew SolidBrush(Theme::AccentHover) : Theme::AccentBrush) : Theme::HoverBrush, path);
            text = Enabled ? Theme::AccentText : Theme::TextMuted;
        } else {
            g->FillPath(pressed_ ? Theme::HoverBrush : hover_ ? Theme::HoverBrush : Theme::Surface2Brush, path);
            g->DrawPath(Theme::InputPen, path);
            text = !Enabled ? Theme::TextMuted : kind == "danger" ? Theme::Danger : Theme::Text;
        }
        TextRenderer::DrawText(g, Text, kind == "accent" ? Theme::BodyBold : Theme::Body, ClientRectangle, text,
                               TextFormatFlags::HorizontalCenter | TextFormatFlags::VerticalCenter | TextFormatFlags::EndEllipsis | TextFormatFlags::NoPrefix);
        if (Focused && ShowFocusCues) g->DrawPath(gcnew Pen(Theme::Accent, 2.0f), Theme::RoundRect(RectangleF(1.5f, 1.5f, Width - 3.0f, Height - 3.0f), Dpi(this, 5)));
    }
    virtual void OnMouseEnter(EventArgs^ e) override { Button::OnMouseEnter(e); hover_ = true; Invalidate(); }
    virtual void OnMouseLeave(EventArgs^ e) override { Button::OnMouseLeave(e); hover_ = false; pressed_ = false; Invalidate(); }
    virtual void OnMouseDown(MouseEventArgs^ e) override { Button::OnMouseDown(e); pressed_ = true; Invalidate(); }
    virtual void OnMouseUp(MouseEventArgs^ e) override { Button::OnMouseUp(e); pressed_ = false; Invalidate(); }
    virtual void OnTextChanged(EventArgs^ e) override {
        Button::OnTextChanged(e);
        Invalidate();
    }

private:
    bool hover_, pressed_;
};

// Card section: filled surface, hairline border, title at the top-left.
public ref class SectionBox : public GroupBox {
public:
    SectionBox() {
        SetStyle(ControlStyles::OptimizedDoubleBuffer | ControlStyles::AllPaintingInWmPaint | ControlStyles::UserPaint | ControlStyles::ResizeRedraw, true);
        Tag = "card";
    }

protected:
    virtual void OnPaint(PaintEventArgs^ e) override {
        Graphics^ g = e->Graphics;
        g->Clear(Parent != nullptr ? Parent->BackColor : Theme::Bg);
        g->SmoothingMode = SmoothingMode::AntiAlias;
        GraphicsPath^ p = Theme::RoundRect(RectangleF(0.5f, 0.5f, Width - 1.0f, Height - 1.0f), Dpi(this, Theme::Radius));
        g->FillPath(Theme::SurfaceBrush, p);
        g->DrawPath(Theme::BorderPen, p);
        TextRenderer::DrawText(g, Text, Theme::H2, Point((int)Dpi(this, 16), (int)Dpi(this, 14)), Theme::Text, TextFormatFlags::NoPrefix);
    }
};

// Status pill: tinted background, small dot and text (color is never the only signal).
public ref class Badge : public Control {
public:
    Color DotColor;
    Badge() {
        DotColor = Theme::TextMuted;
        SetStyle(ControlStyles::OptimizedDoubleBuffer | ControlStyles::AllPaintingInWmPaint | ControlStyles::UserPaint | ControlStyles::SupportsTransparentBackColor, true);
        Height = 26;
        Font = Theme::Caption;
        Tag = "keep";
    }
    void Set(String^ text, Color c) {
        if (Text == text && DotColor == c) return;  // repaint only on change
        Text = text;
        AccessibleName = text;
        DotColor = c;
        Width = TextRenderer::MeasureText(text, Font).Width + (int)Dpi(this, 32);
        Invalidate();
    }

protected:
    virtual void OnPaint(PaintEventArgs^ e) override {
        Graphics^ g = e->Graphics;
        g->SmoothingMode = SmoothingMode::AntiAlias;
        Color bg = Parent != nullptr ? Parent->BackColor : Theme::Bg;
        g->Clear(bg);
        RectangleF r(0.5f, 0.5f, Width - 1.0f, Height - 1.0f);
        g->FillPath(gcnew SolidBrush(Theme::Mix(bg, DotColor, Theme::Dark ? 0.16f : 0.12f)), Theme::RoundRect(r, r.Height / 2));
        const float d = Dpi(this, 7), cx = Dpi(this, 13), cy = r.Height / 2 + 0.5f;
        g->FillEllipse(gcnew SolidBrush(DotColor), cx - d / 2, cy - d / 2, d, d);
        TextRenderer::DrawText(g, Text, Font, Rectangle((int)Dpi(this, 22), 0, Width - (int)Dpi(this, 22), Height), Theme::Text,
                               TextFormatFlags::VerticalCenter | TextFormatFlags::Left | TextFormatFlags::NoPrefix);
    }
};

// Stat card: caption, optional icon, value, detail line and an optional sparkline.
public ref class StatCard : public Control {
public:
    StatCard(String^ caption) { Init(caption, nullptr); }
    StatCard(String^ caption, String^ glyph) { Init(caption, glyph); }
    void Init(String^ caption, String^ glyph) {
        caption_ = caption;
        glyph_ = glyph;
        value_ = "-";
        detail_ = "";
        valueColor_ = Theme::Text;
        spark_ = gcnew List<float>();
        SetStyle(ControlStyles::OptimizedDoubleBuffer | ControlStyles::AllPaintingInWmPaint | ControlStyles::UserPaint | ControlStyles::ResizeRedraw |
                     ControlStyles::SupportsTransparentBackColor,
                 true);
        Margin = Windows::Forms::Padding(0, 0, 12, 12);
        Size = Drawing::Size(180, 92);
        Tag = "keep";
        AccessibleName = caption;
        AccessibleRole = Windows::Forms::AccessibleRole::StaticText;
    }
    void Set(String^ value, String^ detail, Color valueColor) {
        if (value == value_ && detail == detail_ && valueColor == valueColor_) return;
        value_ = value;
        detail_ = detail;
        valueColor_ = valueColor;
        AccessibleDescription = value + " " + detail;
        Invalidate();
    }
    // Appends a sparkline sample (kept to the last 48).
    void AddSpark(double v) {
        if (Double::IsNaN(v) || Double::IsInfinity(v) || Math::Abs(v) > 1e30) return;
        if (spark_->Count > 0 && Math::Abs(spark_[spark_->Count - 1] - (float)v) < 1e-9f) return;
        spark_->Add((float)v);
        if (spark_->Count > 48) spark_->RemoveAt(0);
        Invalidate();
    }

protected:
    virtual void OnPaint(PaintEventArgs^ e) override {
        Graphics^ g = e->Graphics;
        g->Clear(Parent != nullptr ? Parent->BackColor : Theme::Bg);
        g->SmoothingMode = SmoothingMode::AntiAlias;
        GraphicsPath^ path = Theme::RoundRect(RectangleF(0.5f, 0.5f, Width - 1.0f, Height - 1.0f), Dpi(this, Theme::Radius));
        g->FillPath(Theme::SurfaceBrush, path);
        g->DrawPath(Theme::BorderPen, path);
        const int px = (int)Dpi(this, 16);
        const int iconCell = (int)Dpi(this, 22);
        const int capH = (int)Dpi(this, 18);
        const int capY = (int)Dpi(this, 12);
        // The caption always wins: only show the icon when the caption clearly fits beside it, so text can
        // never sit behind the glyph on small or high-DPI displays.
        const int captionW = TextRenderer::MeasureText(caption_, Theme::Small).Width;
        const bool showIcon = glyph_ != nullptr && captionW + iconCell + (int)Dpi(this, 8) <= Width - px * 2;
        const int iconW = showIcon ? iconCell : 0;
        TextRenderer::DrawText(g, caption_, Theme::Small, Rectangle(px, capY, Width - px * 2 - iconW, capH), Theme::TextMuted,
                               TextFormatFlags::Left | TextFormatFlags::EndEllipsis | TextFormatFlags::NoPrefix);
        if (showIcon) {
            Drawing::Region^ prev = g->Clip;
            g->SetClip(Rectangle(Width - px - iconCell, capY, iconCell, capH));  // keep the glyph inside its cell
            TextRenderer::DrawText(g, glyph_, Theme::Icon, Rectangle(Width - px - iconCell, capY, iconCell, capH), Theme::TextMuted,
                                   TextFormatFlags::Right | TextFormatFlags::VerticalCenter | TextFormatFlags::NoClipping);
            g->Clip = prev;
        }
        const int valueW = Width - px * 2 + 2;
        Drawing::Font^ vf = Theme::Big;
        if (TextRenderer::MeasureText(value_, vf).Width > valueW) vf = Theme::H2;
        if (TextRenderer::MeasureText(value_, vf).Width > valueW) vf = Theme::BodyBold;
        TextRenderer::DrawText(g, value_, vf, Rectangle(px - 1, (int)Dpi(this, 32), valueW, (int)Dpi(this, 30)), valueColor_,
                               TextFormatFlags::Left | TextFormatFlags::VerticalCenter | TextFormatFlags::EndEllipsis | TextFormatFlags::NoPrefix);
        const int sparkW = spark_->Count > 1 ? (int)(Width * 0.36f) : 0;
        TextRenderer::DrawText(g, detail_, Theme::Small, Rectangle(px, Height - (int)Dpi(this, 28), Width - px * 2 - sparkW, (int)Dpi(this, 18)), Theme::TextMuted,
                               TextFormatFlags::Left | TextFormatFlags::EndEllipsis | TextFormatFlags::NoPrefix);
        if (sparkW > 0) DrawSpark(g, RectangleF((float)(Width - sparkW - px), Height - Dpi(this, 30), (float)sparkW, Dpi(this, 18)));
    }

private:
    String ^ caption_, ^glyph_, ^value_, ^detail_;
    Color valueColor_;
    List<float>^ spark_;

    void DrawSpark(Graphics^ g, RectangleF area) {
        float mn = Single::MaxValue, mx = Single::MinValue;
        for each (float v in spark_) {
            mn = Math::Min(mn, v);
            mx = Math::Max(mx, v);
        }
        if (mx - mn < 1e-6f) {
            mx += 1;
            mn -= 1;
        }
        array<PointF>^ pts = gcnew array<PointF>(spark_->Count);
        for (int i = 0; i < spark_->Count; ++i)
            pts[i] = PointF(area.X + area.Width * i / (spark_->Count - 1), area.Bottom - (spark_[i] - mn) / (mx - mn) * area.Height);
        g->DrawLines(gcnew Pen(Theme::Accent, 1.5f), pts);
    }
};

// 1 px divider (horizontal or vertical, depending on docking).
public ref class Divider : public Control {
public:
    Divider() {
        SetStyle(ControlStyles::OptimizedDoubleBuffer | ControlStyles::AllPaintingInWmPaint | ControlStyles::UserPaint | ControlStyles::ResizeRedraw, true);
        Height = 1;
        Width = 1;
        Dock = DockStyle::Top;
        Tag = "keep";
        TabStop = false;
    }

protected:
    virtual void OnPaint(PaintEventArgs^ e) override { e->Graphics->FillRectangle(gcnew SolidBrush(Theme::Border), ClientRectangle); }
};

// Brand mark: accent rounded square with a relay chevron (drawn, no image resources).
public ref class BrandMark : public Control {
public:
    BrandMark() {
        SetStyle(ControlStyles::OptimizedDoubleBuffer | ControlStyles::AllPaintingInWmPaint | ControlStyles::UserPaint, true);
        Size = Drawing::Size(26, 26);
        Tag = "keep";
        TabStop = false;
        AccessibleName = "Pipkite";
    }

protected:
    virtual void OnPaint(PaintEventArgs^ e) override {
        Graphics^ g = e->Graphics;
        g->Clear(Parent != nullptr ? Parent->BackColor : Theme::Bg);
        g->SmoothingMode = SmoothingMode::AntiAlias;
        RectangleF r(0.5f, 0.5f, Width - 1.0f, Height - 1.0f);
        g->FillPath(Theme::AccentBrush, Theme::RoundRect(r, r.Width * 0.24f));
        Pen^ p = gcnew Pen(Theme::AccentText, Math::Max(2.0f, Width / 12.0f));
        p->StartCap = LineCap::Round;
        p->EndCap = LineCap::Round;
        p->LineJoin = LineJoin::Round;
        const float w = r.Width;
        for each (float off in gcnew array<float>{0.26f, 0.48f})
            g->DrawLines(p, gcnew array<PointF>{PointF(r.X + w * off, r.Y + w * 0.30f), PointF(r.X + w * (off + 0.19f), r.Y + w * 0.50f), PointF(r.X + w * off, r.Y + w * 0.70f)});
    }
};

// The one table style: header band, horizontal row lines, row hover, themed check boxes and drop-down
// cells, empty-state text and an optional right-click column chooser on the header.
public ref class BufferedGrid : public DataGridView {
public:
    String^ EmptyText;
    bool ColumnChooser;
    BufferedGrid() {
        DoubleBuffered = true;
        ReadOnly = true;
        AllowUserToAddRows = false;
        AllowUserToDeleteRows = false;
        AllowUserToResizeRows = false;
        SelectionMode = DataGridViewSelectionMode::FullRowSelect;
        MultiSelect = false;
        AutoSizeRowsMode = DataGridViewAutoSizeRowsMode::None;
        BorderStyle = Windows::Forms::BorderStyle::FixedSingle;
        RowTemplate->Height = 34;
        StandardTab = true;
        hoverRow_ = -1;
    }
    DataGridViewTextBoxColumn^ AddCol(String^ name, String^ header, int width) {
        DataGridViewTextBoxColumn^ c = gcnew DataGridViewTextBoxColumn();
        c->Name = name;
        c->HeaderText = header;
        c->Width = width;
        c->SortMode = DataGridViewColumnSortMode::NotSortable;
        Columns->Add(c);
        return c;
    }
    DataGridViewCheckBoxColumn^ AddCheckCol(String^ name, String^ header, int width) {
        DataGridViewCheckBoxColumn^ c = gcnew DataGridViewCheckBoxColumn();
        c->Name = name;
        c->HeaderText = header;
        c->Width = width;
        c->SortMode = DataGridViewColumnSortMode::NotSortable;
        Columns->Add(c);
        return c;
    }

protected:
    virtual void OnColumnAdded(DataGridViewColumnEventArgs^ e) override {
        DataGridView::OnColumnAdded(e);
        DataGridViewComboBoxColumn^ combo = dynamic_cast<DataGridViewComboBoxColumn^>(e->Column);
        if (combo != nullptr) {
            combo->DisplayStyle = DataGridViewComboBoxDisplayStyle::Nothing;  // chevron painted in OnCellPainting
            combo->FlatStyle = FlatStyle::Flat;
        }
        DataGridViewCheckBoxColumn^ check = dynamic_cast<DataGridViewCheckBoxColumn^>(e->Column);
        if (check != nullptr) check->FlatStyle = FlatStyle::Flat;
    }
    virtual void OnPaint(PaintEventArgs^ e) override {
        DataGridView::OnPaint(e);
        Graphics^ g = e->Graphics;
        if (RowCount == 0 && !String::IsNullOrEmpty(EmptyText)) {
            const int top = ColumnHeadersVisible ? ColumnHeadersHeight : 0;
            TextRenderer::DrawText(g, EmptyText, Theme::Body, Rectangle(24, top, Width - 48, Math::Max(60, Height - top - 20)), Theme::TextMuted,
                                   TextFormatFlags::HorizontalCenter | TextFormatFlags::VerticalCenter | TextFormatFlags::WordBreak | TextFormatFlags::NoPrefix);
        }
        if (VerticalScrollBar->Visible && HorizontalScrollBar->Visible)  // the corner between scrollbars is system-colored
            g->FillRectangle(Theme::SurfaceBrush, VerticalScrollBar->Left, HorizontalScrollBar->Top, VerticalScrollBar->Width, HorizontalScrollBar->Height);
        g->DrawRectangle(Theme::BorderPen, 0, 0, Width - 1, Height - 1);  // themed border over the system one
    }
    virtual void OnCellPainting(DataGridViewCellPaintingEventArgs^ e) override {
        DataGridView::OnCellPainting(e);
        if (e->Handled || e->ColumnIndex < 0) return;
        if (e->RowIndex < 0) {  // header: band + bottom line
            e->Paint(e->ClipBounds, DataGridViewPaintParts::All & ~DataGridViewPaintParts::Border);
            e->Graphics->DrawLine(Theme::BorderPen, e->CellBounds.Left, e->CellBounds.Bottom - 1, e->CellBounds.Right, e->CellBounds.Bottom - 1);
            e->Handled = true;
            return;
        }
        DataGridViewColumn^ col = Columns[e->ColumnIndex];
        const bool check = dynamic_cast<DataGridViewCheckBoxColumn^>(col) != nullptr;
        const bool combo = dynamic_cast<DataGridViewComboBoxColumn^>(col) != nullptr;
        if (!check && !combo) return;
        const bool selected = (e->State & DataGridViewElementStates::Selected) != DataGridViewElementStates::None;
        if ((e->PaintParts & DataGridViewPaintParts::Background) != DataGridViewPaintParts::None) e->PaintBackground(e->ClipBounds, selected);
        if (check) {
            Object^ v = e->Value;
            if (IsCurrentCellInEditMode && CurrentCell != nullptr && CurrentCell->RowIndex == e->RowIndex && CurrentCell->ColumnIndex == e->ColumnIndex)
                v = CurrentCell->EditedFormattedValue;
            const bool on = v != nullptr && v != DBNull::Value && Convert::ToBoolean(v);
            const int s = Theme::Scale(this, 16);
            Rectangle box(e->CellBounds.X + (e->CellBounds.Width - s) / 2, e->CellBounds.Y + (e->CellBounds.Height - s) / 2, s, s);
            Theme::PaintCheck(e->Graphics, box, on, !col->ReadOnly && !ReadOnly, false);
        } else {
            e->Paint(e->ClipBounds, DataGridViewPaintParts::ContentForeground);
            const float cx = e->CellBounds.Right - Dpi(this, 14), cy = e->CellBounds.Y + e->CellBounds.Height / 2.0f, a = Dpi(this, 3.5f);
            SmoothingMode old = e->Graphics->SmoothingMode;
            e->Graphics->SmoothingMode = SmoothingMode::AntiAlias;
            e->Graphics->DrawLines(gcnew Pen(Theme::TextMuted, 1.4f), gcnew array<PointF>{PointF(cx - a, cy - a / 2), PointF(cx, cy + a / 2), PointF(cx + a, cy - a / 2)});
            e->Graphics->SmoothingMode = old;
        }
        e->Paint(e->ClipBounds, DataGridViewPaintParts::Border);
        e->Handled = true;
    }
    virtual void OnEditingControlShowing(DataGridViewEditingControlShowingEventArgs^ e) override {
        DataGridView::OnEditingControlShowing(e);
        e->CellStyle->BackColor = Theme::Input;
        e->CellStyle->ForeColor = Theme::Text;
        e->Control->BackColor = Theme::Input;
        e->Control->ForeColor = Theme::Text;
        ComboBox^ cb = dynamic_cast<ComboBox^>(e->Control);
        if (cb != nullptr) {
            cb->FlatStyle = FlatStyle::Flat;
            if (cb->IsHandleCreated) Theme::NativeTheme(cb);
        }
    }
    virtual void OnColumnHeaderMouseClick(DataGridViewCellMouseEventArgs^ e) override {
        DataGridView::OnColumnHeaderMouseClick(e);
        if (!ColumnChooser || e->Button != Windows::Forms::MouseButtons::Right) return;
        Windows::Forms::ContextMenuStrip^ menu = gcnew Windows::Forms::ContextMenuStrip();
        menu->Renderer = gcnew ThemedMenuRenderer();
        menu->Font = Theme::Body;
        ToolStripLabel^ title = gcnew ToolStripLabel("Show columns");
        title->ForeColor = Theme::TextMuted;
        menu->Items->Add(title);
        for each (DataGridViewColumn^ c in Columns) {
            ToolStripMenuItem^ it = gcnew ToolStripMenuItem(c->HeaderText);
            it->Checked = c->Visible;
            it->CheckOnClick = true;
            it->Tag = c;
            it->Enabled = c->AutoSizeMode != DataGridViewAutoSizeColumnMode::Fill;  // keep the fill column
            it->CheckedChanged += gcnew EventHandler(this, &BufferedGrid::OnToggleColumn);
            menu->Items->Add(it);
        }
        menu->Show(this, PointToClient(Control::MousePosition));
    }
    // Hover is painted in RowPrePaint (no row unsharing, works in VirtualMode).
    virtual void OnCellMouseEnter(DataGridViewCellEventArgs^ e) override {
        DataGridView::OnCellMouseEnter(e);
        if (e->RowIndex == hoverRow_) return;
        int old = hoverRow_;
        hoverRow_ = e->RowIndex;
        if (old >= 0 && old < RowCount) InvalidateRow(old);
        if (hoverRow_ >= 0 && hoverRow_ < RowCount) InvalidateRow(hoverRow_);
    }
    virtual void OnMouseLeave(EventArgs^ e) override {
        DataGridView::OnMouseLeave(e);
        int old = hoverRow_;
        hoverRow_ = -1;
        if (old >= 0 && old < RowCount) InvalidateRow(old);
    }
    virtual void OnRowPrePaint(DataGridViewRowPrePaintEventArgs^ e) override {
        DataGridView::OnRowPrePaint(e);
        if (e->RowIndex == hoverRow_ && (e->State & DataGridViewElementStates::Selected) == DataGridViewElementStates::None) {
            e->Graphics->FillRectangle(Theme::HoverBrush, e->RowBounds);
            e->PaintParts = e->PaintParts & ~DataGridViewPaintParts::Background;
            e->PaintCells(e->ClipBounds, DataGridViewPaintParts::All & ~(DataGridViewPaintParts::Background | DataGridViewPaintParts::SelectionBackground));
            e->Handled = true;
        }
    }

private:
    int hoverRow_;
    void OnToggleColumn(Object^ sender, EventArgs^) {
        ToolStripMenuItem^ it = safe_cast<ToolStripMenuItem^>(sender);
        safe_cast<DataGridViewColumn^>(it->Tag)->Visible = it->Checked;
    }
};

// Tabs: text labels with an accent underline on the selected tab and a hairline under the strip.
public ref class NeoTabs : public TabControl {
public:
    NeoTabs() {
        SetStyle(ControlStyles::UserPaint | ControlStyles::AllPaintingInWmPaint | ControlStyles::OptimizedDoubleBuffer | ControlStyles::ResizeRedraw, true);
        Padding = Point(16, 7);
        hover_ = -1;
    }

protected:
    virtual void OnPaint(PaintEventArgs^ e) override {
        Graphics^ g = e->Graphics;
        g->Clear(Theme::ContainerBg(Parent));  // TabControl ignores BackColor
        if (TabCount == 0) return;
        const int stripBottom = GetTabRect(0).Bottom;
        g->FillRectangle(gcnew SolidBrush(Theme::Border), 0, stripBottom - 1, Width, 1);
        for (int i = 0; i < TabCount; ++i) {
            Rectangle r = GetTabRect(i);
            const bool sel = i == SelectedIndex;
            TextRenderer::DrawText(g, TabPages[i]->Text, Theme::Body, r, sel || i == hover_ ? Theme::Text : Theme::TextMuted,
                                   TextFormatFlags::HorizontalCenter | TextFormatFlags::VerticalCenter | TextFormatFlags::NoPrefix | TextFormatFlags::SingleLine);
            if (sel) g->FillRectangle(Theme::AccentBrush, r.X + 6, stripBottom - 2, r.Width - 12, 2);
        }
        if (Focused && SelectedIndex >= 0) {
            Rectangle f = GetTabRect(SelectedIndex);
            f.Inflate(-3, -4);
            g->DrawRectangle(Theme::FocusPen, f);
        }
    }
    virtual void OnSelectedIndexChanged(EventArgs^ e) override {
        TabControl::OnSelectedIndexChanged(e);
        Invalidate();
    }
    virtual void OnMouseMove(MouseEventArgs^ e) override {
        TabControl::OnMouseMove(e);
        int h = -1;
        for (int i = 0; i < TabCount && h < 0; ++i)
            if (GetTabRect(i).Contains(e->Location)) h = i;
        if (h != hover_) {
            hover_ = h;
            Invalidate(Rectangle(0, 0, Width, DisplayRectangle.Top));
        }
    }
    virtual void OnMouseLeave(EventArgs^ e) override {
        TabControl::OnMouseLeave(e);
        if (hover_ >= 0) {
            hover_ = -1;
            Invalidate(Rectangle(0, 0, Width, DisplayRectangle.Top));
        }
    }

private:
    int hover_;
};

// List box with roomy, owner-drawn rows (native rows are cramped and ignore the theme's selection color).
public ref class NeoListBox : public ListBox {
public:
    NeoListBox() {
        DrawMode = Windows::Forms::DrawMode::OwnerDrawFixed;
        IntegralHeight = false;
        ItemHeight = 28;
    }

protected:
    virtual void OnDrawItem(DrawItemEventArgs^ e) override {
        if (e->Index < 0) return;
        const bool sel = (e->State & DrawItemState::Selected) == DrawItemState::Selected;
        e->Graphics->FillRectangle(sel ? Theme::SelectionBrush : gcnew SolidBrush(BackColor), e->Bounds);
        TextRenderer::DrawText(e->Graphics, GetItemText(Items[e->Index]), Font, Rectangle(e->Bounds.X + 8, e->Bounds.Y, e->Bounds.Width - 12, e->Bounds.Height), Theme::Text,
                               TextFormatFlags::VerticalCenter | TextFormatFlags::Left | TextFormatFlags::EndEllipsis | TextFormatFlags::NoPrefix);
        if ((e->State & DrawItemState::Focus) == DrawItemState::Focus && Focused) e->Graphics->DrawRectangle(Theme::FocusPen, e->Bounds.X, e->Bounds.Y, e->Bounds.Width - 1, e->Bounds.Height - 1);
    }
    virtual void OnHandleCreated(EventArgs^ e) override {
        ListBox::OnHandleCreated(e);
        ItemHeight = Theme::Scale(this, 28);
    }
};

// Drop-down list matching text boxes: same height, border, focus color and a chevron button.
public ref class NeoCombo : public ComboBox {
public:
    NeoCombo() {
        DrawMode = Windows::Forms::DrawMode::OwnerDrawFixed;
        DropDownStyle = ComboBoxStyle::DropDownList;
        FlatStyle = Windows::Forms::FlatStyle::Flat;
        IntegralHeight = false;
        MaxDropDownItems = 12;
        ItemHeight = Theme::ControlHeight - 6;
    }

protected:
    virtual void OnHandleCreated(EventArgs^ e) override {
        ComboBox::OnHandleCreated(e);
        ItemHeight = Theme::Scale(this, Theme::ControlHeight) - 6;
    }
    virtual void OnDrawItem(DrawItemEventArgs^ e) override {
        const bool edit = (e->State & DrawItemState::ComboBoxEdit) == DrawItemState::ComboBoxEdit;
        const bool sel = !edit && (e->State & DrawItemState::Selected) == DrawItemState::Selected;
        e->Graphics->FillRectangle(sel ? Theme::SelectionBrush : Theme::InputBrush, e->Bounds);
        if (e->Index < 0) return;
        TextRenderer::DrawText(e->Graphics, GetItemText(Items[e->Index]), Font, Rectangle(e->Bounds.X + (edit ? 4 : 8), e->Bounds.Y, e->Bounds.Width - 12, e->Bounds.Height),
                               Enabled ? Theme::Text : Theme::TextMuted, TextFormatFlags::VerticalCenter | TextFormatFlags::Left | TextFormatFlags::EndEllipsis | TextFormatFlags::NoPrefix);
    }
    virtual void WndProc(Message% m) override {
        ComboBox::WndProc(m);
        if (m.Msg == 0x000F && IsHandleCreated) PaintChrome();  // WM_PAINT: our border/button over the flat adapter's
    }
    virtual void OnGotFocus(EventArgs^ e) override { ComboBox::OnGotFocus(e); Invalidate(); }
    virtual void OnLostFocus(EventArgs^ e) override { ComboBox::OnLostFocus(e); Invalidate(); }
    virtual void OnDropDownClosed(EventArgs^ e) override { ComboBox::OnDropDownClosed(e); Invalidate(); }

private:
    void PaintChrome() {
        Graphics^ g = Graphics::FromHwnd(Handle);
        const int w = Width, h = Height, bw = Theme::Scale(this, 26);
        g->FillRectangle(Theme::InputBrush, w - bw - 1, 1, bw, h - 2);
        g->DrawRectangle(gcnew Pen(Theme::Input), 1, 1, w - 3, h - 3);
        g->DrawRectangle(Focused || DroppedDown ? Theme::FocusPen : Theme::InputPen, 0, 0, w - 1, h - 1);
        g->SmoothingMode = SmoothingMode::AntiAlias;
        const float cx = w - bw / 2.0f - 1, cy = h / 2.0f, a = Dpi(this, 4);
        g->DrawLines(gcnew Pen(Theme::TextMuted, 1.5f), gcnew array<PointF>{PointF(cx - a, cy - a / 2), PointF(cx, cy + a / 2), PointF(cx + a, cy - a / 2)});
        delete g;
    }
};

// Check box drawn with the theme's check glyph (the native one stays light in dark mode).
public ref class NeoCheck : public CheckBox {
public:
    NeoCheck() {
        AccessibleRole = Windows::Forms::AccessibleRole::CheckButton;
        SetStyle(ControlStyles::UserPaint | ControlStyles::AllPaintingInWmPaint | ControlStyles::OptimizedDoubleBuffer | ControlStyles::SupportsTransparentBackColor, true);
    }
    virtual Drawing::Size GetPreferredSize(Drawing::Size) override {
        Drawing::Size t = TextRenderer::MeasureText(Text, Font);
        return Drawing::Size(t.Width + Theme::Scale(this, 28), Math::Max(Theme::Scale(this, 24), t.Height + 4));
    }

protected:
    virtual AccessibleObject^ CreateAccessibilityInstance() override { return gcnew OwnerDrawnAccessible(this, Windows::Forms::AccessibleRole::CheckButton); }
    virtual void OnPaint(PaintEventArgs^ e) override {
        Graphics^ g = e->Graphics;
        g->Clear(Parent != nullptr ? Parent->BackColor : Theme::Bg);
        const int s = Theme::Scale(this, 16);
        Rectangle box(1, (Height - s) / 2, s, s);
        Theme::PaintCheck(g, box, Checked, Enabled, hot_);
        TextRenderer::DrawText(g, Text, Font, Rectangle(s + Theme::Scale(this, 10), 0, Width - s - Theme::Scale(this, 10), Height), Enabled ? Theme::Text : Theme::TextMuted,
                               TextFormatFlags::VerticalCenter | TextFormatFlags::Left | TextFormatFlags::NoPrefix);
        if (Focused && ShowFocusCues) {
            box.Inflate(2, 2);
            g->SmoothingMode = SmoothingMode::AntiAlias;
            g->DrawPath(Theme::FocusPen, Theme::RoundRect(RectangleF((float)box.X, (float)box.Y, (float)box.Width - 1, (float)box.Height - 1), 4.5f));
        }
    }
    virtual void OnMouseEnter(EventArgs^ e) override { CheckBox::OnMouseEnter(e); hot_ = true; Invalidate(); }
    virtual void OnMouseLeave(EventArgs^ e) override { CheckBox::OnMouseLeave(e); hot_ = false; Invalidate(); }

private:
    bool hot_;
};

// Lightweight line chart (no animation): primary series with a soft area, secondary as a dashed line.
public ref class LineChart : public Control {
public:
    String^ Title;
    List<PointF>^ SeriesA;
    List<PointF>^ SeriesB;
    String^ NameA;
    String^ NameB;
    String^ EmptyText;
    LineChart() {
        NameA = "Equity";
        NameB = "";
        EmptyText = "No data yet";
        SetStyle(ControlStyles::OptimizedDoubleBuffer | ControlStyles::AllPaintingInWmPaint | ControlStyles::UserPaint | ControlStyles::ResizeRedraw, true);
        SeriesA = gcnew List<PointF>();
        SeriesB = gcnew List<PointF>();
        Tag = "keep";
    }

protected:
    virtual void OnPaint(PaintEventArgs^ e) override {
        Graphics^ g = e->Graphics;
        g->Clear(Parent != nullptr ? Parent->BackColor : Theme::Bg);
        g->SmoothingMode = SmoothingMode::AntiAlias;
        GraphicsPath^ fp = Theme::RoundRect(RectangleF(0.5f, 0.5f, Width - 1.0f, Height - 1.0f), Dpi(this, Theme::Radius));
        g->FillPath(Theme::SurfaceBrush, fp);
        g->DrawPath(Theme::BorderPen, fp);
        TextRenderer::DrawText(g, Title == nullptr ? "" : Title, Theme::BodyBold, Rectangle((int)Dpi(this, 16), (int)Dpi(this, 12), Width - (int)Dpi(this, 32), (int)Dpi(this, 20)),
                               Theme::Text, TextFormatFlags::Left | TextFormatFlags::EndEllipsis | TextFormatFlags::NoPrefix);
        Rectangle plot((int)Dpi(this, 60), (int)Dpi(this, 44), Width - (int)Dpi(this, 76), Height - (int)Dpi(this, 74));
        if (plot.Width < 20 || plot.Height < 20) return;
        if (SeriesA->Count < 2 && SeriesB->Count < 2) {
            TextRenderer::DrawText(g, EmptyText, Theme::Small, plot, Theme::TextMuted, TextFormatFlags::HorizontalCenter | TextFormatFlags::VerticalCenter);
            return;
        }
        float minX = Single::MaxValue, maxX = Single::MinValue, minY = Single::MaxValue, maxY = Single::MinValue;
        for each (List<PointF>^ s in gcnew array<List<PointF>^>{SeriesA, SeriesB})
            for each (PointF p in s) {
                minX = Math::Min(minX, p.X);
                maxX = Math::Max(maxX, p.X);
                minY = Math::Min(minY, p.Y);
                maxY = Math::Max(maxY, p.Y);
            }
        if (maxY - minY < 1e-6f) {
            maxY += 1;
            minY -= 1;
        }
        if (maxX - minX < 1e-6f) maxX = minX + 1;
        for (int i = 0; i <= 3; ++i) {
            int y = plot.Top + plot.Height * i / 3;
            g->DrawLine(Theme::GridPen, plot.Left, y, plot.Right, y);
            float v = maxY - (maxY - minY) * i / 3;
            TextRenderer::DrawText(g, v.ToString("N2"), Theme::Small, Rectangle(0, y - 8, plot.Left - 8, 16), Theme::TextMuted, TextFormatFlags::Right);
        }
        try {  // a chart must never take the window down
            DrawSeries(g, SeriesB, plot, minX, maxX, minY, maxY, Theme::ChartSecondary, false);
            DrawSeries(g, SeriesA, plot, minX, maxX, minY, maxY, Theme::Accent, true);
        } catch (OverflowException^) {
        } catch (ArgumentException^) {
        }
        int lx = plot.Left;
        const int ly = Height - (int)Dpi(this, 22);
        if (SeriesA->Count > 1) {
            g->FillRectangle(Theme::AccentBrush, lx, ly + 7, 12, 2);
            TextRenderer::DrawText(g, NameA, Theme::Small, Point(lx + 18, ly), Theme::TextMuted);
            lx += (int)Dpi(this, 100);
        }
        if (SeriesB->Count > 1) {
            g->FillRectangle(gcnew SolidBrush(Theme::ChartSecondary), lx, ly + 7, 12, 2);
            TextRenderer::DrawText(g, NameB, Theme::Small, Point(lx + 18, ly), Theme::TextMuted);
        }
    }

private:
    void DrawSeries(Graphics^ g, List<PointF>^ s, Rectangle plot, float minX, float maxX, float minY, float maxY, Color c, bool primary) {
        if (s->Count < 2) return;
        // Map to pixels, dropping non-finite points and points on the previous pixel (degenerate paths make
        // GDI+ throw OverflowException).
        List<PointF>^ px = gcnew List<PointF>(s->Count);
        for (int i = 0; i < s->Count; ++i) {
            PointF q(plot.Left + (s[i].X - minX) / (maxX - minX) * plot.Width, plot.Bottom - (s[i].Y - minY) / (maxY - minY) * plot.Height);
            if (Single::IsNaN(q.X) || Single::IsNaN(q.Y) || Single::IsInfinity(q.X) || Single::IsInfinity(q.Y)) continue;
            if (px->Count > 0 && Math::Abs(px[px->Count - 1].X - q.X) < 0.5f && Math::Abs(px[px->Count - 1].Y - q.Y) < 0.5f) continue;
            px->Add(q);
        }
        if (px->Count < 2) return;
        array<PointF>^ pts = px->ToArray();
        if (primary) {
            GraphicsPath^ fill = gcnew GraphicsPath();
            fill->AddLines(pts);
            fill->AddLine(pts[pts->Length - 1], PointF(pts[pts->Length - 1].X, (float)plot.Bottom));
            fill->AddLine(PointF(pts[pts->Length - 1].X, (float)plot.Bottom), PointF(pts[0].X, (float)plot.Bottom));
            fill->CloseFigure();
            g->FillPath(gcnew SolidBrush(Color::FromArgb(Theme::Dark ? 36 : 26, c)), fill);
            g->DrawLines(gcnew Pen(c, 2.0f), pts);
        } else {
            Pen^ dashed = gcnew Pen(c, 1.5f);
            dashed->DashStyle = DashStyle::Dash;
            g->DrawLines(dashed, pts);
        }
    }
};

// Small layout helpers so pages stay consistent.
public ref class L abstract sealed {
public:
    static Label^ Text(String^ text) { return Text(text, nullptr); }
    static Label^ Text(String^ text, String^ tag) {
        Label^ l = gcnew Label();
        l->UseMnemonic = false;  // data-driven text (chat titles) may contain '&'
        l->Text = text;
        l->Tag = tag;
        l->AutoSize = true;
        l->Margin = tag == "h2" ? Windows::Forms::Padding(0, 14, 8, 6) : Windows::Forms::Padding(0, 10, 8, 4);  // centered beside 30 px controls
        return l;
    }
    static Label^ Wrap(String^ text, String^ tag, int width) {
        Label^ l = Text(text, tag);
        l->MaximumSize = Drawing::Size(width, 0);
        return l;
    }
    static Button^ Btn(String^ text) { return Btn(text, nullptr); }
    static Button^ Btn(String^ text, String^ tag) {
        NeoButton^ b = gcnew NeoButton();
        b->Text = text;
        b->Tag = tag;
        b->AutoSize = true;
        b->AutoSizeMode = AutoSizeMode::GrowAndShrink;
        b->Margin = Windows::Forms::Padding(0, 4, 8, 4);
        b->AccessibleName = text;
        return b;
    }
    // Button with a plain-language tooltip explaining what it does (also its accessible description).
    static Button^ Btn(String^ text, String^ tag, String^ help) {
        Button^ b = Btn(text, tag);
        Tip(b, help);
        return b;
    }
    // Subscribe `handler` to the "value changed" event of every editable control under `root` (recursively),
    // so a page can show an "unsaved changes" indicator without wiring each control by hand.
    static void OnAnyChange(Control^ root, EventHandler^ handler) {
        for each (Control^ c in root->Controls) {
            if (dynamic_cast<TextBoxBase^>(c) != nullptr) safe_cast<TextBoxBase^>(c)->TextChanged += handler;
            else if (dynamic_cast<NumericUpDown^>(c) != nullptr) safe_cast<NumericUpDown^>(c)->ValueChanged += handler;
            else if (dynamic_cast<ComboBox^>(c) != nullptr) safe_cast<ComboBox^>(c)->SelectedIndexChanged += handler;
            else if (dynamic_cast<CheckBox^>(c) != nullptr) safe_cast<CheckBox^>(c)->CheckedChanged += handler;
            else if (dynamic_cast<DataGridView^>(c) != nullptr)
                safe_cast<DataGridView^>(c)->CurrentCellDirtyStateChanged += handler;  // fires as soon as a cell is edited
            OnAnyChange(c, handler);
        }
    }

    // Shared hover tooltip. Kept readable long enough to finish reading.
    static void Tip(Control^ c, String^ help) {
        if (String::IsNullOrEmpty(help)) return;
        if (tips_ == nullptr) {
            tips_ = gcnew ToolTip();
            tips_->InitialDelay = 350;
            tips_->ReshowDelay = 120;
            tips_->AutoPopDelay = 20000;
            tips_->ShowAlways = true;
        }
        tips_->SetToolTip(c, help);
        c->AccessibleDescription = help;
    }
    static TextBox^ Box(int width) {
        TextBox^ t = gcnew TextBox();
        t->Width = width;
        t->Margin = Windows::Forms::Padding(0, 4, 8, 4);
        return t;
    }
    static ComboBox^ Combo(int width, array<String^>^ items) {
        NeoCombo^ c = gcnew NeoCombo();
        c->Width = width;
        c->Margin = Windows::Forms::Padding(0, 4, 8, 4);
        for each (String^ s in items) c->Items->Add(s);
        if (c->Items->Count > 0) c->SelectedIndex = 0;
        return c;
    }
    static NumericUpDown^ Num(double min, double max, double value, int decimals, double step) {
        NumericUpDown^ n = gcnew NumericUpDown();
        n->Minimum = Decimal(min);
        n->Maximum = Decimal(max);
        n->DecimalPlaces = decimals;
        n->Increment = Decimal(step);
        n->Value = Decimal(Math::Max(min, Math::Min(max, value)));
        n->Width = 120;
        n->Margin = Windows::Forms::Padding(0, 4, 8, 4);
        return n;
    }
    static CheckBox^ Check(String^ text) {
        NeoCheck^ c = gcnew NeoCheck();
        c->Text = text;
        c->AutoSize = true;
        c->Margin = Windows::Forms::Padding(0, 7, 16, 7);
        return c;
    }
    static FlowLayoutPanel^ Row() {
        FlowLayoutPanel^ f = gcnew FlowLayoutPanel();
        f->AutoSize = true;
        f->AutoSizeMode = AutoSizeMode::GrowAndShrink;
        f->WrapContents = true;
        f->Dock = DockStyle::Top;
        f->Margin = Windows::Forms::Padding(0);
        f->Padding = Windows::Forms::Padding(0, 2, 0, 2);
        return f;
    }
    static FlowLayoutPanel^ Row(... array<Control^>^ items) {
        FlowLayoutPanel^ f = Row();
        for each (Control^ c in items) f->Controls->Add(c);
        return f;
    }
    // Vertically stacked, scrollable column; cards stretch to a common readable width.
    static FlowLayoutPanel^ Stack() {
        FlowLayoutPanel^ f = gcnew FlowLayoutPanel();
        f->FlowDirection = FlowDirection::TopDown;
        f->WrapContents = false;
        f->AutoScroll = true;
        f->Dock = DockStyle::Fill;
        f->Padding = Windows::Forms::Padding(Theme::Gutter, 4, Theme::Gutter, 16);
        f->Resize += gcnew EventHandler(&L::OnStackResize);
        f->ControlAdded += gcnew ControlEventHandler(&L::OnStackControlAdded);
        return f;
    }
    static GroupBox^ Group(String^ title, Control^ content) {
        SectionBox^ g = gcnew SectionBox();
        g->Text = title;
        g->AutoSize = true;
        g->AutoSizeMode = AutoSizeMode::GrowAndShrink;
        g->Font = Theme::Body;
        g->Padding = Windows::Forms::Padding(16, 29, 16, 14);  // content starts below the title (top = font height + 29)
        g->Margin = Windows::Forms::Padding(0, 0, 0, 16);
        content->Font = Theme::Body;
        content->Dock = DockStyle::Fill;
        g->Controls->Add(content);
        return g;
    }
    static TableLayoutPanel^ Form2() {
        TableLayoutPanel^ t = gcnew TableLayoutPanel();
        t->ColumnCount = 2;
        t->AutoSize = true;
        t->AutoSizeMode = AutoSizeMode::GrowAndShrink;
        t->ColumnStyles->Add(gcnew ColumnStyle(SizeType::AutoSize));
        t->ColumnStyles->Add(gcnew ColumnStyle(SizeType::AutoSize));
        return t;
    }
    static void AddField(TableLayoutPanel^ t, String^ label, Control^ c) {
        Label^ l = Text(label);
        l->Margin = Windows::Forms::Padding(0, 10, 20, 4);
        t->Controls->Add(l);
        t->Controls->Add(c);
        c->AccessibleName = label;
    }

    // A small "?" marker: hovering shows `help`. Use this instead of long captions under controls.
    static Label^ Help(String^ help) {
        Label^ l = gcnew Label();
        l->Text = "?";
        l->AutoSize = false;
        l->Size = Drawing::Size(18, 18);
        l->TextAlign = ContentAlignment::MiddleCenter;
        l->Font = Theme::BodyBold;
        l->Tag = "accent";  // themed as the accent colour so it reads as an info marker
        l->Cursor = Cursors::Help;
        l->Margin = Windows::Forms::Padding(2, 11, 0, 4);
        Tip(l, help);
        return l;
    }
    // Field row with a "?" help marker beside the label; the tooltip is also set on the control.
    static void AddField(TableLayoutPanel^ t, String^ label, Control^ c, String^ help) {
        Label^ l = Text(label);
        l->Margin = Windows::Forms::Padding(0, 10, 6, 4);
        FlowLayoutPanel^ row = gcnew FlowLayoutPanel();
        row->AutoSize = true;
        row->WrapContents = false;
        row->Margin = Windows::Forms::Padding(0, 0, 14, 0);
        row->Controls->Add(l);
        row->Controls->Add(Help(help));
        t->Controls->Add(row);
        t->Controls->Add(c);
        c->AccessibleName = label;
        Tip(c, help);
    }

private:
    static ToolTip^ tips_;

    static void SyncWidths(FlowLayoutPanel^ f) {
        const int max = Theme::Scale(f, 1100);
        // Always reserve the vertical scrollbar so showing it never forces a horizontal one.
        const int w = Math::Min(max, f->Width - f->Padding.Horizontal - SystemInformation::VerticalScrollBarWidth - 2);
        if (w < 200) return;
        for each (Control^ c in f->Controls) {
            if (dynamic_cast<SectionBox^>(c) != nullptr) {
                Drawing::Size ms(w - c->Margin.Horizontal, 0);
                if (c->MinimumSize != ms) c->MinimumSize = ms;
            } else if (dynamic_cast<LineChart^>(c) != nullptr) {
                if (c->Width != w - c->Margin.Horizontal) c->Width = w - c->Margin.Horizontal;
            }
        }
    }
    static void OnStackResize(Object^ sender, EventArgs^) { SyncWidths(safe_cast<FlowLayoutPanel^>(sender)); }
    static void OnStackControlAdded(Object^ sender, ControlEventArgs^) { SyncWidths(safe_cast<FlowLayoutPanel^>(sender)); }
};

// Minimal modal prompts.
public ref class Prompt abstract sealed {
public:
    static String^ Ask(IWin32Window^ owner, String^ title, String^ message, String^ initial, bool password) {
        Form^ f = gcnew Form();
        f->Text = title;
        f->FormBorderStyle = FormBorderStyle::FixedDialog;
        f->StartPosition = FormStartPosition::CenterParent;
        f->MinimizeBox = false;
        f->MaximizeBox = false;
        f->ShowInTaskbar = false;
        f->AutoScaleMode = AutoScaleMode::Dpi;
        f->AutoSize = true;
        f->AutoSizeMode = AutoSizeMode::GrowAndShrink;
        f->Font = Theme::Body;
        f->Padding = Windows::Forms::Padding(20);
        FlowLayoutPanel^ s = gcnew FlowLayoutPanel();
        s->FlowDirection = FlowDirection::TopDown;
        s->AutoSize = true;
        Label^ msg = L::Wrap(message, nullptr, 460);
        msg->Margin = Windows::Forms::Padding(0, 0, 0, 10);
        s->Controls->Add(msg);
        TextBox^ box = L::Box(440);
        box->Text = initial;
        box->UseSystemPasswordChar = password;
        box->AccessibleName = message;
        s->Controls->Add(box);
        Button^ ok = L::Btn("OK", "accent");
        ok->DialogResult = DialogResult::OK;
        Button^ cancel = L::Btn("Cancel");
        cancel->DialogResult = DialogResult::Cancel;
        FlowLayoutPanel^ buttons = L::Row(ok, cancel);
        buttons->Margin = Windows::Forms::Padding(0, 12, 0, 0);
        s->Controls->Add(buttons);
        f->Controls->Add(s);
        f->AcceptButton = ok;
        f->CancelButton = cancel;
        Theme::Apply(f);
        f->BackColor = Theme::Bg;
        f->HandleCreated += gcnew EventHandler(&Prompt::OnDialogHandle);
        return f->ShowDialog(owner) == DialogResult::OK ? box->Text : nullptr;
    }

    static void Info(IWin32Window^ owner, String^ title, String^ text) { MessageBox::Show(owner, text, title, MessageBoxButtons::OK, MessageBoxIcon::Information); }
    static void Error(IWin32Window^ owner, String^ text) { MessageBox::Show(owner, text, "Pipkite", MessageBoxButtons::OK, MessageBoxIcon::Warning); }

    // Dialogs get the same title bar as the main window.
    static void OnDialogHandle(Object^ sender, EventArgs^) { Theme::TitleBar(safe_cast<Form^>(sender)); }
};

}  // namespace PipkiteUI
