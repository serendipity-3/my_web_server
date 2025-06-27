//
// Created by 15565 on 2025/6/25.
//

#ifndef HTTP_H
#define HTTP_H
#include <map>
#include <string>
#include <netinet/in.h>

// 从客户端 fd 里接收 HTTP 请求数据，并处理数据，返回给客户端数据
void handle_client(int client_fd);

// 从客户端 fd 里拿到 HTTP 请求
std::map<std::string, std::string> get_http_request(int client_fd, std::string ip);

// 拿到请求后，处理 HTTP 请求
std::string process_http(std::map<std::string, std::string> request, std::string ip);

// 各种支持的方法
std::string process_http_get(std::map<std::string, std::string> request, std::string ip);
std::string process_http_post(std::map<std::string, std::string> request, std::string ip);
std::string process_http_put(std::map<std::string, std::string> request, std::string ip);
std::string process_http_delete(std::map<std::string, std::string> request, std::string ip);
std::string process_http_options(std::map<std::string, std::string> request, std::string ip);
std::string process_http_head(std::map<std::string, std::string> request, std::string ip);
std::string process_http_patch(std::map<std::string, std::string> request, std::string ip);



#endif //HTTP_H