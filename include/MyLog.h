//
// Created by 15565 on 2025/6/25.
//

#ifndef MYLOG_H
#define MYLOG_H
#include <string>
#include <iostream>
#include <sstream> // ostringstream
#include <chrono>
#include <ctime>
#include <iomanip>   // std::put_time
#include <mutex> // 互斥锁
#include <fstream> // 写文件

enum class LOG_TYPE {
    CONSOLE,
    FILE,
    ALL
};

// 获取当前时间
std::string curr_time();

// 打印成功信息到文件或者终端
void ok(const std::string &info, LOG_TYPE type);

// 打印错误信息
void no(const std::string &info,LOG_TYPE type);

// 信息写到文件里
void to_file(const std::string &info, const std::string &path);



#endif //MYLOG_H
