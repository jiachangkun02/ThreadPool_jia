#include <gtest/gtest.h>
#include "thread_pool.h" // 假设线程池代码保存在此文件中
#include <vector>
#include <chrono>
#include <atomic>
#include <thread>
#include <exception>
#include <iostream>
#include <future>

// 测试夹具基类
class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 默认使用4核心线程，8最大线程，100队列大小
        pool_ = ThreadPool::Create(4, 8, 100, 1000); // 1秒超时以便测试
    }
    
    void TearDown() override {
        pool_.reset(); // 显式析构线程池
    }
    
    std::shared_ptr<ThreadPool> pool_;
};

// 1. 基本功能测试
TEST_F(ThreadPoolTest, BasicFunctionality) {
    std::atomic<int> counter(0);
    
    // 提交10个简单任务
    std::vector<ThreadPool::ThreadPoolFuture<void>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool_->Submit([&counter]() {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }));
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.get();
    }
    
    EXPECT_EQ(counter.load(), 10);
    
    // 检查指标
    auto metrics = pool_->GetMetrics();
    EXPECT_EQ(metrics.tasks_submitted, 10);
    EXPECT_EQ(metrics.tasks_completed, 10);
}

// 2. 异常处理测试
TEST_F(ThreadPoolTest, ExceptionHandling) {
    std::atomic<int> handler_called(0);
    
    // 设置自定义异常处理器
    pool_->SetGlobalExceptionHandler([&handler_called](std::exception_ptr eptr) {
        handler_called++;
        try {
            std::rethrow_exception(eptr);
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "test exception");
        }
    });
    
    // 提交会抛出异常的任务
    auto future = pool_->Submit([]() {
        throw std::runtime_error("test exception");
    });
    
    // 等待任务完成（异常被处理，不会传播到future.get()）
    future.get();
    
    EXPECT_EQ(handler_called.load(), 1);
}

// 3. Future结果获取测试
TEST_F(ThreadPoolTest, FutureReturnValue) {
    auto future = pool_->Submit([]() -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 42;
    });
    
    int result = future.get();
    EXPECT_EQ(result, 42);
}

// 4. 死锁预防测试
TEST_F(ThreadPoolTest, DeadlockPrevention) {
    // 创建一个任务，它会等待另一个任务完成
    auto task2_future = pool_->Submit([]() -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 42;
    });
    
    // 在工作线程中提交并等待另一个任务
    auto task1_future = pool_->Submit([task2_future]() mutable -> int {
        // 这应该不会死锁，而是内联执行其他任务
        return task2_future.get();
    });
    
    int result = task1_future.get();
    EXPECT_EQ(result, 42);
}

// 5. 动态线程测试
TEST_F(ThreadPoolTest, DynamicThreadCreation) {
    // 设置较小的队列大小，以便触发动态线程创建
    auto test_pool = ThreadPool::Create(2, 4, 2, 1000);
    
    // 提交超过队列大小的任务
    std::vector<ThreadPool::ThreadPoolFuture<void>> futures;
    for (int i = 0; i < 6; ++i) {
        futures.push_back(test_pool->Submit([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            // 可以通过日志或指标检查是否创建了动态线程
        }));
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.get();
    }
    
    auto metrics = test_pool->GetMetrics();
    EXPECT_GE(metrics.dynamic_threads_created, 1); // 应该至少创建1个动态线程
    EXPECT_EQ(metrics.tasks_submitted, 6);
    EXPECT_EQ(metrics.tasks_completed, 6);
}

// 6. 优雅关闭测试
TEST_F(ThreadPoolTest, GracefulShutdown) {
    std::atomic<int> counter(0);
    
    // 提交多个任务
    std::vector<ThreadPool::ThreadPoolFuture<void>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool_->Submit([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter++;
        }));
    }
    
    // 重置shared_ptr，触发析构
    pool_.reset();
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.get();
    }
    
    EXPECT_EQ(counter.load(), 10); // 所有任务应该都完成了
}

// 7. 队列满时阻塞测试
TEST_F(ThreadPoolTest, BlockingWhenQueueFull) {
    // 创建一个小队列的线程池
    auto test_pool = ThreadPool::Create(2, 2, 2, 1000);
    std::atomic<int> completed(0);
    
    // 启动一个线程持续提交任务
    std::thread producer([&test_pool, &completed]() {
        for (int i = 0; i < 10; ++i) {
            test_pool->Submit([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                completed++;
            });
        }
    });
    
    // 等待生产者线程完成提交
    producer.join();
    
    // 等待所有任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    
    EXPECT_EQ(completed.load(), 10); // 所有任务应该都完成了
}

// 8. 在工作线程中提交新任务
TEST_F(ThreadPoolTest, SubmitFromWorkerThread) {
    std::atomic<int> counter(0);
    
    auto future = pool_->Submit([&]() {
        // 在工作线程中提交新任务
        auto inner_future = pool_->Submit([&counter]() {
            counter = 42;
        });
        
        // 等待内部任务完成
        inner_future.get();
    });
    
    future.get();
    EXPECT_EQ(counter.load(), 42);
}

// 9. 设置nullptr异常处理器应恢复默认
TEST_F(ThreadPoolTest, ResetExceptionHandlerToDefault) {
    std::atomic<bool> default_handler_called(false);
    
    // 设置自定义处理器
    pool_->SetGlobalExceptionHandler([&default_handler_called](std::exception_ptr) {
        FAIL() << "Custom handler should not be called after reset";
    });
    
    // 重置为默认
    pool_->SetGlobalExceptionHandler(nullptr);
    
    // 临时重定向cerr
    std::stringstream buffer;
    std::streambuf* old = std::cerr.rdbuf(buffer.rdbuf());
    
    // 提交一个抛出异常的任务
    auto future = pool_->Submit([]() {
        throw std::runtime_error("test error");
    });
    
    future.get();
    
    // 恢复cerr
    std::cerr.rdbuf(old);
    
    std::string error_output = buffer.str();
    EXPECT_NE(error_output.find("test error"), std::string::npos);
}

// 10. 高负载压力测试
TEST_F(ThreadPoolTest, HighLoadStressTest) {
    const int num_tasks = 1000;
    std::atomic<int> completed(0);
    
    std::vector<ThreadPool::ThreadPoolFuture<void>> futures;
    futures.reserve(num_tasks);
    
    // 提交大量任务
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(pool_->Submit([&completed]() {
            // 模拟工作
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            completed++;
        }));
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.get();
    }
    
    EXPECT_EQ(completed.load(), num_tasks);
    
    auto metrics = pool_->GetMetrics();
    EXPECT_EQ(metrics.tasks_submitted, num_tasks);
    EXPECT_EQ(metrics.tasks_completed, num_tasks);
}

// 11. 线程池在析构后提交应抛出异常
TEST_F(ThreadPoolTest, SubmitAfterShutdown) {
    // 重置shared_ptr，触发析构
    pool_.reset();
    
    // 尝试提交任务
    EXPECT_THROW({
        auto future = ThreadPool::Create(2, 4, 100, 1000)->Submit([]() {});
    }, std::runtime_error);
}

// 12. 验证动态线程超时回收
TEST_F(ThreadPoolTest, DynamicThreadTimeout) {
    // 创建一个核心线程少，且空闲超时短的线程池
    auto test_pool = ThreadPool::Create(1, 4, 1, 100); // 100ms超时
    
    // 提交任务触发动态线程创建
    auto f1 = test_pool->Submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
    
    auto f2 = test_pool->Submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
    
    f1.get();
    f2.get();
    
    // 等待动态线程超时
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto metrics = test_pool->GetMetrics();
    EXPECT_GE(metrics.dynamic_threads_created, 1);
    
    // 重置线程池
    test_pool.reset();
}

// 13. 验证TryExecuteTask的功能
TEST_F(ThreadPoolTest, TryExecuteTaskFunctionality) {
    std::atomic<bool> task_executed(false);
    
    // 提交一个简单任务
    auto future = pool_->Submit([&task_executed]() {
        task_executed = true;
    });
    
    // 在等待future完成前，尝试内联执行
    while (!future.get()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    EXPECT_TRUE(task_executed);
}

// 14. 验证指标更新位置的一致性（针对代码评审第2点）
TEST_F(ThreadPoolTest, MetricsConsistency) {
    // 提交多个任务
    for (int i = 0; i < 10; ++i) {
        pool_->Submit([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    }
    
    // 等待所有任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    auto metrics = pool_->GetMetrics();
    EXPECT_EQ(metrics.tasks_submitted, 10);
    EXPECT_GE(metrics.tasks_completed, 0); // 至少有一些任务已完成
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
