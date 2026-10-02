#pragma once
// Local named-pipe transport. Pipes are restricted to the current user's SID, reject remote
// clients, use the first-instance flag against squatting, and clients verify the pipe owner.

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Framing.h"

namespace sr::ipc {

class PipeConnection {
public:
    explicit PipeConnection(void* handle);
    ~PipeConnection();
    PipeConnection(const PipeConnection&) = delete;
    PipeConnection& operator=(const PipeConnection&) = delete;

    // Frames and writes one payload. Thread-safe. Returns false on error/timeout (connection closed).
    bool send(const std::string& payload, unsigned timeoutMs = 2000);
    // Blocks reading frames until disconnect, malformed frame, or close(). Calls onFrame per payload.
    void readLoop(const std::function<void(std::string&&)>& onFrame);
    void close();
    bool open() const { return open_.load(); }
    unsigned long peerProcessId() const { return peerPid_; }

private:
    void* h_;
    void* readEvent_;
    void* writeEvent_;
    std::mutex writeMu_;
    std::atomic<bool> open_{true};
    unsigned long peerPid_ = 0;
    friend class PipeServer;
};

class PipeServer {
public:
    using OnConnection = std::function<void(std::shared_ptr<PipeConnection>)>;
    // maxInstances bounds concurrent clients. Each connection callback runs on its own thread.
    PipeServer(std::wstring name, unsigned maxInstances, OnConnection onConnection);
    ~PipeServer();
    bool start();
    void stop();
    const std::wstring& name() const { return name_; }

private:
    void acceptLoop();
    std::wstring name_;
    unsigned maxInstances_;
    OnConnection onConnection_;
    std::atomic<bool> running_{false};
    void* stopEvent_ = nullptr;
    std::thread thread_;
    std::mutex workersMu_;
    std::vector<std::thread> workers_;
    std::vector<std::weak_ptr<PipeConnection>> live_;
    bool firstInstance_ = true;
};

// Connects to a per-user pipe; verifies the server pipe is owned by the current user.
std::shared_ptr<PipeConnection> connectPipe(const std::wstring& name, unsigned timeoutMs, std::string* error = nullptr);

}  // namespace sr::ipc
