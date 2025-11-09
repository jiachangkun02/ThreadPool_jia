//
// Created by jiachangkun on 2025/11/9.
//
#pragma once

#ifndef THREADPOOL_CPP20_THREADPOOL_H
#define THREADPOOL_CPP20_THREADPOOL_H

#include <iostream>       // 标准输入输出 (用于日志)
#include <thread>         // 线程对象 (std::thread)
#include <vector>         // 存储核心线程
#include <queue>          // 存储任务 (std::queue)
#include <functional>     // std::function (用于任务包装)
#include <future>         // std::future, std::packaged_task
#include <mutex>          // 互斥锁 (std::mutex, std::unique_lock)
#include <condition_variable> // 条件变量
#include <memory>         // 智能指针 (std::shared_ptr, ...)
#include <type_traits>    // std::invoke_result_t
#include <stdexcept>      // std::runtime_error
#include <chrono>         // 时间库 (seconds, milliseconds)
#include <unordered_set>  // 用于存储线程ID

// /**
//  * @brief C++20 线程池
//  *
//  * @note
//  * 继承 std::enable_shared_from_this
//  * 以确保动态线程的生命周期安全。
//  * 必须通过 ThreadPool::Create() 工厂函数创建。
//  */
// class ThreadPool : public std::enable_shared_from_this<ThreadPool> {
// public:
// 	// C++11: 任务类型别名
//     using Task = std::function<void()>;
//
// // 线程池返回的Future对象 (解决任务依赖死锁)
// template <typename R>
// class ThreadPoolFuture {
// public:
//     // C++11: 构造函数，保存 internal_future 和 线程池的 weak_ptr
//     ThreadPoolFuture(std::future<R>&& f,
//                      std::weak_ptr<ThreadPool> p)
//         : internal_future_(std::move(f)),
//           pool_ptr_(std::move(p)) {}
//
//     // C++11: 允许移动
//     ThreadPoolFuture(ThreadPoolFuture&&) = default;
//     ThreadPoolFuture& operator=(ThreadPoolFuture&&) = default;
//     // C++11: 禁止拷贝
//     ThreadPoolFuture(const ThreadPoolFuture&) = delete;
//     ThreadPoolFuture& operator=(const ThreadPoolFuture&) = delete;
//
//     /**
//      * @brief 获取任务结果 (死锁安全)
//      * @return R 任务的返回值
//      */
//     R get() {
//         // C++17: 逻辑: 检查线程池是否存活
//         std::shared_ptr<ThreadPool> pool = pool_ptr_.lock();
//
//         // C++11: 逻辑: 检查是否为工作线程
//         if (pool && pool->IsWorkerThread()) {
//             // 功能: 是工作线程，必须"内联执行"以防死锁
//             // C++11: 逻辑: 只要 future 还没准备好...
//             while (internal_future_.wait_for(std::chrono::seconds(0)) !=
//                    std::future_status::ready) {
//
//                 // C++11: 功能: 工作线程尝试执行一个任务，而不是阻塞
//                 if (!pool->TryExecuteTask()) {
//                     // C++11: 逻辑: 队列空了或锁冲突，让出 CPU
//                     std::this_thread::yield();
//                 }
//             }
//             // C++26: C++26
//             //         的 `std::future`
//             //         可能会有 `.is_ready()`
//             //         方法，使这个循环更简洁。
//         }
//
//         // 逻辑: 1. 非工作线程 2. 或任务已就绪 3. 或池已销毁
//         // C++11: 功能: 安全获取结果
//         return internal_future_.get();
//     }
//
// private:
// 	// C++11: 真实的 std::future
//     std::future<R> internal_future_;
//     // C++11: 必须使用 weak_ptr 避免循环引用
//     std::weak_ptr<ThreadPool> pool_ptr_;
// };
//
//
// public:
//     /**
//      * @brief 线程池创建函数 (工厂模式)
//      * 必须通过此函数创建实例。
//      */
//     // C++11: 功能: 声明 Create 工厂函数
//     static std::shared_ptr<ThreadPool> Create(
//         size_t core_threads = std::thread::hardware_concurrency(),
//         size_t max_threads = std::thread::hardware_concurrency() * 2,
//         size_t max_queue_size = 1000,
//         int idle_timeout_ms = 60000);
//
//
//     /**
//      * @brief 线程池析构函数
//      */
//     // C++11: 功能: 声明析构函数
//     ~ThreadPool();
//
//     // C++11: 禁止拷贝和赋值
//     ThreadPool(const ThreadPool&) = delete;
//     ThreadPool& operator=(const ThreadPool&) = delete;
//
//     /**
//      * @brief 提交任务到线程池 (异步)
// 	 * (死lok安全 + 停机安全 + 生产者阻塞)
//      *
//      * @note
//      * (新)
//      * 作为模板函数，实现必须在头文件中。
//      */
//     template<typename F, typename... Args>
//     auto Submit(F&& func, Args&&... args)
//         -> ThreadPoolFuture<std::invoke_result_t<F, Args...>> {
//
//         // C++17: 逻辑: 获取函数F(Args...)的返回类型
//         using ReturnType = std::invoke_result_t<F, Args...>;
//
//         // C++11: 功能: 使用 packaged_task 包装任务以获取 future
//         // C++11: 逻辑: 包装到 shared_ptr 中以安全地传递到队列
//         auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
//             // C++11: 功能: 绑定参数
//             std::bind(std::forward<F>(func), std::forward<Args>(args)...)
//             // C++20: C++20
//             //         可以使用模板 lambda
//             //         和 std::make_tuple
//             //         来捕获参数包，
//             //         `std::bind` 更兼容 C++11/14/17。
//         );
//
//         // C++11: 功能: 获取与 task 关联的 future
//         std::future<ReturnType> internal_future = task_ptr->get_future();
//
//         // C++11: 功能: 将 packaged_task 包装成 void() 的通用 Task
//         Task task_wrapper = [task_ptr]() { (*task_ptr)(); };
//
//         // C++11: 【关键区域】:
//         // 逻辑: 使用主锁来协调所有状态：is_running, task queue
//         std::unique_lock<std::mutex> lock(mutex_);
//
//         // C++11: 【停机安全检查】
// 		// 逻辑: 检查是否在析构后提交
//         if (!is_running_) {
//             // C++11: 功能: 抛出异常，防止任务丢失
//             throw std::runtime_error(
//                 "Submit on stopped ThreadPool");
//         }
//
//         // C++11: 【有界队列检查 1】
// 		// 逻辑: 队列未满
//         if (tasks_.size() < max_queue_size_) {
//             // C++11: 功能: 任务入队
//             tasks_.emplace(std::move(task_wrapper));
//         } else {
//             // 逻辑: 队列已满
//
//             // C++11: 【动态线程创建】
// 			// 逻辑: 检查是否可创建新线程
//             if (total_thread_count_ < max_threads_) {
//
//                 // C++11: 【UAF 修复】
//                 // 逻辑: 获取 `shared_ptr`
//                 //       以传递给新线程
//                 auto self = shared_from_this();
//
//                 // C++11: 功能: 创建"非核心"线程
//                 // C++11: 逻辑: lambda 捕获 `self`，
//                 //       保证线程池对象存活
//                 std::thread dynamic_thread(
//                     [self, this]() { this->WorkerThread(false); }
//                 );
//
//                 // C++11: 逻辑: 动态线程 *创建后* //       才增加计数和入队
//                 total_thread_count_++; // 功能: 增加总线程数
//                 // C++11: 功能: 注册新线程的真实 ID
//                 worker_ids_.insert(dynamic_thread.get_id());
//
//                 // C++11: 功能: 分离线程，`self` 会保证其安全
//                 dynamic_thread.detach();
//
//                 // C++11: 功能: 入队
//                 tasks_.emplace(std::move(task_wrapper));
//             } else {
//                 // C++11: 【生产者阻塞】
//                 // 逻辑: 队列已满且达到最大线程数，
//                 //       阻塞当前提交线程
//                 not_full_cv_.wait(lock, [this] {
//                     // C++11: 逻辑: 等待，
//                     //       直到 1.被唤醒 2.队列不满 3.或线程池关闭
//                     return !is_running_ || tasks_.size() < max_queue_size_;
//                 });
//
//                 // C++11: 逻辑: 阻塞后再次检查
//                 if (!is_running_) { // 逻辑: 检查是否在等待时关闭
//                     // C++11: 功能: 抛出异常
//                     throw std::runtime_error(
//                         "Submit on stopped ThreadPool");
//                 }
//
//                 // C++11: 功能: 入队
//                 tasks_.emplace(std::move(task_wrapper));
//             }
//         }
//
//         // C++11: 逻辑: *立即*解锁，然后再通知
//         lock.unlock();
//
//         // C++11: 【唤醒工作线程】
// 		// 功能: 唤醒一个可能在睡眠的线程
//         not_empty_cv_.notify_one();
//
//         // C++11: 【返回自定义Future】
//         // 逻辑: 内部存储为 `weak_ptr`
//         return ThreadPoolFuture<ReturnType>(
//             std::move(internal_future), shared_from_this());
//     }
//
// // 构造函数设为 private，强制使用 Create() 工厂
// private:
//     /**
//      * @brief 私有构造函数
//      */
//     // C++11: 功能: 声明私有构造函数
//     explicit ThreadPool(
//         size_t core_threads,
//         size_t max_threads,
//         size_t max_queue_size,
//         int idle_timeout_ms);
//
//     /**
//      * @brief 启动核心线程。
//      * 必须在 shared_ptr 构造完成后调用。
//      */
//     // C++11: 功能: 声明 init 辅助函数
//     void init();
//
//
// private:
//     /**
//      * @brief 检查当前是否为工作线程
//      * (用于 ThreadPoolFuture::get())
//      */
//     // C++11: 功能: 声明 IsWorkerThread
//     bool IsWorkerThread() const;
//
//     /**
//      * @brief 尝试执行一个任务 (用于内联执行)
//      * @return true 如果成功执行了一个任务, false 如果队列为空或锁冲突
//      */
//     // C++11: 功能: 声明 TryExecuteTask
//     bool TryExecuteTask();
//
//
//     /**
//      * @brief 工作线程的主循环
//      * @param is_core_thread 标记是否为核心线程 (永不销毁)
//      */
//     // C++11: 功能: 声明 WorkerThread
//     void WorkerThread(bool is_core_thread);
//
// private:
//     // 线程池成员变量
//
//
//     // C++11: 同步原语
//     mutable std::mutex mutex_; // 主互斥锁，保护所有共享状态
//     std::condition_variable not_empty_cv_; // 任务队列"非空"条件变量
//     std::condition_variable not_full_cv_;  // 任务队列"未满"条件变量
//
//     // C++: 任务队列
//     std::queue<Task> tasks_; // 存储待执行任务
//
//     // C++11: 状态标志
//     std::atomic<bool> is_running_; // 线程池运行状态 (原子变量，提高可见性)
//
//     // C++: 线程计数与配置
//     size_t core_threads_; // 核心线程数
//     size_t max_threads_;  // 最大线程数
//     size_t max_queue_size_; // 任务队列上限
//     std::chrono::milliseconds idle_timeout_; // 空闲超时时间
//     size_t total_thread_count_; // 当前总线程数 (受 mutex_ 保护)
//     size_t idle_thread_count_;  // 当前空闲线程数 (受 mutex_ 保护)
//
//     // C++11: 线程管理
//     // 存储核心线程对象(用于 join)
//     std::vector<std::thread> core_thread_objects_;
//     // 所有工作线程的ID (用于 IsWorkerThread)
//     std::unordered_set<std::thread::id> worker_ids_;
// };


/**
 * @brief "最优版" 线程池 (V3: 动态缓存 & 内存安全)
 *
 * @note
 * 继承 std::enable_shared_from_this
 * 以确保动态线程的生命周期安全。
 * 必须通过 ThreadPool::Create() 工厂函数创建。
 */
class ThreadPool : public std::enable_shared_from_this<ThreadPool> {
public:
	// C++11: 任务类型别名
    using Task = std::function<void()>;

// 线程池返回的Future对象 (解决任务依赖死锁)
template <typename R>
class ThreadPoolFuture {
public:
    // C++11: 构造函数，保存 internal_future 和 线程池的 weak_ptr
    ThreadPoolFuture(std::future<R>&& f,
                     std::weak_ptr<ThreadPool> p)
        : internal_future_(std::move(f)),
          pool_ptr_(std::move(p)) {}

    // C++11: 允许移动
    ThreadPoolFuture(ThreadPoolFuture&&) = default;
    ThreadPoolFuture& operator=(ThreadPoolFuture&&) = default;
    // C++11: 禁止拷贝
    ThreadPoolFuture(const ThreadPoolFuture&) = delete;
    ThreadPoolFuture& operator=(const ThreadPoolFuture&) = delete;

    /**
     * @brief 获取任务结果 (死锁安全)
     * @return R 任务的返回值
     */
    R get() {
        // C++17: 逻辑: 检查线程池是否存活
        std::shared_ptr<ThreadPool> pool = pool_ptr_.lock();

        // C++11: 逻辑: 检查是否为工作线程
        if (pool && pool->IsWorkerThread()) {
            // 功能: 是工作线程，必须"内联执行"以防死锁
            // C++11: 逻辑: 只要 future 还没准备好...
            while (internal_future_.wait_for(std::chrono::seconds(0)) !=
                   std::future_status::ready) {

                // C++11: 功能: 工作线程尝试执行一个任务，而不是阻塞
                if (!pool->TryExecuteTask()) {
                    // C++11: 逻辑: 队列空了或锁冲突，让出 CPU
                    std::this_thread::yield();
                }
            }
            // C++26: C++26
            //         的 `std::future`
            //         可能会有 `.is_ready()`
            //         方法，使这个循环更简洁。
        }

        // 逻辑: 1. 非工作线程 2. 或任务已就绪 3. 或池已销毁
        // C++11: 功能: 安全获取结果
        return internal_future_.get();
    }

private:
	// C++11: 真实的 std::future
    std::future<R> internal_future_;
    // C++11: 必须使用 weak_ptr 避免循环引用
    std::weak_ptr<ThreadPool> pool_ptr_;
};


public:
    /**
     * @brief 线程池创建函数 (工厂模式)
     * 必须通过此函数创建实例。
     */
    // C++11: 功能: 声明 Create 工厂函数
    static std::shared_ptr<ThreadPool> Create(
        size_t core_threads = std::thread::hardware_concurrency(),
        size_t max_threads = std::thread::hardware_concurrency() * 2,
        size_t max_queue_size = 1000,
        int idle_timeout_ms = 60000);


    /**
     * @brief 线程池析构函数
     */
    // C++11: 功能: 声明析构函数
    ~ThreadPool();

    // C++11: 禁止拷贝和赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief 提交任务到线程池 (异步)
	 * (死lok安全 + 停机安全 + 生产者阻塞)
     *
     * @note
     * (新)
     * 作为模板函数，实现必须在头文件中。
     */
    template<typename F, typename... Args>
    auto Submit(F&& func, Args&&... args)
        -> ThreadPoolFuture<std::invoke_result_t<F, Args...>> {

        // C++17: 逻辑: 获取函数F(Args...)的返回类型
        using ReturnType = std::invoke_result_t<F, Args...>;

        // C++11: 功能: 使用 packaged_task 包装任务以获取 future
        // C++11: 逻辑: 包装到 shared_ptr 中以安全地传递到队列
        auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
            // C++11: 功能: 绑定参数
            std::bind(std::forward<F>(func), std::forward<Args>(args)...)
            // C++20: C++20
            //         可以使用模板 lambda
            //         和 std::make_tuple
            //         来捕获参数包，
            //         `std::bind` 更兼容 C++11/14/17。
        );

        // C++11: 功能: 获取与 task 关联的 future
        std::future<ReturnType> internal_future = task_ptr->get_future();

        // C++11: 功能: 将 packaged_task 包装成 void() 的通用 Task
        Task task_wrapper = [task_ptr]() { (*task_ptr)(); };

        // C++11: 【关键区域】:
        // 逻辑: 使用主锁来协调所有状态：is_running, task queue
        std::unique_lock<std::mutex> lock(mutex_);

        // C++11: 【停机安全检查】
		// 逻辑: 检查是否在析构后提交
        if (!is_running_) {
            // C++11: 功能: 抛出异常，防止任务丢失
            throw std::runtime_error(
                "Submit on stopped ThreadPool");
        }

        // C++11: 【有界队列检查 1】
		// 逻辑: 队列未满
        if (tasks_.size() < max_queue_size_) {
            // C++11: 功能: 任务入队
            tasks_.emplace(std::move(task_wrapper));
        } else {
            // 逻辑: 队列已满

            // C++11: 【动态线程创建】
			// 逻辑: 检查是否可创建新线程
            if (total_thread_count_ < max_threads_) {

                // C++11: 【UAF 修复】
                // 逻辑: 获取 `shared_ptr`
                //       以传递给新线程
                auto self = shared_from_this();

                // C++11: 功能: 创建"非核心"线程
                // C++11: 逻辑: lambda 捕获 `self`，
                //       保证线程池对象存活
                std::thread dynamic_thread(
                    [self, this]() { this->WorkerThread(false); }
                );

                // C++11: 逻辑: 动态线程 *创建后* //       才增加计数和入队
                total_thread_count_++; // 功能: 增加总线程数
                // C++11: 功能: 注册新线程的真实 ID
                worker_ids_.insert(dynamic_thread.get_id());

                // C++11: 功能: 分离线程，`self` 会保证其安全
                dynamic_thread.detach();

                // C++11: 功能: 入队
                tasks_.emplace(std::move(task_wrapper));
            } else {
                // C++11: 【生产者阻塞】
                // 逻辑: 队列已满且达到最大线程数，
                //       阻塞当前提交线程
                not_full_cv_.wait(lock, [this] {
                    // C++11: 逻辑: 等待，
                    //       直到 1.被唤醒 2.队列不满 3.或线程池关闭
                    return !is_running_ || tasks_.size() < max_queue_size_;
                });

                // C++11: 逻辑: 阻塞后再次检查
                if (!is_running_) { // 逻辑: 检查是否在等待时关闭
                    // C++11: 功能: 抛出异常
                    throw std::runtime_error(
                        "Submit on stopped ThreadPool");
                }

                // C++11: 功能: 入队
                tasks_.emplace(std::move(task_wrapper));
            }
        }

        // C++11: 逻辑: *立即*解锁，然后再通知
        lock.unlock();

        // C++11: 【唤醒工作线程】
		// 功能: 唤醒一个可能在睡眠的线程
        not_empty_cv_.notify_one();

        // C++11: 【返回自定义Future】
        // 逻辑: 内部存储为 `weak_ptr`
        return ThreadPoolFuture<ReturnType>(
            std::move(internal_future), shared_from_this());
    }

// 构造函数设为 private，强制使用 Create() 工厂
private:
    // C++11: (新)
    // 声明 EnableMakeShared
    // 为友元，
    // 允许它访问私有构造
    friend struct EnableMakeShared;

    /**
     * @brief 私有构造函数
     */
    // C++11: 功能: 声明私有构造函数
    explicit ThreadPool(
        size_t core_threads,
        size_t max_threads,
        size_t max_queue_size,
        int idle_timeout_ms);

    /**
     * @brief 启动核心线程。
     * 必须在 shared_ptr 构造完成后调用。
     */
    // C++11: 功能: 声明 init 辅助函数
    void init();


private:
    /**
     * @brief 检查当前是否为工作线程
     * (用于 ThreadPoolFuture::get())
     */
    // C++11: 功能: 声明 IsWorkerThread
    bool IsWorkerThread() const;

    /**
     * @brief 尝试执行一个任务 (用于内联执行)
     * @return true 如果成功执行了一个任务, false 如果队列为空或锁冲突
     */
    // C++11: 功能: 声明 TryExecuteTask
    bool TryExecuteTask();


    /**
     * @brief 工作线程的主循环
     * @param is_core_thread 标记是否为核心线程 (永不销毁)
     */
    // C++11: 功能: 声明 WorkerThread
    void WorkerThread(bool is_core_thread);

private:
    // 线程池成员变量
	// ------------------------------------

    // C++11: 同步原语
    mutable std::mutex mutex_; // 主互斥锁，保护所有共享状态
    std::condition_variable not_empty_cv_; // 任务队列"非空"条件变量
    std::condition_variable not_full_cv_;  // 任务队列"未满"条件变量

    // C++: 任务队列
    std::queue<Task> tasks_; // 存储待执行任务

    // C++11: 状态标志
    std::atomic<bool> is_running_; // 线程池运行状态 (原子变量，提高可见性)

    // C++: 线程计数与配置
    size_t core_threads_; // 核心线程数
    size_t max_threads_;  // 最大线程数
    size_t max_queue_size_; // 任务队列上限
    std::chrono::milliseconds idle_timeout_; // 空闲超时时间
    size_t total_thread_count_; // 当前总线程数 (受 mutex_ 保护)
    size_t idle_thread_count_;  // 当前空闲线程数 (受 mutex_ 保护)

    // C++11: 线程管理
    // 存储核心线程对象(用于 join)
    std::vector<std::thread> core_thread_objects_;
    // 所有工作线程的ID (用于 IsWorkerThread)
    std::unordered_set<std::thread::id> worker_ids_;
};

#endif //THREADPOOL_CPP20_THREADPOOL_H