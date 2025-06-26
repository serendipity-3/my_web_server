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


class ThreadPool {
private:
    std::vector<std::thread> threads_; // 开启的所有线程
    std::queue<std::function<void()>> tasks_; // 已有的所有任务
    std::mutex mutex_; // 让单个线程安全从任务队列中拿任务
    std::condition_variable condition_; // 任务队列空的或满的时候，线程睡觉；不为空或满的时候，叫醒线程
    std::atomic<bool> stop_; // 线程池停止标志

public:
    explicit ThreadPool(size_t coreNum = std::thread::hardware_concurrency()); // 默认 CPU 核心数
    ~ThreadPool();

    void submit(std::function<void()> task); // 主线程放个任务到任务队列里
    void worker_thread(); // 每个工作线程的工作方法
    void stop(); // 线程池停不停
};



#endif //THREADPOOL_H
