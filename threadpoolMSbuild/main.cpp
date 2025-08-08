//#include "threadpool.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <future>
#include "tp.h"

// Test function: simple task that returns void
void printMessage(const std::string& msg) {
    std::cout << "Message: " << msg << " (Thread ID: " << std::this_thread::get_id() << ")\n";
}

// Test function: task with return value
int fibonacci(int n) {
    if (n < 2) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

// Test function: task with sleep to demonstrate concurrent execution
void sleepAndPrint(int seconds) {
    std::cout << "Starting task that will sleep for " << seconds << " seconds (Thread ID: " 
              << std::this_thread::get_id() << ")\n";
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    std::cout << "Finished " << seconds << " second sleep (Thread ID: " 
              << std::this_thread::get_id() << ")\n";
}

//int main() {
//    // Create thread pool with 4 threads
//    ThreadPool pool(4);
//    
//    std::cout << "1. Testing basic task execution:\n";
//    auto future1 = pool.Enqueue(printMessage, "Hello from thread pool!");
//    future1.get(); // Wait for completion
//
//    std::cout << "\n2. Testing tasks with return values:\n";
//    auto future2 = pool.Enqueue(fibonacci, 10);
//    std::cout << "Fibonacci(10) = " << future2.get() << std::endl;
//
//    std::cout << "\n3. Testing concurrent execution with sleep tasks:\n";
//    std::vector<std::future<void>> futures;
//    
//    // Enqueue several tasks with different sleep durations
//    for(int i = 1; i <= 3; ++i) {
//        futures.emplace_back(pool.Enqueue(sleepAndPrint, i));
//    }
//
//    // Wait for all sleep tasks to complete
//    for(auto& future : futures) {
//        future.wait();
//    }
//
//    std::cout << "\n4. Testing multiple calculations:\n";
//    std::vector<std::future<int>> calculation_futures;
//    
//    // Calculate Fibonacci numbers concurrently
//    for(int i = 10; i < 13; ++i) {
//        calculation_futures.emplace_back(pool.Enqueue(fibonacci, i));
//    }
//
//    // Get and print results
//    for(size_t i = 0; i < calculation_futures.size(); ++i) {
//        std::cout << "Fibonacci(" << (i + 10) << ") = " << calculation_futures[i].get() << std::endl;
//    }
//
//    return 0;
//}


int main() {
    const int THREAD_COUNT = 8;        // 线程池线程数
    const int TASK_COUNT = 1000000;     // 压力任务总量

    ThreadPool pool(THREAD_COUNT);

    std::vector<std::future<void>> results;
    std::atomic<int> counter{0};

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < TASK_COUNT; ++i) {
        results.push_back(pool.Enqueue([&counter]() {
            // 模拟一定的负载：加法操作 + 稍许延迟
            int x = counter.fetch_add(1, std::memory_order_relaxed);
            if (x % 10000 == 0) {
                std::ostringstream oss;
                oss << "Executed " << x << " tasks\n";
                std::cout << oss.str();
            }
        }));
    }

    // 等待所有任务完成
    for (auto& f : results) {
        f.get();
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "\n====== 压力测试结果 ======\n";
    std::cout << "线程数: " << THREAD_COUNT << "\n";
    std::cout << "任务总数: " << TASK_COUNT << "\n";
    std::cout << "总耗时: " << duration_ms << " ms\n";
    std::cout << "平均每个任务耗时: " << duration_ms / TASK_COUNT << " ms\n";
    std::cout << "任务执行总数: " << counter.load() << "\n";

    return 0;
}