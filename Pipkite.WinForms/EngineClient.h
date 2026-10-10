#pragma once
// Authenticated local named-pipe client for the native engine. Runs a background reader thread and
// marshals every callback to the UI thread through BeginInvoke, so forms never block on the engine.

#include "Json.h"

using namespace System;
using namespace System::Diagnostics;
using namespace System::IO;
using namespace System::IO::Pipes;
using namespace System::Security::Cryptography;
using namespace System::Security::Principal;
using namespace System::Text;
using namespace System::Threading;
using namespace System::Windows::Forms;

namespace PipkiteUI {

public delegate void ResponseHandler(bool ok, Object^ result, String^ error);
public delegate void SnapshotHandler(JObj^ snapshot);
public delegate void NotificationHandler(String^ level, String^ message);
public delegate void ConnectionHandler(bool connected, String^ detail);

public ref class EngineClient {
public:
    EngineClient(Control^ syncTarget) : sync_(syncTarget) {
        lastError_ = "";
        lastLaunch_ = DateTime::MinValue;
        writeLock_ = gcnew Object();
        pending_ = gcnew Dictionary<String^, ResponseHandler^>();
    }

    event SnapshotHandler^ SnapshotReceived;
    event NotificationHandler^ NotificationReceived;
    event ConnectionHandler^ ConnectionChanged;

    property bool Connected {
        bool get() { return authenticated_; }
    }
    property String^ LastError {
        String^ get() { return lastError_; }
    }

    static String^ DataDir() {
        String^ env = Environment::GetEnvironmentVariable("PIPKITE_DATA_DIR");
        if (!String::IsNullOrEmpty(env)) return env;
        return Path::Combine(Environment::GetFolderPath(Environment::SpecialFolder::LocalApplicationData), "Pipkite");
    }

    static String^ PipeName() {
        String^ inst = Environment::GetEnvironmentVariable("PIPKITE_INSTANCE");
        String^ sid = WindowsIdentity::GetCurrent()->User->Value;
        return "Pipkite-" + sid + (String::IsNullOrEmpty(inst) ? "" : "-" + inst) + "-ui";
    }

    void Start() {
        running_ = true;
        thread_ = gcnew Thread(gcnew ThreadStart(this, &EngineClient::Run));
        thread_->IsBackground = true;
        thread_->Name = "Pipkite engine client";
        thread_->Start();
    }

    void Stop() {
        running_ = false;
        try {
            if (pipe_ != nullptr) pipe_->Close();
        } catch (Exception^) {
        }
    }

    void Call(String^ method, Object^ params, ResponseHandler^ callback) {
        if (!authenticated_) {
            if (callback != nullptr) Post(callback, false, nullptr, "Engine is not connected");
            return;
        }
        String^ id = Guid::NewGuid().ToString();
        if (callback != nullptr) {
            Monitor::Enter(pending_);
            pending_[id] = callback;
            Monitor::Exit(pending_);
        }
        JObj^ payload = J::New();
        payload["method"] = method;
        payload["params"] = params != nullptr ? params : J::New();
        JObj^ env = Envelope("REQUEST");
        env["id"] = id;
        env["payload"] = payload;
        if (!Send(env) && callback != nullptr) {
            Monitor::Enter(pending_);
            pending_->Remove(id);
            Monitor::Exit(pending_);
            Post(callback, false, nullptr, "Could not send the request to the engine");
        }
    }

    void Call(String^ method, Object^ params) { Call(method, params, nullptr); }

private:
    Control^ sync_;
    NamedPipeClientStream^ pipe_;
    Thread^ thread_;
    Object^ writeLock_;
    Dictionary<String^, ResponseHandler^>^ pending_;
    volatile bool running_;
    volatile bool authenticated_;
    String^ lastError_;
    DateTime lastLaunch_;

    static JObj^ Envelope(String^ type) {
        JObj^ e = J::New();
        e["v"] = 1;
        e["type"] = type;
        e["id"] = "";
        e["accountKey"] = "";
        e["seq"] = gcnew Int32(0);
        e["deadline"] = gcnew Int32(0);
        e["configVersion"] = gcnew Int32(0);
        e["payload"] = J::New();
        return e;
    }

    ref class ResponseInvoker {
    public:
        ResponseHandler^ h;
        bool ok;
        Object^ result;
        String^ error;
        void Invoke() { h(ok, result, error); }
    };

    ref class SnapshotInvoker {
    public:
        EngineClient^ owner;
        JObj^ snap;
        void Invoke() { owner->RaiseSnapshot(snap); }
    };

    ref class NotifyInvoker {
    public:
        EngineClient^ owner;
        String^ level;
        String^ message;
        void Invoke() { owner->RaiseNotification(level, message); }
    };

    ref class ConnInvoker {
    public:
        EngineClient^ owner;
        bool connected;
        String^ detail;
        void Invoke() { owner->RaiseConnection(connected, detail); }
    };

internal:
    void RaiseSnapshot(JObj^ s) { SnapshotReceived(s); }
    void RaiseNotification(String^ l, String^ m) { NotificationReceived(l, m); }
    void RaiseConnection(bool c, String^ d) { ConnectionChanged(c, d); }

private:
    void Post(ResponseHandler^ h, bool ok, Object^ result, String^ error) {
        ResponseInvoker^ inv = gcnew ResponseInvoker();
        inv->h = h;
        inv->ok = ok;
        inv->result = result;
        inv->error = error;
        SafeInvoke(gcnew MethodInvoker(inv, &ResponseInvoker::Invoke));
    }

    void SafeInvoke(MethodInvoker^ m) {
        try {
            if (sync_ != nullptr && sync_->IsHandleCreated && !sync_->IsDisposed) sync_->BeginInvoke(m);
        } catch (Exception^) {
        }
    }

    void SetConnected(bool c, String^ detail) {
        authenticated_ = c;
        ConnInvoker^ inv = gcnew ConnInvoker();
        inv->owner = this;
        inv->connected = c;
        inv->detail = detail;
        SafeInvoke(gcnew MethodInvoker(inv, &ConnInvoker::Invoke));
    }

    bool Send(JObj^ env) {
        array<Byte>^ body = Encoding::UTF8->GetBytes(J::Stringify(env));
        if (body->Length > 64 * 1024) return false;
        array<Byte>^ frame = gcnew array<Byte>(body->Length + 4);
        frame[0] = (Byte)(body->Length & 0xFF);
        frame[1] = (Byte)((body->Length >> 8) & 0xFF);
        frame[2] = (Byte)((body->Length >> 16) & 0xFF);
        frame[3] = (Byte)((body->Length >> 24) & 0xFF);
        Array::Copy(body, 0, frame, 4, body->Length);
        Monitor::Enter(writeLock_);
        try {
            if (pipe_ == nullptr || !pipe_->IsConnected) return false;
            pipe_->Write(frame, 0, frame->Length);
            pipe_->Flush();
            return true;
        } catch (Exception^) {
            return false;
        } finally {
            Monitor::Exit(writeLock_);
        }
    }

    bool ReadExact(array<Byte>^ buf, int count) {
        int off = 0;
        while (off < count) {
            int n = pipe_->Read(buf, off, count - off);
            if (n <= 0) return false;
            off += n;
        }
        return true;
    }

    JObj^ ReadEnvelope() {
        array<Byte>^ hdr = gcnew array<Byte>(4);
        if (!ReadExact(hdr, 4)) return nullptr;
        int len = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (hdr[3] << 24);
        if (len <= 0 || len > 64 * 1024) return nullptr;  // validated before allocation
        array<Byte>^ body = gcnew array<Byte>(len);
        if (!ReadExact(body, len)) return nullptr;
        return J::Obj(J::Parse(Encoding::UTF8->GetString(body)));
    }

    static String^ Hex(array<Byte>^ b) {
        StringBuilder^ sb = gcnew StringBuilder();
        for each (Byte x in b) sb->Append(x.ToString("x2"));
        return sb->ToString();
    }

    void LaunchEngineIfNeeded() {
        if ((DateTime::Now - lastLaunch_).TotalSeconds < 6) return;
        lastLaunch_ = DateTime::Now;
        String^ exe = Path::Combine(Path::GetDirectoryName(Application::ExecutablePath), "Pipkite.Engine.exe");
        if (!File::Exists(exe)) {
            lastError_ = "Pipkite.Engine.exe was not found next to the application";
            return;
        }
        try {
            ProcessStartInfo^ psi = gcnew ProcessStartInfo(exe);
            psi->UseShellExecute = false;
            psi->CreateNoWindow = true;
            psi->WorkingDirectory = Path::GetDirectoryName(exe);
            Process::Start(psi);
        } catch (Exception^ ex) {
            lastError_ = "Cannot start the engine: " + ex->Message;
        }
    }

    bool Handshake() {
        array<Byte>^ secretBlob;
        try {
            secretBlob = File::ReadAllBytes(Path::Combine(DataDir(), "secrets", "ui.bin"));
        } catch (Exception^) {
            lastError_ = "Waiting for the engine to create its local secret";
            return false;
        }
        array<Byte>^ secret = ProtectedData::Unprotect(secretBlob, nullptr, DataProtectionScope::CurrentUser);
        HMACSHA256^ hmac = gcnew HMACSHA256(secret);
        array<Byte>^ nonceBytes = gcnew array<Byte>(16);
        RandomNumberGenerator::Create()->GetBytes(nonceBytes);
        String^ nonceU = Hex(nonceBytes);
        JObj^ hello = Envelope("HELLO");
        JObj^ hp = J::New();
        hp["nonce"] = nonceU;
        hp["client"] = "winforms";
        hello["payload"] = hp;
        Send(hello);
        JObj^ ch = ReadEnvelope();
        if (ch == nullptr || J::Str(ch, "type") != "CHALLENGE") return false;
        JObj^ cp = J::Obj(ch, "payload");
        String^ nonceE = J::Str(cp, "nonce");
        String^ expect = Hex(hmac->ComputeHash(Encoding::UTF8->GetBytes("engine|" + nonceU + "|" + nonceE)));
        if (expect != J::Str(cp, "proof")) {
            lastError_ = "Engine authentication failed";
            return false;  // never talk to a process that cannot prove the shared secret
        }
        JObj^ auth = Envelope("AUTH");
        JObj^ ap = J::New();
        ap["proof"] = Hex(hmac->ComputeHash(Encoding::UTF8->GetBytes("ui|" + nonceE + "|" + nonceU)));
        auth["payload"] = ap;
        Send(auth);
        JObj^ w = ReadEnvelope();
        return w != nullptr && J::Str(w, "type") == "WELCOME";
    }

    void Run() {
        while (running_) {
            try {
                pipe_ = gcnew NamedPipeClientStream(".", PipeName(), PipeDirection::InOut, PipeOptions::Asynchronous, TokenImpersonationLevel::Identification);
                try {
                    pipe_->Connect(1500);
                } catch (TimeoutException^) {
                    LaunchEngineIfNeeded();
                    SetConnected(false, "Starting the Pipkite engine...");
                    pipe_ = nullptr;
                    Thread::Sleep(700);
                    continue;
                }
                if (!Handshake()) {
                    SetConnected(false, lastError_);
                    pipe_->Close();
                    Thread::Sleep(1000);
                    continue;
                }
                SetConnected(true, "Connected");
                while (running_) {
                    JObj^ env = ReadEnvelope();
                    if (env == nullptr) break;
                    Dispatch(env);
                }
            } catch (Exception^ ex) {
                lastError_ = ex->Message;
            }
            try {
                if (pipe_ != nullptr) pipe_->Close();
            } catch (Exception^) {
            }
            FailPending();
            if (running_) {
                SetConnected(false, "Engine connection lost; reconnecting");
                Thread::Sleep(800);
            }
        }
    }

    void FailPending() {
        Monitor::Enter(pending_);
        array<ResponseHandler^>^ hs = gcnew array<ResponseHandler^>(pending_->Count);
        pending_->Values->CopyTo(hs, 0);
        pending_->Clear();
        Monitor::Exit(pending_);
        for each (ResponseHandler^ h in hs) Post(h, false, nullptr, "Engine connection lost");
    }

    void Dispatch(JObj^ env) {
        String^ type = J::Str(env, "type");
        JObj^ p = J::Obj(env, "payload");
        if (type == "RESPONSE") {
            String^ id = J::Str(env, "id");
            ResponseHandler^ h = nullptr;
            Monitor::Enter(pending_);
            if (pending_->TryGetValue(id, h)) pending_->Remove(id);
            Monitor::Exit(pending_);
            if (h != nullptr) Post(h, J::Bool(p, "ok"), J::Get(p, "result"), J::Str(p, "error"));
        } else if (type == "EVENT") {
            String^ topic = J::Str(p, "topic");
            if (topic == "snapshot") {
                SnapshotInvoker^ inv = gcnew SnapshotInvoker();
                inv->owner = this;
                inv->snap = J::Obj(p, "data");
                SafeInvoke(gcnew MethodInvoker(inv, &SnapshotInvoker::Invoke));
            } else if (topic == "notification") {
                JObj^ d = J::Obj(p, "data");
                NotifyInvoker^ inv = gcnew NotifyInvoker();
                inv->owner = this;
                inv->level = J::Str(d, "level");
                inv->message = J::Str(d, "message");
                SafeInvoke(gcnew MethodInvoker(inv, &NotifyInvoker::Invoke));
            }
        }
    }
};

}  // namespace PipkiteUI
