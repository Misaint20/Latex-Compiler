#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <cstddef>

namespace ipc {

// Single-threaded task pool for work that must stay off the UI thread
// (scans, binary reads, disk history). One worker keeps CPU usage flat and
// preserves issue order; tasks are lambdas with no result threading — the
// completion callback is supplied by the caller.
class TaskExecutor {
public:
    using Task = std::function<void()>;

    TaskExecutor() : worker_([this] { drain(); }) {}

    ~TaskExecutor() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
        worker_.join();
    }

    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;

    // Returns false when the queue is full; the caller must answer the
    // request itself so no frontend Promise is left hanging.
    bool try_submit(Task task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_ || tasks_.size() >= kMaxQueued) {
                return false;
            }
            tasks_.push_back(std::move(task));
        }
        cv_.notify_one();
        return true;
    }

private:
    static constexpr std::size_t kMaxQueued = 64;

    void drain() {
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopped_ || !tasks_.empty(); });
                if (tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    bool stopped_ = false;
    std::thread worker_;
};

} // namespace ipc
