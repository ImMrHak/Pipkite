#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace sr {

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::int64_t nowMs() const = 0;        // wall clock, Unix epoch milliseconds
    virtual std::int64_t monotonicUs() const = 0;  // monotonic microseconds for latency measurement
};

class SystemClock final : public IClock {
public:
    std::int64_t nowMs() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    std::int64_t monotonicUs() const override {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

class FakeClock final : public IClock {
public:
    explicit FakeClock(std::int64_t startMs = 1790000000000) : now_(startMs) {}
    std::int64_t nowMs() const override { return now_.load(); }
    std::int64_t monotonicUs() const override { return now_.load() * 1000; }
    void advance(std::int64_t ms) { now_ += ms; }
    void set(std::int64_t ms) { now_ = ms; }

private:
    std::atomic<std::int64_t> now_;
};

}  // namespace sr
