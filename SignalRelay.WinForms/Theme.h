#pragma once
// Design tokens and theme application. One calm neutral palette per theme with a single blue accent;
// status colors (green = confirmed, amber = needs attention, red = error) always come with text.
// Type scale (Segoe UI Variable): body 9.75 pt, small 8.75 pt, section 11 pt semibold, page title 16 pt
// semibold. Every input is ControlHeight tall with the same 1 px border and an accent focus border.
// Performance: pens/brushes are cached and rebuilt only when the theme changes.

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Drawing;
using namespace System::Drawing::Drawing2D;
using namespace System::Runtime::InteropServices;
using namespace System::Windows::Forms;

namespace SignalRelayUI {

[StructLayout(LayoutKind::Sequential)]
public value struct WinRect {
    int Left, Top, Right, Bottom;
};

public ref class NativeUi abstract sealed {
public:
    [DllImport("uxtheme.dll", CharSet = CharSet::Unicode)]
    static int SetWindowTheme(IntPtr hwnd, String^ subAppName, String^ subIdList);
    [DllImport("dwmapi.dll")]
    static int DwmSetWindowAttribute(IntPtr hwnd, int attribute, int% value, int size);
    [DllImport("user32.dll")]
    static IntPtr GetWindowDC(IntPtr hwnd);
    [DllImport("user32.dll")]
    static int ReleaseDC(IntPtr hwnd, IntPtr hdc);
    [DllImport("user32.dll")]
    static bool GetWindowRect(IntPtr hwnd, WinRect% rect);
    [DllImport("user32.dll")]
    static bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, unsigned int flags);
    [DllImport("user32.dll")]
    static IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr wp, IntPtr lp);
};

public ref class Theme abstract sealed {
    ref class InputFrame;

public:
    static bool Dark = true;
    static Color Bg, Surface, Surface2, SurfaceHover, Border, InputBorder, Text, TextMuted, Accent, AccentHover, AccentText, AccentSoft, Success, Warning, Danger,
        Input, Selection, GridLine, ChartSecondary;
    static Drawing::Font^ Body;
    static Drawing::Font^ BodyBold;  // semibold
    static Drawing::Font^ Small;
    static Drawing::Font^ Caption;   // small semibold: table headers, card captions
    static Drawing::Font^ Title;
    static Drawing::Font^ H2;
    static Drawing::Font^ Mono;
    static Drawing::Font^ Icon;
    static Drawing::Font^ IconLarge;
    static Drawing::Font^ Big;
    static SolidBrush ^ BgBrush, ^SurfaceBrush, ^Surface2Brush, ^HoverBrush, ^TextBrush, ^MutedBrush, ^AccentBrush, ^InputBrush, ^SelectionBrush, ^AccentSoftBrush;
    static Pen ^ BorderPen, ^InputPen, ^GridPen, ^AccentPen, ^FocusPen;
    literal int Gap = 8;
    literal int Radius = 8;
    literal int ControlHeight = 30;  // inputs, combo boxes and buttons (96 DPI)
    literal int Gutter = 24;         // page side padding

    static Theme() {
        String^ ui = Pick(gcnew array<String^>{"Segoe UI Variable Text", "Segoe UI"});
        String^ uiSemi = Pick(gcnew array<String^>{"Segoe UI Variable Text Semibold", "Segoe UI Semibold", "Segoe UI"});
        String^ displaySemi = Pick(gcnew array<String^>{"Segoe UI Variable Display Semib", "Segoe UI Semibold", "Segoe UI"});
        String^ mono = Pick(gcnew array<String^>{"Cascadia Mono", "Consolas"});
        Body = gcnew Drawing::Font(ui, 9.75f);
        BodyBold = gcnew Drawing::Font(uiSemi, 9.75f);
        Small = gcnew Drawing::Font(ui, 8.75f);
        Caption = gcnew Drawing::Font(uiSemi, 8.75f);
        Title = gcnew Drawing::Font(displaySemi, 16.0f);
        H2 = gcnew Drawing::Font(displaySemi, 11.0f);
        Big = gcnew Drawing::Font(displaySemi, 15.0f);
        Mono = gcnew Drawing::Font(mono, 9.75f);
        String^ iconFont = Pick(gcnew array<String^>{"Segoe Fluent Icons", "Segoe MDL2 Assets"});
        Icon = gcnew Drawing::Font(iconFont, 11.0f);
        IconLarge = gcnew Drawing::Font(iconFont, 13.0f);
        frames_ = gcnew Dictionary<Control^, InputFrame^>();
        Set(true);
    }

    static String^ Pick(array<String^>^ names) {
        for each (String^ n in names)
            if (IsFontInstalled(n)) return n;
        return "Segoe UI";
    }
    static bool IsFontInstalled(String^ name) {
        Drawing::Font^ f = gcnew Drawing::Font(name, 10.0f);
        bool ok = f->Name == name;
        delete f;
        return ok;
    }

    static Color Mix(Color a, Color b, float t) {
        return Color::FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));
    }

    static void Set(bool dark) {
        Dark = dark;
        if (dark) {
            Bg = Color::FromArgb(17, 19, 23);
            Surface = Color::FromArgb(25, 28, 33);
            Surface2 = Color::FromArgb(32, 36, 42);
            SurfaceHover = Color::FromArgb(38, 43, 50);
            Border = Color::FromArgb(44, 49, 57);
            InputBorder = Color::FromArgb(58, 64, 74);
            Text = Color::FromArgb(230, 232, 235);
            TextMuted = Color::FromArgb(148, 155, 165);
            Accent = Color::FromArgb(76, 141, 255);
            AccentHover = Color::FromArgb(104, 160, 255);
            AccentText = Color::White;
            Success = Color::FromArgb(64, 196, 125);
            Warning = Color::FromArgb(232, 170, 60);
            Danger = Color::FromArgb(240, 98, 98);
            Input = Color::FromArgb(20, 22, 27);
            GridLine = Color::FromArgb(36, 40, 47);
            ChartSecondary = Color::FromArgb(130, 138, 150);
        } else {
            Bg = Color::FromArgb(245, 246, 248);
            Surface = Color::White;
            Surface2 = Color::FromArgb(243, 244, 246);
            SurfaceHover = Color::FromArgb(236, 238, 241);
            Border = Color::FromArgb(224, 227, 232);
            InputBorder = Color::FromArgb(202, 207, 214);
            Text = Color::FromArgb(28, 31, 36);
            TextMuted = Color::FromArgb(98, 106, 117);
            Accent = Color::FromArgb(37, 99, 235);
            AccentHover = Color::FromArgb(29, 78, 216);
            AccentText = Color::White;
            Success = Color::FromArgb(21, 138, 76);
            Warning = Color::FromArgb(170, 104, 0);
            Danger = Color::FromArgb(200, 40, 40);
            Input = Color::White;
            GridLine = Color::FromArgb(236, 238, 241);
            ChartSecondary = Color::FromArgb(150, 156, 166);
        }
        AccentSoft = Mix(Surface, Accent, dark ? 0.16f : 0.10f);
        Selection = Mix(Surface, Accent, dark ? 0.22f : 0.14f);
        BgBrush = gcnew SolidBrush(Bg);
        SurfaceBrush = gcnew SolidBrush(Surface);
        Surface2Brush = gcnew SolidBrush(Surface2);
        HoverBrush = gcnew SolidBrush(SurfaceHover);
        TextBrush = gcnew SolidBrush(Text);
        MutedBrush = gcnew SolidBrush(TextMuted);
        AccentBrush = gcnew SolidBrush(Accent);
        InputBrush = gcnew SolidBrush(Input);
        SelectionBrush = gcnew SolidBrush(Selection);
        AccentSoftBrush = gcnew SolidBrush(AccentSoft);
        BorderPen = gcnew Pen(Border);
        InputPen = gcnew Pen(InputBorder);
        GridPen = gcnew Pen(GridLine);
        AccentPen = gcnew Pen(Accent, 2.0f);
        FocusPen = gcnew Pen(Accent, 1.0f);
    }

    static Color StateColor(String^ state) {
        if (state == nullptr) return TextMuted;
        if (state == "Filled" || state == "Dispatched" || state == "Accepted" || state == "Planned" || state == "Valid" || state == "Working" || state == "Good") return Success;
        if (state == "Review" || state == "Waiting" || state == "Unknown" || state == "PartiallyFilled" || state == "Previewed" || state == "Uncertain" || state == "Validated" ||
            state == "Incomplete" || state == "Ambiguous" || state == "Submitted" || state == "Dispatching" || state == "ReceivedByEA")
            return Warning;
        if (state == "Rejected" || state == "Invalid" || state == "Expired" || state == "Canceled" || state == "Bad") return Danger;
        return TextMuted;
    }

    static int Scale(Control^ c, int v) { return (int)(v * c->DeviceDpi / 96.0f + 0.5f); }

    static GraphicsPath^ RoundRect(RectangleF r, float radius) {
        GraphicsPath^ p = gcnew GraphicsPath();
        float d = Math::Max(2.0f, radius * 2);
        p->AddArc(r.X, r.Y, d, d, 180, 90);
        p->AddArc(r.Right - d, r.Y, d, d, 270, 90);
        p->AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p->AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        p->CloseFigure();
        return p;
    }

    // Shared check box glyph (NeoCheck, grid check columns).
    static void PaintCheck(Graphics^ g, Rectangle box, bool checked, bool enabled, bool hot) {
        SmoothingMode old = g->SmoothingMode;
        g->SmoothingMode = SmoothingMode::AntiAlias;
        RectangleF r((float)box.X + 0.5f, (float)box.Y + 0.5f, (float)box.Width - 1.0f, (float)box.Height - 1.0f);
        GraphicsPath^ p = RoundRect(r, 3.5f);
        if (checked) {
            g->FillPath(enabled ? (hot ? gcnew SolidBrush(AccentHover) : AccentBrush) : MutedBrush, p);
            Pen^ tick = gcnew Pen(AccentText, Math::Max(1.6f, box.Width / 9.0f));
            tick->StartCap = LineCap::Round;
            tick->EndCap = LineCap::Round;
            tick->LineJoin = LineJoin::Round;
            const float w = r.Width;
            g->DrawLines(tick, gcnew array<PointF>{PointF(r.X + w * 0.24f, r.Y + w * 0.52f), PointF(r.X + w * 0.43f, r.Y + w * 0.70f), PointF(r.X + w * 0.76f, r.Y + w * 0.32f)});
        } else {
            g->FillPath(InputBrush, p);
            g->DrawPath(hot ? FocusPen : gcnew Pen(enabled ? TextMuted : InputBorder), p);
        }
        g->SmoothingMode = old;
    }

    // Native dark scrollbars for standard controls (Windows 10 1809+). One call per HWND.
    static void NativeTheme(Control^ c) {
        if (!c->IsHandleCreated) return;
        if (dynamic_cast<ComboBox^>(c) != nullptr) NativeUi::SetWindowTheme(c->Handle, Dark ? "DarkMode_CFD" : "Explorer", nullptr);
        else NativeUi::SetWindowTheme(c->Handle, Dark ? "DarkMode_Explorer" : "Explorer", nullptr);
        for each (Control^ child in c->Controls) {
            if (dynamic_cast<ScrollBar^>(child) == nullptr) continue;
            if (child->IsHandleCreated) NativeUi::SetWindowTheme(child->Handle, Dark ? "DarkMode_Explorer" : "Explorer", nullptr);
            else child->HandleCreated += gcnew EventHandler(&Theme::OnHandleCreated);
        }
    }

    // Windows 11 title bar matching the theme.
    static void TitleBar(Form^ f) {
        if (!f->IsHandleCreated) return;
        int dark = Dark ? 1 : 0;
        NativeUi::DwmSetWindowAttribute(f->Handle, 20, dark, 4);  // DWMWA_USE_IMMERSIVE_DARK_MODE
        int caption = ColorTranslator::ToWin32(Surface);
        NativeUi::DwmSetWindowAttribute(f->Handle, 35, caption, 4);  // DWMWA_CAPTION_COLOR
        int border = ColorTranslator::ToWin32(Border);
        NativeUi::DwmSetWindowAttribute(f->Handle, 34, border, 4);  // DWMWA_BORDER_COLOR
    }

    // Background a control inherits: "card"/"surface" containers are Surface, everything else Bg.
    static Color ContainerBg(Control^ c) {
        for (Control^ p = c; p != nullptr; p = p->Parent) {
            String^ t = dynamic_cast<String^>(p->Tag);
            if (t == "card" || t == "surface") return Surface;
            if (t == "surface2") return Surface2;
        }
        return Bg;
    }

    // Recursively applies theme colors. Tags: "card", "surface", "surface2", "muted", "accent", "danger",
    // "title", "h2", "mono", "keep" (owner-drawn: repainted, not restyled).
    static void Apply(Control^ c) { ApplyCore(c, c->Parent != nullptr ? ContainerBg(c->Parent) : Bg); }

    static void StyleGrid(DataGridView^ g) {
        g->BackgroundColor = Surface;
        g->GridColor = GridLine;
        g->EnableHeadersVisualStyles = false;
        g->ColumnHeadersDefaultCellStyle->BackColor = Surface2;
        g->ColumnHeadersDefaultCellStyle->ForeColor = TextMuted;
        g->ColumnHeadersDefaultCellStyle->Font = Caption;
        g->ColumnHeadersDefaultCellStyle->SelectionBackColor = Surface2;
        g->ColumnHeadersDefaultCellStyle->SelectionForeColor = TextMuted;
        g->ColumnHeadersDefaultCellStyle->Padding = Windows::Forms::Padding(8, 0, 8, 0);
        g->ColumnHeadersBorderStyle = DataGridViewHeaderBorderStyle::None;
        g->ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode::DisableResizing;
        g->ColumnHeadersHeight = Scale(g, 34);
        g->DefaultCellStyle->BackColor = Surface;
        g->DefaultCellStyle->ForeColor = Text;
        g->DefaultCellStyle->SelectionBackColor = Selection;
        g->DefaultCellStyle->SelectionForeColor = Text;
        g->DefaultCellStyle->Font = Body;
        g->DefaultCellStyle->Padding = Windows::Forms::Padding(8, 0, 8, 0);
        g->AlternatingRowsDefaultCellStyle->BackColor = Surface;
        g->RowHeadersVisible = false;
        g->CellBorderStyle = DataGridViewCellBorderStyle::SingleHorizontal;
        g->RowTemplate->Height = Scale(g, 34);
    }

    // Draws a themed 1 px frame (and optional vertical text padding) in a control's non-client area.
    // Used for text boxes, list boxes and rich text boxes so every input shares one border style.
    static void Frame(Control^ c, bool padText, bool border) {
        if (frames_->ContainsKey(c)) {
            frames_[c]->Repaint();
            return;
        }
        frames_[c] = gcnew InputFrame(c, padText, border);
    }

private:
    static Dictionary<Control^, InputFrame^>^ frames_;

    ref class InputFrame : public NativeWindow {
    public:
        Control^ C;
        bool Pad, Border;
        InputFrame(Control^ c, bool pad, bool border) {
            C = c;
            Pad = pad;
            Border = border;
            c->HandleCreated += gcnew EventHandler(this, &InputFrame::OnCreated);
            c->HandleDestroyed += gcnew EventHandler(this, &InputFrame::OnDestroyed);
            c->GotFocus += gcnew EventHandler(this, &InputFrame::OnFocus);
            c->LostFocus += gcnew EventHandler(this, &InputFrame::OnFocus);
            c->Disposed += gcnew EventHandler(this, &InputFrame::OnDisposed);
            if (c->IsHandleCreated) Attach();
        }
        void Repaint() {
            if (Handle != IntPtr::Zero) PaintFrame();
        }

    protected:
        virtual void WndProc(Message% m) override {
            const int WM_NCCALCSIZE = 0x83, WM_NCHITTEST = 0x84, WM_NCPAINT = 0x85;
            if (Pad && m.Msg == WM_NCCALCSIZE) {
                NativeWindow::WndProc(m);
                // lParam points at a RECT (first member of NCCALCSIZE_PARAMS too): the proposed client area.
                int top = Marshal::ReadInt32(m.LParam, 4), bottom = Marshal::ReadInt32(m.LParam, 12);
                const int textH = C->Font->Height;
                const int edge = Border ? 1 : 0;  // our 1 px border lives in the non-client band
                const int pad = Math::Max(edge, (bottom - top - textH) / 2);
                const int side = Scale(C, 7) + edge;
                Marshal::WriteInt32(m.LParam, 0, Marshal::ReadInt32(m.LParam, 0) + side);
                Marshal::WriteInt32(m.LParam, 4, top + pad);
                Marshal::WriteInt32(m.LParam, 8, Marshal::ReadInt32(m.LParam, 8) - side);
                Marshal::WriteInt32(m.LParam, 12, bottom - pad);
                return;
            }
            if (Pad && m.Msg == WM_NCHITTEST) {
                NativeWindow::WndProc(m);
                const int r = m.Result.ToInt32();
                if (r == 0 || r == 18) m.Result = IntPtr(1);  // padding clicks focus the box (HTCLIENT)
                return;
            }
            NativeWindow::WndProc(m);
            if (m.Msg == WM_NCPAINT) PaintFrame();
        }

    private:
        void Attach() {
            AssignHandle(C->Handle);
            if (Pad) NativeUi::SetWindowPos(C->Handle, IntPtr::Zero, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0004 | 0x0010 | 0x0020);  // FRAMECHANGED
            else PaintFrame();
        }
        void OnCreated(Object^, EventArgs^) { Attach(); }
        void OnDestroyed(Object^, EventArgs^) { ReleaseHandle(); }
        void OnFocus(Object^, EventArgs^) { Repaint(); }
        void OnDisposed(Object^, EventArgs^) { frames_->Remove(C); }
        void PaintFrame() {
            IntPtr dc = NativeUi::GetWindowDC(C->Handle);
            if (dc == IntPtr::Zero) return;
            try {
                Graphics^ g = Graphics::FromHdc(dc);
                const int w = C->Width, h = C->Height;
                if (Pad) {
                    WinRect wr;
                    NativeUi::GetWindowRect(C->Handle, wr);
                    Point origin = C->PointToScreen(Point::Empty);
                    Drawing::Region^ band = gcnew Drawing::Region(Rectangle(0, 0, w, h));
                    band->Exclude(Rectangle(origin.X - wr.Left, origin.Y - wr.Top, C->ClientSize.Width, C->ClientSize.Height));
                    g->FillRegion(gcnew SolidBrush(C->BackColor), band);
                }
                if (Border) g->DrawRectangle(C->Focused ? FocusPen : InputPen, 0, 0, w - 1, h - 1);
                delete g;
            } finally {
                NativeUi::ReleaseDC(C->Handle, dc);
            }
        }
    };

    static void ApplyCore(Control^ c, Color inherited) {
        String^ tag = dynamic_cast<String^>(c->Tag);
        if (tag == "keep") {
            c->Invalidate();
            return;
        }
        bool nativeScroll = false;
        Color childBg = inherited;
        if (dynamic_cast<TextBoxBase^>(c) != nullptr) {
            c->BackColor = Input;
            c->ForeColor = Text;
            if (tag == "mono") c->Font = Mono;
            TextBox^ tb = dynamic_cast<TextBox^>(c);
            RichTextBox^ rtb = dynamic_cast<RichTextBox^>(c);
            bool inUpDown = dynamic_cast<UpDownBase^>(c->Parent) != nullptr;
            const bool singleLine = tb != nullptr && !tb->Multiline;
            if (tb != nullptr) tb->BorderStyle = inUpDown || singleLine ? BorderStyle::None : BorderStyle::FixedSingle;
            if (rtb != nullptr) rtb->BorderStyle = BorderStyle::FixedSingle;
            if (singleLine && !inUpDown) {
                tb->AutoSize = false;
                tb->Height = Scale(c, ControlHeight);
            }
            Frame(c, singleLine, !inUpDown);
            nativeScroll = true;
        } else if (dynamic_cast<ComboBox^>(c) != nullptr) {
            c->BackColor = Input;
            c->ForeColor = Text;
            nativeScroll = true;
        } else if (dynamic_cast<NumericUpDown^>(c) != nullptr) {
            NumericUpDown^ n = safe_cast<NumericUpDown^>(c);
            n->BackColor = Input;
            n->ForeColor = Text;
            n->BorderStyle = BorderStyle::FixedSingle;
            n->AutoSize = false;
            n->Height = Scale(c, ControlHeight);
            n->Paint -= gcnew PaintEventHandler(&Theme::OnUpDownPaint);
            n->Paint += gcnew PaintEventHandler(&Theme::OnUpDownPaint);
            if (n->Controls->Count > 0) {
                Control^ spin = n->Controls[0];
                spin->Paint -= gcnew PaintEventHandler(&Theme::OnSpinPaint);
                spin->Paint += gcnew PaintEventHandler(&Theme::OnSpinPaint);
                spin->Invalidate();
            }
            if (n->Controls->Count > 1) {
                Control^ edit = n->Controls[1];
                edit->GotFocus -= gcnew EventHandler(&Theme::OnUpDownFocus);
                edit->GotFocus += gcnew EventHandler(&Theme::OnUpDownFocus);
                edit->LostFocus -= gcnew EventHandler(&Theme::OnUpDownFocus);
                edit->LostFocus += gcnew EventHandler(&Theme::OnUpDownFocus);
            }
            n->Invalidate();
        } else if (dynamic_cast<Button^>(c) != nullptr) {
            Button^ b = safe_cast<Button^>(c);
            b->FlatStyle = FlatStyle::Flat;
            b->FlatAppearance->BorderColor = InputBorder;
            b->FlatAppearance->BorderSize = 1;
            b->FlatAppearance->MouseOverBackColor = SurfaceHover;
            b->Cursor = Cursors::Hand;
            if (tag == "accent") {
                b->BackColor = Accent;
                b->ForeColor = AccentText;
                b->FlatAppearance->BorderColor = Accent;
            } else {
                b->BackColor = Surface2;
                b->ForeColor = tag == "danger" ? Danger : Text;
            }
            b->Invalidate();
        } else if (dynamic_cast<DataGridView^>(c) != nullptr) {
            StyleGrid(safe_cast<DataGridView^>(c));
            nativeScroll = true;
        } else if (dynamic_cast<ListView^>(c) != nullptr) {
            c->BackColor = Surface;
            c->ForeColor = Text;
            nativeScroll = true;
        } else if (dynamic_cast<ListBox^>(c) != nullptr) {
            c->BackColor = Input;
            c->ForeColor = Text;
            safe_cast<ListBox^>(c)->BorderStyle = BorderStyle::FixedSingle;
            Frame(c, false, true);
            nativeScroll = true;
        } else if (dynamic_cast<Label^>(c) != nullptr || dynamic_cast<CheckBox^>(c) != nullptr || dynamic_cast<RadioButton^>(c) != nullptr) {
            c->ForeColor = tag == "muted" ? TextMuted : tag == "danger" ? Danger : tag == "accent" ? Accent : Text;
            if (tag == "title") c->Font = Title;
            else if (tag == "h2") c->Font = H2;
            c->BackColor = Color::Transparent;
            c->Invalidate();
        } else if (dynamic_cast<TabControl^>(c) != nullptr) {
            c->BackColor = inherited;
            c->ForeColor = Text;
            c->Invalidate();
        } else {
            childBg = tag == "card" || tag == "surface" ? Surface : tag == "surface2" ? Surface2 : inherited;
            c->BackColor = childBg;
            c->ForeColor = Text;
            ScrollableControl^ sc = dynamic_cast<ScrollableControl^>(c);
            nativeScroll = sc != nullptr && sc->AutoScroll;
            if (tag == "card") c->Invalidate();
        }
        if (nativeScroll) {
            if (c->IsHandleCreated) NativeTheme(c);
            else {
                c->HandleCreated -= gcnew EventHandler(&Theme::OnHandleCreated);
                c->HandleCreated += gcnew EventHandler(&Theme::OnHandleCreated);
            }
        }
        for each (Control^ child in c->Controls) ApplyCore(child, childBg);
    }

    static void OnHandleCreated(Object^ sender, EventArgs^) { NativeTheme(safe_cast<Control^>(sender)); }
    static void OnUpDownFocus(Object^ sender, EventArgs^) {
        Control^ p = safe_cast<Control^>(sender)->Parent;
        if (p != nullptr) p->Invalidate();
    }
    // NumericUpDown border (drawn in its client area) matching text boxes.
    static void OnUpDownPaint(Object^ sender, PaintEventArgs^ e) {
        Control^ c = safe_cast<Control^>(sender);
        e->Graphics->DrawRectangle(c->ContainsFocus ? FocusPen : InputPen, 0, 0, c->Width - 1, c->Height - 1);
    }
    static void OnSpinPaint(Object^ sender, PaintEventArgs^ e) {
        Control^ c = safe_cast<Control^>(sender);
        Graphics^ g = e->Graphics;
        const int w = c->Width, h = c->Height, mid = h / 2;
        g->FillRectangle(InputBrush, 0, 0, w, h);
        g->SmoothingMode = SmoothingMode::AntiAlias;
        Pen^ chev = gcnew Pen(TextMuted, 1.4f);
        const float cx = w / 2.0f, a = Math::Max(3.0f, w / 5.0f), uy = mid / 2.0f + 1, dy = mid + (h - mid) / 2.0f - 1;
        g->DrawLines(chev, gcnew array<PointF>{PointF(cx - a, uy + a / 2), PointF(cx, uy - a / 2), PointF(cx + a, uy + a / 2)});
        g->DrawLines(chev, gcnew array<PointF>{PointF(cx - a, dy - a / 2), PointF(cx, dy + a / 2), PointF(cx + a, dy - a / 2)});
    }
};

}  // namespace SignalRelayUI
