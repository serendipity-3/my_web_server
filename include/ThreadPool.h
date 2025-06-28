//
// Created by 15565 on 2025/6/25.
//

#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

#include "MyLog.h"

extern LOG_TYPE running_log_type;

class ThreadPool {
private:
    std::vector<std::thread> threads_; // 开启的所有线程
    std::queue<std::function<void()> > tasks_; // 已有的所有任务
    std::mutex mutex_; // 让单个线程安全从任务队列中拿任务
    std::condition_variable condition_; // 任务队列空的或满的时候，线程睡觉；不为空或满的时候，叫醒线程
    bool stop_; // 线程池停止标志

public:
    explicit ThreadPool(size_t coreNum = std::thread::hardware_concurrency()); // 默认 CPU 核心数
    ~ThreadPool();

    // 主线程放个任务到任务队列里
    template<typename Func>
    void submit(Func func) { {
            // 锁上，别的线程不能来
            std::unique_lock<std::mutex> lock(this->mutex_);
            // 放任务队列里，无需再拷贝一遍，内部自动拷贝了
            this->tasks_.emplace(std::function<void()>(func));
        }
        // 叫醒一个线程起来干活
        this->condition_.notify_one();
    }

    void stop(); // 线程池停不停
};


#endif //THREADPOOL_H
