//
// Created by 15565 on 2025/6/25.
//

#include "MyLog.h"

// 全局使用的日志打印类型
auto running_log_type = LOG_TYPE::ALL;
auto start_log_type = LOG_TYPE::CONSOLE;

// 开不开日志
bool log_enabled = true;
// 保证打印时线程安全
std::mutex file_mutex;
std::mutex console_mutex;

std::string curr_time() {
    std::time_t t = std::time(nullptr);
    const std::tm *tm = std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void ok(const std::string &info, LOG_TYPE type) {
    if (log_enabled) {
        std::ostringstream oss;
        oss << "🎯 Yes,ok[" << curr_time() << "]"
            << "[" << __FILE__ << ":" << __LINE__ << "]: "
            << info;
        std::string msg = oss.str();

        if (type == LOG_TYPE::CONSOLE) {
            std::unique_lock<std::mutex> lock(console_mutex);
            std::cout << msg << std::endl;

        } else if (type == LOG_TYPE::FILE) {
            to_file(msg, "./ok.log");

        } else if (type == LOG_TYPE::ALL) {
            {
                std::unique_lock<std::mutex> lock(console_mutex);
                std::cout << msg << std::endl;
            }

            to_file(msg, "./ok.log");
        }
    }
}

void no(const std::string &info, LOG_TYPE type) {
    if (log_enabled) {
        std::ostringstream oss;
        oss << "❌ Oh,no[" << curr_time() << "]"
            << "[" << __FILE__ << ":" << __LINE__ << "]: "
            << info;

        std::string msg = oss.str();

        if (type == LOG_TYPE::CONSOLE) {
            std::cout << msg << std::endl;
        } else if (type == LOG_TYPE::FILE) {
            to_file(msg, "./no.log");
        } else if (type == LOG_TYPE::ALL) {
            std::cout << msg << std::endl;
            to_file(msg, "./no.log");
        }
    }
}

void to_file(const std::string &info, const std::string &path) {
    // 只有一个线程能写，从一开始就加锁这样每个打开的 File 结构体的索引都是最新的 = 最后一行。
    std::unique_lock<std::mutex> lock(file_mutex);

    // 文件能打开吗
    // TODO: The code of opening and closing file-operation can move to main function globally.
    // Because opening and closing file frequently must cause the server running slowly.
    std::ofstream log_file(path, std::ios::app);
    if (!log_file) {
        std::cerr << "❌[" << curr_time() << "]: " << "Cannot open log file " << path << std::endl;
        return;
    }

    log_file << info << std::endl;
    log_file.close();
    lock.unlock();
}
