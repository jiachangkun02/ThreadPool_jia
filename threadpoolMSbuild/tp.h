#pragma once

#include <condition_variable>
#include <forward_list>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>
#include "tq.h"

using Task=std::function<void()>;

//普通版线程池主要部分实现

class ThreadPool
{
public:
    ThreadPool(size_t thread_nums)
        :_thread_counts(thread_nums)
        ,_is_running(true)
    {
        _threads.reserve(thread_nums);
        for (int i = 0; i < thread_nums; ++i)
        {
            _threads.emplace_back(&ThreadPool::work_thread,this);
        }
    }

    template<class F,class ...args>
    auto Enqueue(F&& f,args&& ...arg)
    ->std::future<std::invoke_result_t<F,args...>>
    {
        using returnType=typename std::invoke_result_t<F,args...>;
        //将函数打包
        auto bound_task=std::bind(std::forward<F>(f),std::forward<args>(arg)...);
        //将不同的函数返回值统一  使用智能指针来管理
        auto task_ptr=std::make_shared<std::packaged_task<returnType()>>(std::move(bound_task));

        auto result=task_ptr->get_future();

        {
            std::lock_guard<std::mutex> lg(_queue_mutex);

            _task.push(std::move([task_ptr](){(*task_ptr)();}));
        }

        _cond.notify_one();
        return result;

    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> ul(_queue_mutex);
            _is_running=false;
        }
        
        _cond.notify_all();
        for (auto& thd : _threads)
        {
            if (thd.joinable())
            {
                thd.join();
            }
        }
        std::cout<<"线程池析构函数执行完毕 线程池退出"<<std::endl;
    }

private:
    const size_t& _thread_counts;//线程数量统计
    std::vector<std::thread> _threads;//线程的集合
    //std::queue<Task> _task;//任务队列
    std::mutex _queue_mutex;
    std::atomic_bool _is_running;
    std::condition_variable _cond;
    TaskQueue _task;

    void work_thread()
    {
        while (true)
        {
            Task task;
            // {
            //     std::unique_lock<std::mutex> ul(_queue_mutex);
            //     _cond.wait(ul,[this]()
            //     {
            //         return _is_running.load() || !_task.empty();
            //     });
            //
            //     if (_is_running.load() && _task.empty())//线程池不在跑了  且任务为空   工作线程就可以安全退出了
            //     {
            //         return;
            //     }
            //
            //     task=std::move(_task.front());
            //     _task.pop();
            // }


            _task.wait_and_pop(task);
            // if (task)  //  避免空任务导致 bad_function_call
            //     task();
            task();
        }

    }
};