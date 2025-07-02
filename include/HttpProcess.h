//
// Created by 15565 on 2025/6/25.
//

#ifndef HTTP_H
#define HTTP_H
#include <map>
#include <string>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "MyLog.h"
#include <unistd.h>
#include <unordered_map>
#include <cstring>
#include <sys/stat.h>

#include "HttpProcess.h"
#include "HttpProcess.h"

struct Connection;

extern LOG_TYPE running_log_type;
extern int epoll_fd;
extern std::unordered_map<int, Connection> connections;
extern std::mutex connections_mutex;
extern void defer_close_fd(int other_fd);

struct Connection {
    int client_fd; // 客户端文件描述符
    int port;
    std::string ip;
    std::string read_buffer; // 存从客户端读到的所有数据
    // std::string write_buffer; // 存准备发给客户端的所有数据
    bool request_completed; // 从客户端读的数据完整
};

enum class HttpParseResult {
    Incomplete,         // 还没读完
    Complete,           // 已完整
    HeaderTooLarge,     // 请求头超长
    BodyTooLarge,       // 请求体超长
    MalformedHeader     // Content-Length 解析失败
};


// 从客户端 fd 里接收 HTTP 请求数据，并处理数据，返回给客户端数据
void handle_client_read(int client_fd);

// 检查请求的所有数据完整不？太大不？形式对不？
HttpParseResult check_http_request_status(const std::string& request);

// 从客户端 fd 里拿到 HTTP 请求
std::map<std::string, std::string> get_http_request(Connection &connection);

// 字符串转 map
int request_str_to_map(const std::string & request_str, std::map<std::string, std::string> &request_map);

// 拿到请求后，处理 HTTP 请求
int process_http(std::map<std::string, std::string> &request, Connection &connection);

// 各种支持的方法
int process_http_get(std::map<std::string, std::string> &request, Connection &connection);
int process_http_post(std::map<std::string, std::string> &request, Connection &connection);
int process_http_put(std::map<std::string, std::string> &request, Connection &connection);
int process_http_delete(std::map<std::string, std::string> &request, Connection &connection);
int process_http_options(std::map<std::string, std::string> &request, Connection &connection);
int process_http_head(std::map<std::string, std::string> &request, Connection &connection);
int process_http_patch(std::map<std::string, std::string> &request, Connection &connection);
int process_http_other(std::map<std::string, std::string> &request_map, Connection &connection);

// 文件老哥在不
int file_exists(std::string &filename);
// 文件多大了
int64_t get_file_size(std::string &filename);
// 拿所有文件内容转字符串
std::string get_file_content(const std::string &filename);
// 发文件数据到客户端
int send_all(const std::string &response, Connection &connection);

#endif //HTTP_H