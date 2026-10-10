#pragma once
// Base class for navigation pages. Pages never make trading decisions; they render engine snapshots
// and send explicit user commands.

#include "Controls.h"
#include "EngineClient.h"

using namespace System;
using namespace System::Windows::Forms;

namespace PipkiteUI {

public ref class PageBase : public UserControl {
public:
    EngineClient^ Client;
    JObj^ LastSnapshot;

    PageBase() {
        AutoScaleMode = Windows::Forms::AutoScaleMode::Dpi;
        Dock = DockStyle::Fill;
        Font = Theme::Body;
        DoubleBuffered = true;
    }

    virtual void OnSnapshot(JObj^ snapshot) { LastSnapshot = snapshot; }
    virtual void OnActivated() {}
    virtual void OnThemeChanged() {
        Theme::Apply(this);
        Invalidate(true);
    }

protected:
    void ShowError(String^ message) { Prompt::Error(FindForm(), message); }
    void ShowInfo(String^ title, String^ message) { Prompt::Info(FindForm(), title, message); }

    // Generic "ok or show error" response handler.
    void OnSimpleResult(bool ok, Object^ result, String^ error) {
        if (!ok) ShowError(error);
    }
    ResponseHandler^ SimpleHandler() { return gcnew ResponseHandler(this, &PageBase::OnSimpleResult); }

    static Label^ Header(String^ title, String^ subtitle, Control^ parent) {
        Panel^ p = gcnew Panel();
        p->Dock = DockStyle::Top;
        p->Height = subtitle == nullptr ? 72 : 94;
        p->Padding = Windows::Forms::Padding(Theme::Gutter - 2, 20, Theme::Gutter, 12);
        Label^ sub = nullptr;
        if (subtitle != nullptr) {
            sub = L::Text(subtitle, "muted");
            sub->Dock = DockStyle::Top;
            sub->AutoSize = false;
            sub->Height = 22;
            p->Controls->Add(sub);
        }
        Label^ t = L::Text(title, "title");
        t->Dock = DockStyle::Top;
        t->AutoSize = false;
        t->Height = 36;
        p->Controls->Add(t);
        parent->Controls->Add(p);
        return t;
    }
};

}  // namespace PipkiteUI
