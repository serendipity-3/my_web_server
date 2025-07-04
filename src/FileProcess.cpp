//
// Created by 15565 on 2025/7/4.
//
#include "FileProcess.h"


bool file_exists(std::string &filename) {
    struct stat file_stat{};
    if (stat(filename.c_str(), &file_stat) == 0) {
        return true;
    } else {
        return false;
    }
}

std::string generate_filename_by_time(const std::string &filename, const std::string &postfix) {
    // 拿到当前时间
    time_t t = time(nullptr);
    struct tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream oss;
    oss << filename << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << "." << postfix;
    return oss.str();
}

std::string get_file_content(const std::string &filename) {
    std::ifstream ifs(filename, std::ifstream::binary | std::ifstream::in);
    if (!ifs.is_open()) {
        no("文件没打开", running_log_type);
        return "";
    }

    std::ostringstream temp_content;
    temp_content << ifs.rdbuf();

    return temp_content.str();
}