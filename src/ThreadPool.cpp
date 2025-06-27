//
// Created by 15565 on 2025/6/25.
//

#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t coreNum) {

}

ThreadPool::~ThreadPool() {

}

template<typename Func>
void ThreadPool::submit(Func func) {

}

void ThreadPool::worker_thread() {

}

void ThreadPool::stop() {
    {
        // 加个锁，当主线程改的时候，子线程等着。作用域自动解锁
        std::unique_lock<std::mutex> lock(this->mutex_);
        this->stop_ = true;
    }

    this->condition_.notify_all();
    for (std::thread t : this->threads_) {
        t.join();
    }
}


