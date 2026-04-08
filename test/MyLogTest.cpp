//
// Created by 15565 on 2025/6/26.
//

#include <thread>
#include <vector>

#include "MyLog.h"

auto logType = LOG_TYPE::ALL;

void to_file_test(int id) {
    to_file("线程日志测试 - ID: " + std::to_string(id), "./log_test.txt");
}

void ok_test(int id) {
    ok(logType, "测试 ok thread:" + std::to_string(id), __FILE__, __LINE__);
}

void no_test(int id) {
    no(logType, "测试 no thread:" + std::to_string(id), __FILE__, __LINE__);
}

void to_file_test_fun() {
    std::vector<std::thread> arr;
    for (int i = 0; i < 10; i++) {
        arr.push_back(std::thread(to_file_test, i));
    }

    for (int i = 0; i < arr.size(); i++) {
        arr[i].join();
    }
}

void ok_test_fun() {
    std::vector<std::thread> arr;
    for (int i = 0; i < 10; i++) {
        arr.push_back(std::thread(ok_test, i));
    }

    for (int i = 0; i < arr.size(); i++) {
        arr[i].join();
    }
}

void no_test_fun() {
    std::vector<std::thread> arr;
    for (int i = 0; i < 10; i++) {
        arr.push_back(std::thread(no_test, i));
    }

    for (int i = 0; i < arr.size(); i++) {
        arr[i].join();
    }
}

int main() {
    ok_test_fun();
    no_test_fun();
    to_file_test_fun();
    // cout << "cout test" << endl;
    // cerr << "cerr test" << endl;

    return 0;
}