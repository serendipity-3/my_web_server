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
                    connection.request_completed = true;
                    break;
                case HttpParseResult::Incomplete:
                    continue;  // 再试一次 recv
                    // 垃圾请求，滚蛋了
                case HttpParseResult::HeaderTooLarge:
                case HttpParseResult::BodyTooLarge:
                case HttpParseResult::MalformedHeader:
                    no("请求非法或请求太大了，关闭连接", running_log_type);
                    defer_close_fd( client_fd);
                    // 这个活不用干了
                    return;
            }
        } else if (bytes_received == 0) {
            no("客户端连接关闭了", running_log_type);
            defer_close_fd(client_fd);
            // 这个活不用干了
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
        std::map<std::string, std::string> request_map;
        // 解析成 map
        request_str_to_map(connection.read_buffer, request_map);

        process_http(request_map, connection);

        // 连接处理完毕，epoll 中拿掉，关掉文件描述符，
        defer_close_fd(client_fd);
    }
    // 不完整的请求而且内核没数据了，所以等下一次 epoll 通知吧
}


HttpParseResult check_http_request_status(const std::string& request) {
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


int request_str_to_map(const std::string & request_str,std::map<std::string, std::string> &request_map) {
    // 分开(请求行+请求头)和(请求体)


    // 读请求行，根据 \r\n 和空格分割成[方法,路径,版本]分别放到 map 里


    // 读请求头，根据 \r\n 和 : 分割成[请求头名, 请求头值]当 key-val 放到 map 里


    // [假如有请求体] 根据 \r\n\r\n，把之后的所有字符放到 map 里


}


int process_http(std::map<std::string, std::string> &request, Connection &connection) {


}

std::string process_http_get(std::map<std::string, std::string> &request, Connection &connection) {

}

std::string process_http_post(std::map<std::string, std::string> &request, Connection &connection) {
    // TODO
}

std::string process_http_delete(std::map<std::string, std::string> &request, Connection &connection) {
    // TODO
}

std::string process_http_put(std::map<std::string, std::string> &request, Connection &connection) {
    // TODO
}

std::string process_http_options(std::map<std::string, std::string> &request, Connection &connection) {
    // TODO
}

std::string process_http_head(std::map<std::string, std::string> &request,Connection &connection) {
    // TODO
}


std::string process_http_patch(std::map<std::string, std::string> &request, Connection &connection) {
    // TODO
}






