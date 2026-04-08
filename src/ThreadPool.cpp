//
// Created by 15565 on 2025/6/25.
//

#include "ThreadPool.h"

#include "MyLog.h"



ThreadPool::ThreadPool(size_t coreNum): stop_(false) {
    for (size_t i = 0; i < coreNum; i++) {
        // 启动一个线程干活
        this->threads_.emplace_back([this, i]() {
            std::ostringstream oss;
            oss << "线程 " << i << " 启动了";
            ok(running_log_type, oss.str(), __FILE__, __LINE__);
            while (true) {
                std::function<void()> task;
                {
                    // 子线程先挡住别的线程，让自己拿完一个任务。这时子线程不知道队列里有没有任务。
                    std::unique_lock<std::mutex> lock(this->mutex_);

                    // 没任务且线程池没关闭，睡一会吧，等会干活
                    while (this->tasks_.empty() && !this->stop_) {
                        this->condition_.wait(lock);
                    }

                    // 线程池停了继续干完剩下的活。任务队列还有继续干活。
                    // this->condition_.wait(lock, [this]() {
                    //     return this->stop_ || !this->tasks_.empty();
                    // });

                    // 线程池关了还没任务，干完活了
                    if (this->stop_ && this->tasks_.empty()) {
                        std::ostringstream oss3;
                        oss3 << "线程 " << i << " 退休了";
                        ok(running_log_type, oss3.str(),  __FILE__, __LINE__);
                        return;
                    }
                    // 拿到一个任务
                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                }
                // 干活中
                task();

                std::ostringstream oss2;
                oss2 << "线程 " << i << " 干了一个活";
                ok(running_log_type, oss2.str(), __FILE__, __LINE__);
            }
        });
    }
}


void ThreadPool::stop() {

    this->stop_ = true;

    // 都醒醒，要完事了，要确定所有线程不会再睡眠
    // 因为主线程不会再叫醒线程
    this->condition_.notify_all();
    for (std::thread &t : this->threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

ThreadPool::~ThreadPool() {
    stop();
}
