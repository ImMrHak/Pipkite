// Pipkite Windows Forms UI entry point (C++/CLI, .NET Framework 4.8).

#include "MainForm.h"

using namespace System;
using namespace System::Threading;
using namespace System::Windows::Forms;

namespace {

void ReportCrash(Exception^ ex) {
    try {
        String^ dir = PipkiteUI::EngineClient::DataDir();
        IO::Directory::CreateDirectory(dir);
        IO::File::AppendAllText(IO::Path::Combine(dir, "ui-errors.log"), DateTime::Now.ToString("s") + " " + ex->ToString() + Environment::NewLine);
    } catch (Exception^) {
    }
    MessageBox::Show("Pipkite hit an unexpected UI error and logged it to ui-errors.log in the data folder.\n\n" + ex->Message +
                         "\n\nThe engine keeps running independently; reopen the window from the tray or start Pipkite again.",
                     "Pipkite", MessageBoxButtons::OK, MessageBoxIcon::Error);
}

ref class CrashHandlers {
public:
    static void OnThread(Object^, ThreadExceptionEventArgs^ e) { ReportCrash(e->Exception); }
    static void OnDomain(Object^, UnhandledExceptionEventArgs^ e) {
        Exception^ ex = dynamic_cast<Exception^>(e->ExceptionObject);
        if (ex != nullptr) ReportCrash(ex);
    }
};

}  // namespace

[STAThread]
int main(array<String^>^ args) {
    Application::SetUnhandledExceptionMode(UnhandledExceptionMode::CatchException);
    Application::ThreadException += gcnew ThreadExceptionEventHandler(&CrashHandlers::OnThread);
    AppDomain::CurrentDomain->UnhandledException += gcnew UnhandledExceptionEventHandler(&CrashHandlers::OnDomain);
    // One UI per Windows session; a second launch just exits (the first window stays in the tray).
    bool created = false;
    Mutex^ single = gcnew Mutex(true, "Local\\Pipkite.UI." + Security::Principal::WindowsIdentity::GetCurrent()->User->Value +
                                          Environment::GetEnvironmentVariable("PIPKITE_INSTANCE"),
                                created);
    if (!created) return 0;
    Application::EnableVisualStyles();
    Application::SetCompatibleTextRenderingDefault(false);
    bool minimized = false;
    for each (String^ a in args)
        if (a == "--minimized") minimized = true;
    Application::Run(gcnew PipkiteUI::MainForm(minimized));
    GC::KeepAlive(single);
    return 0;
}
