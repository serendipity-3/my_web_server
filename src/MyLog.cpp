//
// Created by 15565 on 2025/6/25.
//

#include "MyLog.h"

#include <thread>

// 全局使用的日志打印类型
LOG_TYPE running_log_type = LOG_TYPE::ALL;

LOG_TYPE start_log_type = LOG_TYPE::CONSOLE;

// 开不开日志
bool log_enabled = true;
// 保证打印时线程安全
std::mutex file_mutex;
std::mutex console_mutex;

// 全局日志文件对象
std::ofstream ok_log_file;
std::ofstream no_log_file;

void init_log() {
    ok_log_file.open("./ok.log", std::ios::app);
    if (!ok_log_file) {
        std::cerr << "❌[" << curr_time() << "]: " << "Cannot open ok.log" << std::endl;
    }

    no_log_file.open("./no.log", std::ios::app);
    if (!no_log_file) {
        std::cerr << "❌[" << curr_time() << "]: " << "Cannot open no.log" << std::endl;
    }
}

void close_log() {
    std::unique_lock<std::mutex> lock(file_mutex);
    if (ok_log_file.is_open()) {
        ok_log_file.close();
    }
    if (no_log_file.is_open()) {
        no_log_file.close();
    }
}

std::string curr_time() {
    std::time_t t = std::time(nullptr);
    const std::tm *tm = std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void ok(LOG_TYPE type, const std::string &info, const std::string &filename, int line_num) {
    if (log_enabled) {
        std::ostringstream oss;
        oss << "🎯 Yes,ok[" << curr_time() << "]"
            << "[thread:" << std::this_thread::get_id() << "]"
            << "[" << filename << ":" << line_num << "]: "
            << info;
        std::string msg = oss.str();

        if (type == LOG_TYPE::CONSOLE) {
            std::unique_lock<std::mutex> lock(console_mutex);
            std::cout << msg << std::endl;

        } else if (type == LOG_TYPE::FILE) {
            to_file(msg, true);

        } else if (type == LOG_TYPE::ALL) {
            {
                std::unique_lock<std::mutex> lock(console_mutex);
                std::cout << msg << std::endl;
            }

            to_file(msg, true);
        }
    }
}

void no(LOG_TYPE type, const std::string &info, const std::string &filename, int line_num) {
    if (log_enabled) {
        std::ostringstream oss;
        oss << "❌ Oh,no[" << curr_time() << "]"
            << "[thread:" << std::this_thread::get_id() << "]"
            << "[" << filename << ":" << line_num << "]: "
            << info;

        std::string msg = oss.str();

        if (type == LOG_TYPE::CONSOLE) {
            std::cout << msg << std::endl;
        } else if (type == LOG_TYPE::FILE) {
            to_file(msg, false);
        } else if (type == LOG_TYPE::ALL) {
            std::cout << msg << std::endl;
            to_file(msg, false);
        }
    }
}

void to_file(const std::string &info, bool is_ok) {
    std::unique_lock<std::mutex> lock(file_mutex);

    std::ofstream& log_file = is_ok ? ok_log_file : no_log_file;
    if (!log_file.is_open()) {
        std::cerr << "❌[" << curr_time() << "]: " << "Log file not open" << std::endl;
        return;
    }

    log_file << info << std::endl;
    log_file.flush();
}
