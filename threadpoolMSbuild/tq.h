#pragma once


#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

using Task = std::function<void()>;

//原版任务队列
class TaskQueue {
public:
    void push(Task&& task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.emplace(std::move(task));
        }
        cond_.notify_one();
    }

    bool try_pop(Task& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void wait_and_pop(Task& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty(); });
        out = std::move(queue_.front());
        queue_.pop();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::queue<Task> queue_;
    std::condition_variable cond_;
};
