//
// Created by 15565 on 2025/7/4.
//

#ifndef FILEPROCESS_H
#define FILEPROCESS_H
#include <map>
#include <string>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <unordered_map>
#include <cstring>
#include <sys/stat.h>

#include "MyLog.h"

extern LOG_TYPE running_log_type;

// 文件老哥在不
bool file_exists(std::string &filename);
// 文件多大了
int64_t get_file_size(std::string &filename);
// 根据时间生成文件名字
std::string generate_filename_by_time(const std::string &filename, const std::string &postfix);
// 拿所有文件内容转字符串
std::string get_file_content(const std::string &filename);


#endif //FILEPROCESS_H
