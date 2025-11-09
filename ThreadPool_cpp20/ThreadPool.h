//
// Created by jiachangkun on 2025/11/9.
//
#pragma once

// 原始文件的宏保护
#ifndef THREADPOOL_CPP20_THREADPOOL_H
#define THREADPOOL_CPP20_THREADPOOL_H

#include <iostream>       // 标准输入输出 (用于默认异常处理器)
#include <thread>         // 线程对象 (std::thread)
#include <vector>         // 存储核心线程
#include <queue>          // 存储任务 (std::queue)
#include <functional>     // std::function (用于任务包装)
#include <future>         // std::future, std::packaged_task
#include <mutex>          // 互斥锁 (std::mutex, std::unique_lock)
#include <condition_variable> // 条件变量
#include <memory>         // 智能指针 (std::shared_ptr, std::make_shared, std::enable_shared_from_this)
#include <type_traits>    // std::invoke_result_t
#include <stdexcept>      // std::runtime_error, std::invalid_argument
#include <chrono>         // 时间库 (seconds, milliseconds)
#include <atomic>         // 用于监控指标

// 线程本地（thread_local）变量，用于无锁地标识当前是否为工作线程
namespace {
	thread_local bool g_is_worker_thread = false;
	
	// 默认配置常量
	constexpr int DEFAULT_IDLE_TIMEOUT_MS = 60000;
	constexpr size_t DEFAULT_QUEUE_SIZE = 1000;

	/**
	 * @brief RAII 守护类
	 * 确保 g_is_worker_thread 标志在工作线程退出时（无论是正常返回还是异常）
	 * 都能被正确重置为 false。
	 */
	struct ThreadFlagGuard {
		ThreadFlagGuard() { g_is_worker_thread = true; }
		~ThreadFlagGuard() { g_is_worker_thread = false; }
		// 禁止拷贝和移动
		ThreadFlagGuard(const ThreadFlagGuard&) = delete;
		ThreadFlagGuard& operator=(const ThreadFlagGuard&) = delete;
		ThreadFlagGuard(ThreadFlagGuard&&) = delete;
		ThreadFlagGuard& operator=(ThreadFlagGuard&&) = delete;
	};
}

/**
 * @brief 一个功能全面、异常安全的线程池实现
 *
 * @note 
 * 继承 std::enable_shared_from_this 
 * 以确保动态线程（捕获shared_ptr）的生命周期安全。
 * 必须通过 ThreadPool::Create() 工厂函数创建。
 */
class ThreadPool : public std::enable_shared_from_this<ThreadPool> {
public:
	// 任务类型别名
	using Task = std::function<void()>; 
	
	// 监控指标
	struct Metrics {
		std::atomic<size_t> tasks_submitted{0};
		std::atomic<size_t> tasks_completed{0};
		std::atomic<size_t> dynamic_threads_created{0};
	};

// 线程池返回的Future对象 (解决任务依赖死锁)
	template <typename R>
	class ThreadPoolFuture {
	public:
		// 构造函数，保存 internal_future 和 线程池的 weak_ptr
		ThreadPoolFuture(std::future<R>&& f, 
			std::weak_ptr<ThreadPool> p)
		: internal_future_(std::move(f)), 
		pool_ptr_(std::move(p)) {} 
		
		// 允许移动
		ThreadPoolFuture(ThreadPoolFuture&&) = default;
		ThreadPoolFuture& operator=(ThreadPoolFuture&&) = default;
		// 禁止拷贝
		ThreadPoolFuture(const ThreadPoolFuture&) = delete;
		ThreadPoolFuture& operator=(const ThreadPoolFuture&) = delete;
		
		/**
		 * @brief 获取任务结果 (死锁安全)
		 * @return R 任务的返回值
		 */
		R get() {
			// 检查线程池是否存活
			std::shared_ptr<ThreadPool> pool = pool_ptr_.lock(); 
			
			// 检查是否为工作线程
			if (pool && pool->IsWorkerThread()) {
				// 是工作线程，必须"内联执行"以防死锁
				// 只要 future 还没准备好...
				while (internal_future_.wait_for(std::chrono::seconds(0)) != 
					std::future_status::ready) {
					
					// 工作线程尝试执行一个任务，而不是阻塞
					if (!pool->TryExecuteTask()) {
						// 队列空了或锁冲突，短暂休眠，避免CPU忙等待
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
				}
			}
			
			// 检查：如果线程池已销毁且任务未完成，抛出异常
			if (!pool && internal_future_.wait_for(std::chrono::seconds(0)) != 
						 std::future_status::ready) {
				throw std::runtime_error("ThreadPool destroyed before task could complete.");
			}

			// 非工作线程，或任务已就绪，或池已销毁但任务已就绪
			// 安全获取结果
			return internal_future_.get();
		}
		
	private:
		// 真实的 std::future
		std::future<R> internal_future_; 
		// 必须使用 weak_ptr 避免循环引用
		std::weak_ptr<ThreadPool> pool_ptr_; 
	};
	
	
public:
	/**
	 * @brief 线程池创建函数 (工厂模式)
	 * 必须通过此函数创建实例。
	 */
	static std::shared_ptr<ThreadPool> Create(
		size_t core_threads = std::thread::hardware_concurrency(),
		size_t max_threads = std::thread::hardware_concurrency() * 2,
		size_t max_queue_size = DEFAULT_QUEUE_SIZE,
		int idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS)
	{
		// 辅助类，用于make_shared调用私有构造
		// (定义在 Create 内部，因为它只在这里被使用)
		struct EnableMakeShared : public ThreadPool {
			EnableMakeShared(size_t core, size_t max, 
				size_t queue, int timeout)
			: ThreadPool(core, max, queue, timeout) {}
		};
		// 创建 shared_ptr (enable_shared_from_this 的要求)
		auto pool = std::make_shared<EnableMakeShared>(
			core_threads, max_threads, max_queue_size, idle_timeout_ms);
		
		// 启动核心线程
		// 必须在 shared_ptr 创建 *之后* 调用，
		// 这样 init 内部才能调用 shared_from_this()。
		pool->init(); 
		return pool;
	}
	
	
	/**
	 * @brief 线程池析构函数
	 */
	~ThreadPool() {
		{
			// 加锁，设置线程池停止标志
			std::unique_lock<std::mutex> lock(mutex_); 
			is_running_ = false; 
		} // 锁释放
		
		// 唤醒所有等待的工作线程
		not_empty_cv_.notify_all(); 
		// 唤醒所有等待的生产者线程
		not_full_cv_.notify_all();  
		
		// 等待所有*核心*线程退出
		for (std::thread& t : core_thread_objects_) { 
			if (t.joinable()) { 
				t.join(); // 等待线程执行完毕
			}
		}
	}
	
	// 禁止拷贝和赋值
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	
	/**
	 * @brief 提交任务到线程池 (异步)
	 * (死锁安全 + 停机安全 + 生产者阻塞)
	 */
	template<typename F, typename... Args>
	auto Submit(F&& func, Args&&... args)
	-> ThreadPoolFuture<std::invoke_result_t<F, Args...>> { 
		
		// 获取函数F(Args...)的返回类型
		using ReturnType = std::invoke_result_t<F, Args...>; 
		
		// 使用 packaged_task 包装任务以获取 future
		// 包装到 shared_ptr 中以安全地传递到队列
		auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<F>(func), std::forward<Args>(args)...)
			);
		
		// 获取与 task 关联的 future
		std::future<ReturnType> internal_future = task_ptr->get_future();
		
		// 获取当前配置的异常处理器
		auto current_handler = global_exception_handler_;

		// 包装任务，确保捕获所有异常（包括处理器本身的异常）
		Task task_wrapper = [task_ptr, current_handler]() mutable {
			try {
				(*task_ptr)();
			} catch (...) {
				// 任务本身抛出异常
				try {
					// 调用用户设置的异常处理器
					current_handler(std::current_exception());
				} catch (const std::exception& e) {
					// 用户的异常处理器 *自己* 也抛出了异常
					std::cerr << "ThreadPool: Exception handler itself threw an exception: " 
							  << e.what() << std::endl;
				} catch (...) {
					std::cerr << "ThreadPool: Exception handler itself threw an unknown exception." 
							  << std::endl;
				}
			}
		};
		
		// 关键区域：使用主锁来协调所有状态
		std::unique_lock<std::mutex> lock(mutex_); 
		
		// 停机安全检查：检查是否在析构后提交
		if (!is_running_) { 
			throw std::runtime_error(
				"Submit on stopped ThreadPool");
		}
		
		// 有界队列检查：队列未满
		if (tasks_.size() < max_queue_size_) { 
			tasks_.emplace(std::move(task_wrapper)); // 任务入队
		} else {
			// 队列已满
			
			// 动态线程创建：检查是否可创建新线程
			if (total_thread_count_ < max_threads_) { 
				
				// UAF 修复：获取 `shared_ptr` 以传递给新线程
				auto self = shared_from_this(); 
				
				// 创建"非核心"线程
				// lambda 捕获 `self`，保证线程池对象存活
				std::thread dynamic_thread( 
					[self, this]() { this->WorkerThread(false); }
					); 
				
				// 动态线程 *创建后* 才增加计数和入队
				total_thread_count_++; // 增加总线程数
				
				// 记录动态线程创建
				metrics_.dynamic_threads_created++;
				
				// 分离线程，`self` 会保证其安全
				dynamic_thread.detach(); 
				
				tasks_.emplace(std::move(task_wrapper)); // 入队
			} else {
				// 生产者阻塞：队列已满且达到最大线程数，阻塞当前提交线程
				not_full_cv_.wait(lock, [this] {
					// 等待，直到 1.被唤醒 2.队列不满 3.或线程池关闭
					return !is_running_ || tasks_.size() < max_queue_size_;
				});
				
				// 阻塞后再次检查
				if (!is_running_) { // 检查是否在等待时关闭
					throw std::runtime_error(
						"Submit on stopped ThreadPool");
				}
				
				tasks_.emplace(std::move(task_wrapper)); // 入队
			}
		}
		
		// 记录任务提交 (移入锁内以保持风格一致)
		metrics_.tasks_submitted++;
		
		// *立即*解锁，然后再通知
		lock.unlock(); 
		
		// 唤醒工作线程：唤醒一个可能在睡眠的线程
		not_empty_cv_.notify_one(); 
		
		// 返回自定义Future：内部存储为 `weak_ptr`
		return ThreadPoolFuture<ReturnType>(
			std::move(internal_future), shared_from_this());
	}
	
	/**
	 * @brief 设置全局异常处理器
	 */
	void SetGlobalExceptionHandler(
		std::function<void(std::exception_ptr)> handler) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (handler) {
			global_exception_handler_ = std::move(handler);
		} else {
			// 如果设置了 nullptr，恢复为默认处理器
			global_exception_handler_ = GetDefaultExceptionHandler();
		}
	}

	/**
	 * @brief 获取当前监控指标
	 * @note 返回一个（非原子）快照
	 */
	Metrics GetMetrics() const {
		Metrics m;
		m.tasks_submitted = metrics_.tasks_submitted.load();
		m.tasks_completed = metrics_.tasks_completed.load();
		m.dynamic_threads_created = metrics_.dynamic_threads_created.load();
		return m;
	}

	/**
	 * @brief 获取当前队列中的任务数
	 */
	size_t GetQueueSize() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return tasks_.size();
	}
	
// 构造函数设为 private，强制使用 Create() 工厂
private:
	/**
	 * @brief 私有构造函数
	 */
	explicit ThreadPool(
		size_t core_threads,
		size_t max_threads,
		size_t max_queue_size,
		int idle_timeout_ms)
	: is_running_(true), 
	core_threads_(core_threads), 
	max_threads_(max_threads), 
	max_queue_size_(max_queue_size), 
	idle_timeout_(std::chrono::milliseconds(idle_timeout_ms)), 
	total_thread_count_(0), 
	idle_thread_count_(0),
	global_exception_handler_(GetDefaultExceptionHandler()) // 初始化为默认值
	{ 
		
		// 参数验证
		if (max_queue_size == 0) {
			throw std::invalid_argument("max_queue_size must be > 0");
		}
		if (idle_timeout_ms <= 0) {
			throw std::invalid_argument("idle_timeout_ms must be > 0");
		}

		// 确保参数合理性
		if (core_threads_ > max_threads_) { 
			core_threads_ = max_threads_;
		}
	}
	
	/**
	 * @brief 启动核心线程。
	 * 必须在 shared_ptr 构造完成后调用。
	 */
	void init() {
		// 获取 `self` (shared_ptr) 以传递给核心线程
		auto self = shared_from_this(); 
		std::lock_guard<std::mutex> lock(mutex_); // 保护 total_thread_count_
		
		core_thread_objects_.reserve(core_threads_); // 预分配空间
		// 启动所有核心线程
		for (size_t i = 0; i < core_threads_; ++i) { 
			
			// 核心线程也必须捕获 `self`，保持逻辑一致性
			core_thread_objects_.emplace_back( 
				[self, this]() { this->WorkerThread(true); });
			
			total_thread_count_++; // 增加总线程数
		}
	}
	
	
private:
	/**
	 * @brief 检查当前是否为工作线程
	 * (用于 ThreadPoolFuture::get())
	 */
	bool IsWorkerThread() const {
		// 检查 thread_local 标志，此操作无锁
		return g_is_worker_thread;
	}
	
	/**
	 * @brief 尝试执行一个任务 (用于内联执行)
	 * @return true 如果成功执行了一个任务, false 如果队列为空或锁冲突
	 */
	bool TryExecuteTask() {
		Task task;
		
		// 关键：使用 try_lock 避免内联执行
		// 与 WorkerThread 发生死锁
		std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock); 
		
		// 锁失败或队空
		if (!lock.owns_lock() || tasks_.empty()) { 
			return false; // 立即返回，不阻塞 get()
		}
		
		// 成功获取锁，且队列不为空
		task = std::move(tasks_.front()); 
		tasks_.pop(); // 任务出队
		
		lock.unlock(); // *立即*解锁，再通知
		
		// 唤醒一个可能阻塞的生产者
		not_full_cv_.notify_one(); 
		
		if (task) { 
			task(); // 执行任务
			// 记录任务完成
			metrics_.tasks_completed++;
		}
		
		return true; 
	}
	
	
	/**
	 * @brief 工作线程的主循环
	 * @param is_core_thread 标记是否为核心线程 (永不销毁)
	 */
	void WorkerThread(bool is_core_thread) {
		// 使用 RAII 守护类，确保 g_is_worker_thread 在任何退出路径
		// (包括异常) 都会被重置为 false。
		ThreadFlagGuard flagGuard;

		// `self` 是在线程创建时捕获的 `shared_ptr`。
		// 这个 `shared_ptr` 的存在，
		// 保证了 `this` 在整个函数执行期间都有效。
		
		// 主循环
		while (true) { 
			Task task;
			// 标记是否为空闲超时
			bool timed_out = false; 
			
			{ // 锁的保护域开始
				std::unique_lock<std::mutex> lock(mutex_); 
				
				// 队列为空，准备睡眠
				if (tasks_.empty()) { 
					
					idle_thread_count_++; // 标记为空闲
					
					// 睡眠/等待
					if (is_core_thread) { 
						// 核心线程，无限等待
						not_empty_cv_.wait(lock, [this] {
							// 唤醒条件：1.不运行了 2.队列不空
							return !is_running_ || !tasks_.empty();
						});
					} else { 
						// 动态线程，带超时的等待
						auto status = not_empty_cv_.wait_for(lock, idle_timeout_, 
							[this] {
								return !is_running_ || !tasks_.empty();
							});
						
						// 空闲回收：确认是超时
						if (!status && tasks_.empty()) { 
							timed_out = true; // 标记超时
						}
					}
					
					idle_thread_count_--; // 被唤醒或超时，不再空闲
				}
				
				// 退出条件
				if ((!is_running_ && tasks_.empty()) || timed_out) {
					// 1. 析构时，队列清空
					// 2. 动态线程空闲超时
					total_thread_count_--; // 减少总数
					
					// RAII 守护类将在此处析构，自动重置标志
					return; // 线程安全退出
				}
				
				// 被唤醒后检查队列
				if (!tasks_.empty()) { 
					task = std::move(tasks_.front()); // 获取任务
					tasks_.pop(); // 任务出队
				}
				
			} // 锁在此释放
			
			if (task) { 
				// *立即*通知生产者，我们消费了一个
				not_full_cv_.notify_one(); 
				task(); // 关键：在锁外执行任务
				// 记录任务完成
				metrics_.tasks_completed++;
			}
		}
	}

	/**
	 * @brief 获取默认的异常处理器
	 */
	static std::function<void(std::exception_ptr)> GetDefaultExceptionHandler() {
		return [](std::exception_ptr eptr) {
			try {
				if (eptr) std::rethrow_exception(eptr);
			} catch (const std::exception& e) {
				std::cerr << "Thread pool unhandled exception: " 
						  << e.what() << std::endl;
			}
		};
	}
	
private:
	// 线程池成员变量
	// ------------------------------------
	
	// 同步原语
	mutable std::mutex mutex_; // 主互斥锁，保护所有共享状态
	std::condition_variable not_empty_cv_; // 任务队列"非空"条件变量
	std::condition_variable not_full_cv_;  // 任务队列"未满"条件变量
	
	// 任务队列
	std::queue<Task> tasks_; // 存储待执行任务
	
	// 状态标志
	bool is_running_; // 线程池运行状态 (受 mutex_ 保护)
	
	// 线程计数与配置
	size_t core_threads_; // 核心线程数
	size_t max_threads_;  // 最大线程数
	size_t max_queue_size_; // 任务队列上限
	std::chrono::milliseconds idle_timeout_; // 空闲超时时间
	size_t total_thread_count_; // 当前总线程数 (受 mutex_ 保护)
	size_t idle_thread_count_;  // 当前空闲线程数 (受 mutex_ 保护)
	
	// 线程管理
	std::vector<std::thread> core_thread_objects_; // 存储核心线程对象(用于 join)
	
	// 全局异常处理器
	std::function<void(std::exception_ptr)> global_exception_handler_;

	// 监控指标
	Metrics metrics_;
};

#endif //THREADPOOL_CPP20_THREADPOOL_H
