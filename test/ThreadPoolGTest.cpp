//
// ThreadPoolGTest.cpp
// 线程池单元测试
//

#include <gtest/gtest.h>
#include <atomic>
#include <vector>
#include "ThreadPool.h"

// ========== 基本功能测试 ==========

class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F(ThreadPoolTest, ConstructorDefault) {
    ThreadPool pool;
    EXPECT_TRUE(true);
}

TEST_F(ThreadPoolTest, ConstructorWithSize) {
    ThreadPool pool(4);
    EXPECT_TRUE(true);
}

TEST_F(ThreadPoolTest, SubmitSingleTask) {
    ThreadPool pool(2);
    std::atomic<bool> executed{false};

    pool.submit([&executed]() {
        executed = true;
    });

    pool.stop();
    EXPECT_TRUE(executed);
}

TEST_F(ThreadPoolTest, SubmitMultipleTasks) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    const int num_tasks = 100;

    for (int i = 0; i < num_tasks; i++) {
        pool.submit([&counter]() {
            counter++;
        });
    }

    pool.stop();
    EXPECT_EQ(counter.load(), num_tasks);
}




// ========== 停止测试 ==========

TEST_F(ThreadPoolTest, StopWaitsForTasks) {
    ThreadPool pool(2);
    std::atomic<int> completed{0};
    const int num_tasks = 10;

    for (int i = 0; i < num_tasks; i++) {
        pool.submit([&completed]() {
            usleep(10000);
            completed++;
        });
    }

    pool.stop();
    EXPECT_EQ(completed.load(), num_tasks);
}

TEST_F(ThreadPoolTest, DestructorStopsPool) {
    std::atomic<int> completed{0};

    {
        ThreadPool pool(2);
        for (int i = 0; i < 5; i++) {
            pool.submit([&completed]() {
                usleep(10000);
                completed++;
            });
        }
    }

    EXPECT_EQ(completed.load(), 5);
}


// ========== 边界测试 ==========

TEST_F(ThreadPoolTest, SingleThread) {
    ThreadPool pool(1);
    std::atomic<int> counter{0};

    for (int i = 0; i < 100; i++) {
        pool.submit([&counter]() {
            counter++;
        });
    }

    pool.stop();
    EXPECT_EQ(counter.load(), 100);
}

TEST_F(ThreadPoolTest, ManyThreads) {
    ThreadPool pool(16);
    std::atomic<int> counter{0};

    for (int i = 0; i < 1000; i++) {
        pool.submit([&counter]() {
            counter++;
        });
    }

    pool.stop();
    EXPECT_EQ(counter.load(), 1000);
}


// ========== 主函数 ==========

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
