//
// Created by 15565 on 2025/6/25.
//

#include "HttpProcess.h"


// 线程池里的线程要干的活
void handle_client_read(int client_fd) {
    // 拿到 connections 里的对应连接
    std::unique_lock<std::mutex> lock(connections_mutex);
    const auto it = connections.find(client_fd);
    if (it == connections.end()) {
        lock.unlock();
        return;
    }
    Connection &connection = it->second;
    lock.unlock();

    // 不是完整数据，继续读数据
    // 若没有别的线程再次拿到这个 connection ，就不加锁了
    // 拿到所有内核 socket 接收缓冲区中的所有数据
    char buffer[4096];
    ssize_t bytes_received = 0;

    while (!connection.request_completed) {
        bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);

        if (bytes_received > 0) {
            connection.read_buffer.append(buffer, bytes_received);

            switch (check_http_request_status(connection.read_buffer)) {
                case HttpParseResult::Complete:
                    // 终于是完整的请求了
                    connection.request_completed = true;
                    break;
                case HttpParseResult::Incomplete:
                    continue; // 再试一次 recv
                // 垃圾请求，滚蛋
                case HttpParseResult::HeaderTooLarge:
                case HttpParseResult::BodyTooLarge:
                case HttpParseResult::MalformedHeader:
                    no("请求非法或请求太大了，关闭连接", running_log_type);
                    defer_close_fd(client_fd);
                    // 这个活不用干了
                    return;
            }
        } else if (bytes_received == 0) {
            no("客户端连接关闭了", running_log_type);
            defer_close_fd(client_fd);
            // 甲方跑路了，不用干这个活了
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有数据可读，等下一次 epoll 通知
                break;
            } else {
                no("recv 客户端数据出错", running_log_type);
                defer_close_fd(client_fd);
                // 这个活不用干了
                return;
            }
        }
    }

    // 完整的请求，开始干活
    if (connection.request_completed) {
        //  TODO: 这个 map 以后能放到全局变量 connections 里，配合 epoll 放到线程池里处理写事件
        // 用了个局部变量，默认后续的 process_http() 能处理完这一次请求，响应数据完整发送，交给主线程关闭客户端 fd
        std::map<std::string, std::string> request_map;
        // 解析成 map
        request_str_to_map(connection.read_buffer, request_map);

        process_http(request_map, connection);

        // 放到全局变量 close_queue 里，让主线程在处理完所有事件后，关闭客户端 fd
        defer_close_fd(client_fd);
    }
    // 不完整的请求而且内核没数据了，所以等下一次 epoll 通知吧
}


HttpParseResult check_http_request_status(const std::string &request) {
    // 1. 查找 \r\n\r\n，判断 header 是否完整
    size_t header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (request.size() > MAX_HEADER_SIZE) {
            return HttpParseResult::HeaderTooLarge;
        }
        return HttpParseResult::Incomplete;
    }

    // 包含 \r\n\r\n 的完整头部长度
    size_t header_size = header_end + 4;
    if (header_size > MAX_HEADER_SIZE) {
        return HttpParseResult::HeaderTooLarge;
    }

    // 2. 解析 Content-Length
    size_t pos = request.find("Content-Length:");
    if (pos != std::string::npos) {
        size_t len_start = pos + strlen("Content-Length:");
        size_t len_end = request.find("\r\n", len_start);
        if (len_end == std::string::npos) return HttpParseResult::MalformedHeader;

        std::string len_str = request.substr(len_start, len_end - len_start);
        int content_length = 0;

        try {
            content_length = std::stoi(len_str);
        } catch (...) {
            return HttpParseResult::MalformedHeader;
        }

        // 检查请求体是否太大
        if (content_length > MAX_BODY_SIZE) {
            return HttpParseResult::BodyTooLarge;
        }

        // 请求还没读完
        size_t expected_total = header_size + content_length;
        if (request.size() < expected_total) {
            return HttpParseResult::Incomplete;
        }

        return HttpParseResult::Complete;
    }

    // GET 等无 Content-Length 的情况，header 即为完整请求
    return HttpParseResult::Complete;
}


int request_str_to_map(const std::string &request_str, std::map<std::string, std::string> &request_map) {
    // 对不对
    int main_part_end = request_str.find("\r\n\r\n");
    if (main_part_end == std::string::npos) {
        no("没找到 \r\n\r\n HTTP 字符串转 Map 失败", running_log_type);
        return -1;
    }

    // 分开(请求行+请求头)和(请求体)
    std::string main_part = request_str.substr(0, main_part_end);
    // 跳过 \r\n\r\n
    std::string body_part = request_str.substr(main_part_end + 4);

    // 读请求行，根据 \r\n 和空格分割成[方法,路径,版本]分别放到 map 里
    std::string line;
    std::istringstream main_part_stream(main_part);
    std::istringstream line_stream(line);

    std::getline(main_part_stream, line);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    // 读请求头，根据 \r\n 和 : 分割成[请求头名, 请求头值]当 key-val 放到 map 里
    std::string method, path, version;
    line_stream >> method >> path >> version;

    request_map["Method"] = std::move(method);
    request_map["Path"] = std::move(path);
    request_map["Version"] = std::move(version);

    // 拿到请求头的每一行
    while (std::getline(main_part_stream, line)) {
        // 干掉末尾的 '\r'
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // 拿到一行中的 key val
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            // 去掉可能开头的空格
            while (!value.empty() && value[0] == ' ') {
                value.erase(value.begin());
            }
            request_map[key] = std::move(value);
        }
    }

    // 假如有请求体放到 map 里
    if (!body_part.empty()) {
        request_map["Body"] = std::move(body_part);
    }
    return 0;
}


int process_http(std::map<std::string, std::string> &request_map, Connection &connection) {
    auto it = request_map.find("Method");
    if (it == request_map.end()) {
        no("没有 HTTP 请求方法", running_log_type);
        return -1;
    }
    std::string method = it->second;
    int success = 0;

    if (method == "GET") {
        success = process_http_get(request_map, connection);
    } else if (method == "POST") {
        success = process_http_post(request_map, connection);
    } else if (method == "HEAD") {
        // 处理 HEAD 请求
        success = process_http_head(request_map, connection);
    } else if (method == "PUT") {
        // 处理 PUT 请求
        success = process_http_put(request_map, connection);
    } else if (method == "DELETE") {
=
        success = process_http_delete(request_map, connection);
    } else if (method == "OPTIONS") {
=
        success = process_http_options(request_map, connection);
    } else if (method == "PATCH") {
=
        success = process_http_patch(request_map, connection);
    } else {
        success = process_http_other(request_map, connection);
    }


}

int process_http_get(std::map<std::string, std::string> &request_map, Connection &connection) {

}

int process_http_post(std::map<std::string, std::string> &request_map, Connection &connection) {
    // TODO
}

int process_http_delete(std::map<std::string, std::string> &request_map, Connection &connection) {
    // TODO
}

int process_http_put(std::map<std::string, std::string> &request_map, Connection &connection) {
    // TODO
}

int process_http_options(std::map<std::string, std::string> &request_map, Connection &connection) {
    // TODO
}

int process_http_head(std::map<std::string, std::string> &request_map, Connection &connection) {
    // TODO
}


int process_http_patch(std::map<std::string, std::string> &request_map, Connection &connection) {
    // TODO
}

int process_http_other(std::map<std::string, std::string> &request_map, Connection &connection) {

}
