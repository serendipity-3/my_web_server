//
// Created by 15565 on 2025/6/26.
//

#include <thread>

#include "MyLog.h"

using namespace std;

void write_log_test(int id) {
    to_file("线程日志测试 - ID: " + std::to_string(id), "log_test.txt");
}

int main() {
    std::thread t1(write_log_test, 1);
    std::thread t2(write_log_test, 2);
    std::thread t3(write_log_test, 3);

    t1.join();
    t2.join();
    t3.join();
    return 0;
}