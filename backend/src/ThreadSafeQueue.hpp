#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

template<typename T>
class ThreadSafeQueue {
public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            q_.push(std::move(item));
        }
        cv_.notify_one();
    }

    // Blocking pop with timeout. Returns false on timeout.
    bool pop(T& item, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mu_);
        if (!cv_.wait_for(lock, timeout, [this]{ return !q_.empty(); }))
            return false;
        item = std::move(q_.front());
        q_.pop();
        return true;
    }

    // Non-blocking pop
    bool tryPop(T& item) {
        std::lock_guard<std::mutex> lock(mu_);
        if (q_.empty()) return false;
        item = std::move(q_.front());
        q_.pop();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return q_.size();
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::queue<T> q_;
};
