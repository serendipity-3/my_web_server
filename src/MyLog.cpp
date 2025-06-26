//
// Created by 15565 on 2025/6/25.
//

#include "MyLog.h"

std::mutex file_mutex;

std::string curr_time() {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void ok(const std::string &info, LOG_TYPE type) {
    std::string msg =  "🎯 ok[" + curr_time() + "]: " + info;

    if (type == LOG_TYPE::CONSOLE) {
        std::cout << msg << std::endl;
    } else if (type == LOG_TYPE::FILE) {
        to_file(msg, "./ok.log");
    }
}

void no(const std::string &info, LOG_TYPE type) {
    std::string msg = "❌ no[" + curr_time() + "]: " + info;
    if (type == LOG_TYPE::CONSOLE) {
        std::cout << msg << std::endl;
    } else if (type == LOG_TYPE::FILE) {
        to_file(msg, "./no.log");
    }
}

void to_file(const std::string &info, std::string path) {

    // 只有一个线程能写，从一开始就加锁这样每个打开的 File 结构体的索引都是最新的 = 最后一行。
    std::unique_lock<std::mutex> lock(file_mutex);

    // 文件能打开吗
    std::ofstream log_file(path, std::ios::app);
    if (!log_file) {
        std::cerr << "❌[" << curr_time() << "]: " << "Cannot open log file " << path << std::endl;
        return;
    }

    log_file << info << std::endl;

    log_file.close();
    lock.unlock();
}



