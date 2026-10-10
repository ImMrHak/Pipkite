#pragma once
// Understandable mutex/condition-variable bounded queue. Profile before replacing with lock-free code.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace sr {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    // Never blocks. Returns false on overflow so the caller can record it and inhibit admission.
    bool tryPush(T item) {
        {
            std::lock_guard lock(mu_);
            if (closed_ || items_.size() >= capacity_) return false;
            items_.push_back(std::move(item));
        }
        cv_.notify_one();
        return true;
    }

    std::optional<T> tryPop() {
        std::lock_guard lock(mu_);
        if (items_.empty()) return std::nullopt;
        T v = std::move(items_.front());
        items_.pop_front();
        return v;
    }

    template <typename Rep, typename Period>
    std::optional<T> popFor(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(mu_);
        cv_.wait_for(lock, timeout, [&] { return closed_ || !items_.empty(); });
        if (items_.empty()) return std::nullopt;
        T v = std::move(items_.front());
        items_.pop_front();
        return v;
    }

    void close() {
        {
            std::lock_guard lock(mu_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    bool closed() const {
        std::lock_guard lock(mu_);
        return closed_;
    }

    std::size_t size() const {
        std::lock_guard lock(mu_);
        return items_.size();
    }

    std::size_t capacity() const { return capacity_; }

    // Drains everything currently queued (used by Pause to reject unsubmitted entries).
    std::deque<T> drain() {
        std::lock_guard lock(mu_);
        std::deque<T> out;
        out.swap(items_);
        return out;
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<T> items_;
    bool closed_ = false;
};

}  // namespace sr
