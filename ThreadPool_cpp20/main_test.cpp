#include <iostream>
#include "ThreadPool.h"
#include <vector>
#include <string>
#include <sstream>      // std::ostringstream
#include <atomic>       // std::atomic_int
#include <chrono>       // std::chrono::*
#include <memory>       // C++11: std::shared_ptr

// C++11: 全局原子计数器，用于验证所有任务是否都执行了
std::atomic<int> g_task_counter(0);
// C++11: 用于日志输出的全局互斥锁
std::mutex g_cout_mutex;

/**
 * @brief 线程安全的打印函数
 */
// C++11: 功能: 实现线程安全打印
void print(const std::string& msg) {
    // C++11: 功能: 使用 lock_guard 保护 std::cout
    std::lock_guard<std::mutex> lock(g_cout_mutex);
    // C++: 功能: 打印消息
    std::cout << msg << std::endl;
}

/**
 * @brief 测试任务：简单执行
 */
// C++11: 功能: 测试函数1
void simple_task(int id) {
    // C++11: 功能: stringstream 用于格式化
    std::ostringstream oss;
    oss << "[Task " << id << "] \t"
        << "Hello from thread " << std::this_thread::get_id();
    // C++11: 功能: 打印日志
    print(oss.str());

    // C++11: 功能: 模拟少量工作
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    // C++11: 功能: 原子递增
    g_task_counter++;
}

/**
 * @brief 测试任务：带返回值
 */
// C++11: 功能: 测试函数2
int task_with_return(int id) {
    std::ostringstream oss;
    oss << "[Task " << id << "] \t"
        << "Calculating... (Thread " << std::this_thread::get_id() << ")";
    print(oss.str());

    // C++11: 功能: 模拟工作
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // C++11: 功能: 原子递增
    g_task_counter++;
    // C++: 功能: 返回一个结果
    return id * 10;
}

/**
 * @brief 测试1: 死锁安全测试
 * (来自我们 V2 版本的测试)
 */
// C++11: 功能: 测试函数3
void test_deadlock_safety(std::shared_ptr<ThreadPool> pool) {
    print("\n====== 1. TEST: Deadlock Safety ======");
    print("Submitting 4 parent tasks that wait on 4 child tasks...");

    // C++11: 逻辑: 获取自定义 Future 的类型
    using FutureType = decltype(pool->Submit(task_with_return, 0));
    // C++11: 逻辑: vector 存储 futures
    std::vector<FutureType> futures;

    // C++11: 逻辑: 提交与核心线程数 (4) 相同数量的父任务
    for (int i = 0; i < 4; ++i) {
        futures.emplace_back(
            // C++11: 功能: 提交 Lambda
            pool->Submit([pool, i]() -> int { // C++11:
                                              // 逻辑: 【UAF 修复】
                                              //       捕获 shared_ptr pool

                std::ostringstream oss;
                oss << "[Parent " << i << "] \t"
                    << "STARTING on thread " << std::this_thread::get_id();
                print(oss.str());

                // C++11: 功能: 父任务提交一个子任务
                auto child_future = pool->Submit(task_with_return, 100 + i);

                // C++11: 【关键点】
                // 功能: 父任务(工作线程)调用 .get()
                // 逻辑:
                // 如果没有内联执行，这里将死锁
                int child_result = child_future.get();

                // C++: 逻辑: 清空 stringstream
                oss.str("");
                oss.clear();
                oss << "[Parent " << i << "] \t"
                    << "FINISHED (Child " << 100 + i
                    << " returned " << child_result
                    << ") on thread " << std::this_thread::get_id();
                print(oss.str());

                // C++11: 功能: 父任务计数
                g_task_counter++;
                return i * 1000;
            })
        );
    }

    // C++11: 逻辑: 等待所有父任务完成
    for (auto& f : futures) {
        // C++11: 功能: 主线程获取结果
        print("Main thread getting result: " + std::to_string(f.get()));
    }
    print("====== 1. TEST: Deadlock Safety PASSED ======");
}

/**
 * @brief 测试2: 生产者阻塞和动态线程
 * (验证来自 threadpool.h 的特性)
 */
// C++11: 功能: 测试函数4
void test_producer_blocking_and_dynamic_threads() {
    print("\n====== 2. TEST: Bounded Queue & Dynamic Threads ======");
    // C++11: 逻辑: 核心2, 最大4, 队列*极小*(2)
    // C++11: 逻辑: 【UAF 修复】
    //       使用 Create()
    //       工厂和 shared_ptr
    auto pool = ThreadPool::Create(2, 4, 2, 10000); // 10秒回收
    // C++11: 功能: 重置计数器
    g_task_counter = 0;

    print("Pool created: Core=2, Max=4, QueueSize=2");

    // C++11: 功能: 用于阻塞线程池
    auto blocking_task = [](int id) {
        std::ostringstream oss;
        oss << "[Blocker " << id << "] \t"
            << "STARTING LONG TASK (500ms) on thread "
            << std::this_thread::get_id();
        print(oss.str());
        // C++11: 功能: 长时间睡眠以占住线程
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        g_task_counter++;
        oss.str("");
        oss.clear();
        oss << "[Blocker " << id << "] \t"
            << "FINISHED on thread " << std::this_thread::get_id();
        print(oss.str());
    };

    // 逻辑: 1. 提交 4 个长任务
    // 预期:
    // Task 0, 1 -> 被 Core 0, 1 拿走
    // Task 2, 3 -> 进入大小为 2 的队列
    print("Submitting 4 tasks to fill Core(2) + Queue(2)...");
    pool->Submit(blocking_task, 0); // 核心1
    pool->Submit(blocking_task, 1); // 核心2
    pool->Submit(blocking_task, 2); // 队列1
    pool->Submit(blocking_task, 3); // 队列2

    // 逻辑: 2. 提交第 5, 6 个任务
    // 预期:
    // 队列已满 (2)。
    // 触发动态线程创建
    // Task 4 -> Dynamic Thread 3 拿走
    // Task 5 -> Dynamic Thread 4 拿走 (达到 Max=4)
    print("Submitting tasks 5 & 6 (should trigger dynamic threads)...");
    pool->Submit(blocking_task, 4); // 动态3
    pool->Submit(blocking_task, 5); // 动态4

    // 逻辑: 3. 提交第 7 个任务
    // 预期:
    // 队列已满 (0, 因为Task 2,3 还在等)。
    // 线程已满 (4)。
    // 下面的 Submit 将会 *阻塞*
    print("Submitting task 7 (should BLOCK main thread)...");
    // C++11: 功能: 计时
    auto start_wait = std::chrono::steady_clock::now();
    pool->Submit(blocking_task, 6); // C++11: 阻塞
    auto end_wait = std::chrono::steady_clock::now(); // C++11

    // C++11: 功能: 计算阻塞时间
    auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_wait - start_wait).count();

    print("...Main thread UNBLOCKED after " +
          std::to_string(wait_duration) + " ms.");

    // C++: 逻辑: 必须等待约500ms
    if (wait_duration > 400) {
        print("====== 2. TEST: Producer Blocking PASSED ======");
    } else {
        print("====== 2. TEST: Producer Blocking FAILED ======");
    }

    // C++11: 逻辑: 等待所有 (7) 个任务完成
    while (g_task_counter < 7) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    print("Producer test tasks executed: " +
          std::to_string(g_task_counter.load()));

    // C++11: 逻辑:
    //       `pool`
    //       在此析构，
    //       动态线程
    //       将在 10 秒后超时退出
    print("Producer test pool going out of scope...");
}


// C++: 功能: 主函数
int main() {
    try {
        // C++11: 逻辑: 核心=4, 最大=8, 队列=100, 60秒回收
        // C++11: 逻辑: 【UAF 修复】
        //       必须使用 Create()
        //       工厂
        auto pool = ThreadPool::Create(4, 8, 100, 60000);
        // C++11: 功能: 初始化计数器
        g_task_counter = 0;

        // C++11: 功能: 测试死锁
        test_deadlock_safety(pool);

        // C++11: 逻辑: 等待死锁测试的任务 (4父+4子=8) 完成
        while (g_task_counter < 8) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        print("Deadlock test tasks executed: " +
              std::to_string(g_task_counter.load()));

        // C++11: 功能: 测试生产者阻塞和动态线程
        test_producer_blocking_and_dynamic_threads();

        // C++11: 逻辑:
        //       g_task_counter
        //       在 test_producer_blocking_and_dynamic_threads
        //       内部被重置和等待，
        //       所以我们在这里不需要等待它

        print("All tests completed. Total tasks executed (from all tests): " +
              std::to_string(g_task_counter.load()));

    } catch (const std::exception& e) { // C++: 功能: 捕获标准异常
        print(std::string("FATAL ERROR: ") + e.what());
        return 1;
    }

    print("\nShutting down (main pool destructor will be called)...");
    // C++11: 逻辑:
    //       `pool`
    //       的 shared_ptr
    //       在此析构
    return 0;
}
