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
#include <concurrentqueue/concurrentqueue.h>

using namespace std;

using Task=std::function<void()>;

//线程池主要部分实现（无锁队列版本实现   实测速度比原版直接提升3倍）

class ThreadPool {
public:
    ThreadPool(size_t num_threads) : is_running_(true) {
        threads_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            threads_.emplace_back([this] { WorkerThread(); });
        }
    }
 
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            is_running_ = false;
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
 
        if (!is_running_) {
            throw std::runtime_error("Enqueue on stopped ThreadPool");
        }
 
        auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(func), std::forward<Args>(args)...)
        );
 
        std::future<ReturnType> future = task_ptr->get_future();
 
        /*{
            std::lock_guard<std::mutex> lock(queue_mutex_);
            tasks_.emplace([task_ptr]() {
                (*task_ptr)();
            });
        }*/

        tasks_.enqueue(std::move(
            [task_ptr]()
            {
                (*task_ptr)();
            }
        ));
        
        condition_.notify_one();
 
        return future;
    }
 
private:
    void WorkerThread() {
        /*while (true) {
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
        }*/


        while (is_running_ || !tasks_.size_approx() == 0) {
            Task task;
            if (tasks_.try_dequeue(task)) {
                if (task) task();
            } else {
                std::this_thread::yield();
            }
        }
    }
 
    std::vector<std::thread> threads_;
    moodycamel::ConcurrentQueue<Task> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> is_running_;
};