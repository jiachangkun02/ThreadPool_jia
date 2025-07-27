#pragma once


#include <iostream>
#include <thread>
#include <functional>
#include <future>
#include <condition_variable>
#include <chrono>
#include <queue>
#include <memory>
#include <atomic>
#include <type_traits>

using namespace std;

using Task=std::function<void()>;

class ThreadPool {
public:
    ThreadPool(size_t num_threads) : running_(true) {
        threads_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            threads_.emplace_back([this] { WorkerThread(); });
        }
    }
 
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            running_ = false;
        }
        condition_.notify_all();
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
 
    template<typename F, typename... Args>
    auto Enqueue(F&& func, Args&&... args) //打包提交任务
        -> std::future<std::invoke_result_t<F, Args...>> {
        
        using ReturnType = std::invoke_result_t<F, Args...>;
 
        if (!running_) {
            throw std::runtime_error("Enqueue on stopped ThreadPool");
        }
 
        auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(func), std::forward<Args>(args)...)
        );
 
        std::future<ReturnType> future = task_ptr->get_future();
 
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            tasks_.emplace([task_ptr]() {
                (*task_ptr)();
            });
        }
        
        condition_.notify_one();
 
        return future;
    }
 
private:
    void WorkerThread() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [this] { return !running_ || !tasks_.empty(); });
                
                if (!running_ && tasks_.empty()) {
                    return;
                }
                
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }
 
    std::vector<std::thread> threads_;
    std::queue<Task> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> running_;
};