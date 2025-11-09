//
// Created by jiachangkun on 2025/11/9.
//

#include "ThreadPool.h" // C++: 包含对应的头文件

/**
 * @brief (新) 辅助类，用于 make_shared 调用私有构造
 * * @note
 * 这是实现 `Create` 工厂模式的标准技巧。
 * `std::make_shared`
 * 需要访问 `public` 构造函数，
 * 所以我们创建一个 `public`
 * 的辅助类，它继承 `ThreadPool`
 * 并调用其 `private` 构造函数。
 */
struct EnableMakeShared : public ThreadPool {
    // C++11: 功能: 公开构造函数，
    //       传递参数给私有的父类构造
    EnableMakeShared(size_t core, size_t max,
                     size_t queue, int timeout)
        : ThreadPool(core, max, queue, timeout) {}
};

/**
 * @brief 线程池创建函数 (工厂模式)
 * 必须通过此函数创建实例。
 */
// C++11: 功能: 实现 Create 工厂函数
std::shared_ptr<ThreadPool> ThreadPool::Create(
    size_t core_threads,
    size_t max_threads,
    size_t max_queue_size,
    int idle_timeout_ms)
{
    // C++11: 功能: 创建 shared_ptr (enable_shared_from_this 的要求)
    // 逻辑: 使用辅助类 EnableMakeShared
    auto pool = std::make_shared<EnableMakeShared>(
        core_threads, max_threads, max_queue_size, idle_timeout_ms);

    // C++11: 功能: 启动核心线程
    // 逻辑: 必须在 shared_ptr 创建 *之后* 调用，
    //       这样 init 内部才能调用 shared_from_this()。
    pool->init();
    return pool; // C++11: 功能: 返回管理对象的 shared_ptr
}


/**
 * @brief 线程池析构函数
 */
// C++11: 功能: 实现析构函数
ThreadPool::~ThreadPool() {
    {
        // C++11: 功能: 加锁，设置线程池停止标志
        std::unique_lock<std::mutex> lock(mutex_);
        // C++11: 逻辑: 设置原子标志为 false
        is_running_ = false;
    } // C++11: 逻辑: 锁在此释放

    // C++11: 功能: 唤醒所有等待的工作线程
    not_empty_cv_.notify_all();
    // C++11: 功能: 唤醒所有等待的生产者线程
    not_full_cv_.notify_all();

    // C++11: 功能: 等待所有*核心*线程退出
    for (std::thread& t : core_thread_objects_) {
        // C++11: 逻辑: 检查线程是否可被 join
        if (t.joinable()) {
            // C++11: 功能: 阻塞等待线程执行完毕
            t.join();
        }
    }
    // 逻辑: 动态线程 (Detached) 无需 join。
    //       `shared_ptr` 机制保证了它们退出前对象不会被销毁。
    // C++: (调试日志)
    // std::cout << "ThreadPool destroyed." << std::endl;
}


/**
 * @brief 私有构造函数
 */
// C++11: 功能: 实现私有构造函数
ThreadPool::ThreadPool(
    size_t core_threads,
    size_t max_threads,
    size_t max_queue_size,
    int idle_timeout_ms)
    // C++11: 功能: 初始化原子变量
    : is_running_(true),
      // C++11: 功能: 初始化配置
      core_threads_(core_threads),
      max_threads_(max_threads),
      max_queue_size_(max_queue_size),
      // C++11: 功能: 初始化超时时间
      idle_timeout_(std::chrono::milliseconds(idle_timeout_ms)),
      // C++11: 功能: 初始化计数器
      total_thread_count_(0),
      idle_thread_count_(0) {

    // C++: 逻辑: 确保参数合理性
    if (core_threads_ > max_threads_) {
        core_threads_ = max_threads_;
    }
}

/**
 * @brief 启动核心线程。
 * 必须在 shared_ptr 构造完成后调用。
 */
// C++11: 功能: 实现 init 辅助函数
void ThreadPool::init() {
    // C++11: 功能: 获取 `self` (shared_ptr)
    //       以传递给核心线程
    auto self = shared_from_this();
    // C++11: 功能: 保护 `worker_ids_` 和 `core_thread_objects_`
    std::lock_guard<std::mutex> lock(mutex_);

    // C++11: 功能: 预分配空间
    core_thread_objects_.reserve(core_threads_);

    // C++11: 功能: 启动所有核心线程
    for (size_t i = 0; i < core_threads_; ++i) {

        // C++11: 功能: 创建线程
        // 逻辑: 核心线程也必须捕获 `self`，
        //       以保证 `shared_ptr`
        //       的引用计数在线程退出前 > 0
        core_thread_objects_.emplace_back(
            [self, this]() { this->WorkerThread(true); });

        // C++11: 功能: 增加总线程数
        total_thread_count_++;
        // C++11: 功能: 注册真实线程 ID
        // 逻辑: 必须在 emplace_back 之后才能 get_id()
        worker_ids_.insert(core_thread_objects_.back().get_id());
    }
}


/**
 * @brief 检查当前是否为工作线程
 * (用于 ThreadPoolFuture::get())
 */
// C++11: 功能: 实现 IsWorkerThread
bool ThreadPool::IsWorkerThread() const {
    // C++11: 功能: 获取当前线程ID
    auto id = std::this_thread::get_id();
    // C++11: 功能: 加锁保护 worker_ids_ 的读取
    std::lock_guard<std::mutex> lock(mutex_);
    // C++11: 功能: 检查 ID 是否在工作集合中
    // C++11: 逻辑: 【UAF 修复】
    //       锁操作是安全的，
    //       因为 `shared_ptr` 保证了对象存活
    return worker_ids_.count(id) > 0;
}

/**
 * @brief 尝试执行一个任务 (用于内联执行)
 * @return true 如果成功执行了一个任务, false 如果队列为空或锁冲突
 */
// C++11: 功能: 实现 TryExecuteTask
bool ThreadPool::TryExecuteTask() {
    Task task;

    // C++11: 【关键】
    // 功能: 使用 try_lock
    // 逻辑: 避免内联执行（在 .get() 中调用）
    //       与 WorkerThread 发生死锁
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);

	// C++11: 逻辑: 锁失败(说明 WorkerThread
    //       正在操作) 或 队空
    if (!lock.owns_lock() || tasks_.empty()) {
        // C++11: 功能: 立即返回，不阻塞 .get()
        return false;
    }

    // C++11: 逻辑: 成功获取锁，且队列不为空
    task = std::move(tasks_.front());
    tasks_.pop(); // 功能: 任务出队

    // C++11: 逻辑: *立即*解锁，再通知
    lock.unlock();

	// C++11: 功能: 唤醒一个可能阻塞的生产者
    not_full_cv_.notify_one();

    if (task) {
        task(); // 功能: 执行任务
    }

    return true;
}


/**
 * @brief 工作线程的主循环
 * @param is_core_thread 标记是否为核心线程 (永不销毁)
 */
// C++11: 功能: 实现 WorkerThread
void ThreadPool::WorkerThread(bool is_core_thread) {
    // C++11: 逻辑: 【UAF 修复】
    //       `self` (shared_ptr)
    //       是在线程创建时（init 或 Submit）
    //       捕获的。
    //       这个 `shared_ptr` 的存在，
    //       保证了 `this` 在整个函数执行期间都有效。

    // C++11: 功能: 主循环
    while (true) {
        Task task;
		// C++11: 标记是否为空闲超时
        bool timed_out = false;

        { // C++11: 逻辑: 锁的保护域开始
            std::unique_lock<std::mutex> lock(mutex_);

			// C++11: 逻辑: 队列为空，准备睡眠
            if (tasks_.empty()) {

                // C++11: 功能: 标记为空闲
                idle_thread_count_++;

                // C++11: 【睡眠/等待】
                if (is_core_thread) {
                    // C++11: 逻辑: 核心线程，无限等待
                    not_empty_cv_.wait(lock, [this] {
                        // C++11: 逻辑: 唤醒条件：
                        //       1.不运行了 2.队列不空
                        return !is_running_ || !tasks_.empty();
                    });
                } else {
                    // C++11: 逻辑: 动态线程，带超时的等待
                    auto status = not_empty_cv_.wait_for(lock, idle_timeout_,
                    [this] {
                        // C++11: 逻辑: 唤醒条件 (同上)
                        return !is_running_ || !tasks_.empty();
                    });

                    // C++11: 【空闲回收】
					// 逻辑: 确认是超时
                    //       (status == cv_status::timeout)
                    if (!status && tasks_.empty()) {
                        // C++11: 功能: 标记超时
                        timed_out = true;
                    }
                }

                // C++11: 逻辑: 被唤醒或超时，不再空闲
                idle_thread_count_--;
            }

            // C++11: 【退出条件】
            if ((!is_running_ && tasks_.empty()) || timed_out) {
                // 逻辑: 1. 析构时，队列清空
                //       2. 动态线程空闲超时
                // C++11: 功能: 减少总数
                total_thread_count_--;
                // C++11: 功能: 移除ID
                worker_ids_.erase(std::this_thread::get_id());
                // C++11: 功能: 线程安全退出
                return;
            }

			// C++11: 逻辑: 被唤醒后检查队列
            if (!tasks_.empty()) {
                // C++11: 功能: 获取任务
                task = std::move(tasks_.front());
                // C++11: 功能: 任务出队
                tasks_.pop();
            }

        } // C++11: 逻辑: 锁在此释放

        if (task) {
            // C++11: 逻辑: *立即*通知生产者，
            //       我们消费了一个
            not_full_cv_.notify_one();
            // C++11: 【关键】
            // 功能: 在锁外执行任务
            task();
        }
    } // C++11: 逻辑: 循环结束 (仅通过 L250 的 return 退出)
}