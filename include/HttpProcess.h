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
#include <unistd.h>
#include <unordered_map>
#include <cstring>
#include <sys/stat.h>
#include <nlohmann/json.hpp>

#include "MyLog.h"
#include "FileProcess.h"

using json = nlohmann::json;


struct Connection;

extern LOG_TYPE running_log_type;
extern int epoll_fd;
extern std::unordered_map<int, Connection> connections;
extern std::mutex connections_mutex;
extern void defer_close_fd(int other_fd);

struct Connection {
    int client_fd;
    int port;
    std::string ip;
    std::string read_buffer;
    bool request_completed;
    bool processing;
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


// 发文件数据到客户端
int send_all(const std::string &response, Connection &connection);

#endif //HTTP_H