//
// Created by 15565 on 2025/6/25.
//

#include "HttpProcess.h"

constexpr size_t MAX_HEADER_SIZE = 8 * 1024; // 8KB 请求头限制
constexpr size_t MAX_BODY_SIZE = 4 * 1024 * 1024; // 4MB 请求体限制

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
            connection.recv_request_buffer.append(buffer, bytes_received);

            switch (check_http_request_status(connection.recv_request_buffer)) {
                case HttpParseResult::Complete:
                    // 终于是完整的请求了
                    connection.request_completed = true;
                    break;
                case HttpParseResult::Incomplete:
                    continue; // 再试一次 recv
                // 垃圾请求，一边去
                case HttpParseResult::HeaderTooLarge:
                case HttpParseResult::BodyTooLarge:
                case HttpParseResult::MalformedHeader:
                    no(running_log_type, "请求非法或请求太大了，关闭连接",  __FILE__, __LINE__);
                    defer_close_fd(client_fd);
                    // 这个活不用干了
                    return;
            }
        } else if (bytes_received == 0) {
            no(running_log_type, "客户端连接关闭了", __FILE__, __LINE__);
            defer_close_fd(client_fd);
            // 甲方跑路了，不用干这个活了
            return;

        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有数据可读，等下一次 epoll 通知
                // 当前 client_fd 放到 rearm_queue 里，让主线程重新把它加入 epoll 里，等下一次 epoll 通知它读数据
                // 剩下的活交给队友了
                defer_rearm_fd(client_fd);
                return;
            }
            no(running_log_type, "recv 客户端数据出错", __FILE__, __LINE__);
            defer_close_fd(client_fd);
            // 不用干这个活了
            return;
        }
    }

    // 完整的请求，开始干活
    if (connection.request_completed) {
        //  TODO: 这个 map 以后能放到全局变量 connections 里，配合 epoll 放到线程池里处理写事件
        // 用了个局部变量，默认后续的 process_http() 能处理完这一次请求，响应数据完整发送，交给主线程关闭客户端 fd
        std::map<std::string, std::string> request_map;
        // 解析成 map
        request_str_to_map(connection.recv_request_buffer, request_map);

        ok(running_log_type, connection.recv_request_buffer, __FILE__, __LINE__);

        // 打印日志
        std::stringstream oss1;
        oss1 << "IP=" << connection.ip << " PORT=" << connection.port << " 的用户执行了 "
                << request_map["Method"] << " 方法;" << "请求路径: " << request_map["Path"];


        for (const auto &pair : request_map) {
            std::cout << pair.first << ": " << pair.second << std::endl;
        }

        ok(running_log_type, oss1.str(),  __FILE__, __LINE__);

        process_http(request_map, connection);

        ok(running_log_type, "执行完毕", __FILE__, __LINE__);

        defer_close_fd(client_fd);
    }
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
        no(running_log_type, "没找到 \r\n\r\n HTTP 字符串转 Map 失败",  __FILE__, __LINE__);
        return -1;
    }

    // 分开(请求行+请求头)和(请求体)
    std::string main_part = request_str.substr(0, main_part_end);
    // 跳过 \r\n\r\n
    std::string body_part = request_str.substr(main_part_end + 4);

    // 读请求行，根据 \r\n 和空格分割成[方法,路径,版本]分别放到 map 里
    std::string line;
    std::istringstream main_part_stream(main_part);
    std::getline(main_part_stream, line);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    // 读请求头，根据 \r\n 和 : 分割成[请求头名, 请求头值]当 key-val 放到 map 里
    std::istringstream line_stream(line);
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
    // 拿到方法
    auto it = request_map.find("Method");
    if (it == request_map.end()) {
        no(running_log_type, "没有 HTTP 请求方法", __FILE__, __LINE__);
        return -1;
    }
    std::string method = it->second;
    int success = 0;

    // 是哪个方法，处理
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
        success = process_http_delete(request_map, connection);
    } else if (method == "OPTIONS") {
        success = process_http_options(request_map, connection);
    } else if (method == "PATCH") {
        success = process_http_patch(request_map, connection);
    } else {
        success = process_http_other(request_map, connection);
    }

    return success;
}

// 只支持 HTML 文本文件
int process_http_get(std::map<std::string, std::string> &request_map, Connection &connection) {
    std::string req_path = request_map["Path"];
    if (req_path == "/") {
        req_path = "/index.html";
    }
    std::string path = "." + req_path;

    std::string response;
    std::string file_content;
    ok(running_log_type, path,  __FILE__, __LINE__);
    if (file_exists(path)) {
        // 成了
        file_content = get_file_content(path);

        response.append("HTTP/1.1 200 OK\r\n")
                .append("Content-Type: text/html\r\n")
                .append("Content-Length: " + std::to_string(file_content.size()) + "\r\n")
                .append("Connection: close\r\n")
                .append("\r\n")
                .append(file_content);
    } else {
        // 没有
        response.append("HTTP/1.1 404 Not Found\r\n")
                .append("Content-Type: text/html\r\n")
                .append("Content-Length: 49\r\n")
                .append("Connection: close\r\n")
                .append("\r\n")
                .append("<html><body><h1>404 Not Found</h1></body></html>");
    }
    ok(running_log_type, response,  __FILE__, __LINE__);
    return send_all(response, connection);
}

int process_http_post(std::map<std::string, std::string> &request_map, Connection &connection) {
    // 数据提交到哪里？默认 path 是一个目录，提交的是 JSON 格式数据
    std::string path = "." + request_map["Path"];
    json str_to_json = json::parse(request_map["Body"]);
    std::string response;
    std::string json_to_str;

    // TODO: 用全局 hashmap 存所有支持的路径 -> 处理的方法
    // 目前就这一个支持的路径吧
    if (path == "./file") {
        // 向 path 里放数据
        bool success = true; // 数据存到文件里了
        std::string file_name = generate_filename_by_time("file", "html");
        std::ofstream output_file(path + file_name, std::ios::out | std::ios::binary | std::ios::trunc);


        if (output_file.is_open()) {
            // 拿到 JSON 里的 file 的内容，file 里只应该有文件的原始数据，不要文件元信息
            std::string file_content = str_to_json["file"];

            // TODO: 这里若是两个客户端写到同一个地方，多线程会错乱的，所以要加个确定用户身份
            output_file.write(file_content.c_str(), file_content.size());
            if (output_file.fail()) {
                no(running_log_type, "文件打开了, 却存不了数据",  __FILE__, __LINE__);
                success = false;
            }

            output_file.close();
        } else {
            no( running_log_type, "文件打不开", __FILE__, __LINE__);
            success = false;
        }

        // 告诉客户端完事了

        if (success) {
            json content;
            content["msg"] = "提交成功了";
            content["code"] = 200;

            json data;
            data["filename"] = file_name;
            data["url"] = path + "/" + file_name;
            content["data"] = data;

            std::string str_content = content.dump();
            // 成了
            response.append("HTTP/1.1 200 OK\r\n")
                    .append("Content-Type: text/plain\r\n")
                    .append("Content-Length: 0\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        } else {
            // 没成
            response.append("HTTP/1.1 500 Internal Server Error\r\n")
                    .append("Content-Type: text/plain\r\n")
                    .append("Content-Length: 0\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        }
    } else {
        json content;
        content["msg"] = "没有这个路径，这条路径以后再来探索吧";
        content["code"] = 404;
        std::string str_content = content.dump();

        response.append("HTTP/1.1 404 Not Found\r\n")
                .append("Content-Type: application/json\r\n")
                .append("Content-Length: " + std::to_string(str_content.size()) + "\r\n")
                .append("Connection: close\r\n")
                .append("\r\n")
                .append(str_content);
    }

    return send_all(response, connection);
}

// 删掉某个资源
int process_http_delete(std::map<std::string, std::string> &request_map, Connection &connection) {
    // 删哪里的文件？默认 path 是一个文件，不是一个目录
    std::string path = "." + request_map["Path"];

    // 准备删除
    bool success = true; // 数据删掉了
    std::string response;

    // TODO: 应该不会有 ../ 这种路径吧，有的话就完蛋了
    if (file_exists(path)) {
        // 删除文件
        if (std::remove(path.c_str()) != 0) {
            success = false;
        }

        // 返回请求
        if (success) {
            // 成了
            response.append("HTTP/1.1 200 OK\r\n")
                    .append("Content-Type: text/plain\r\n")
                    .append("Content-Length: 0\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        } else {
            // 没成
            response.append("HTTP/1.1 500 Internal Server Error\r\n")
                    .append("Content-Type: text/plain\r\n")
                    .append("Content-Length: 0\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        }
    } else {
        // 没有
        response.append("HTTP/1.1 404 Not Found\r\n")
                .append("Content-Type: text/plain\r\n")
                .append("Content-Length: 0\r\n")
                .append("Connection: close\r\n")
                .append("\r\n");
    }

    return send_all(response, connection);
}

// 更新整个资源
int process_http_put(std::map<std::string, std::string> &request_map, Connection &connection) {
    std::string path = "." + request_map["Path"];
    //
    std::string body = request_map["Body"];
    std::string response;

    if (file_exists(path)) {
        // 更新文件
        int updated = 0;

        if (updated == 0) {
            // 成了
            response.append("HTTP/1.1 200 OK\r\n")
                    .append("Content-Type: text/html\r\n")
                    .append("Content-Length: 0\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        } else {
            // 未知错误
            response.append("HTTP/1.1 500 Internal Server Error\r\n")
                    .append("Content-Type: text/plain\r\n")
                    .append("Content-Length: 0\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        }
    } else {
        // 没有
        response.append("HTTP/1.1 404 Not Found\r\n")
                .append("Content-Type: text/plain\r\n")
                .append("Content-Length: 0\r\n")
                .append("Connection: close\r\n")
                .append("\r\n");
    }

    return send_all(response, connection);
}

// 有哪些支持的方法
int process_http_options(std::map<std::string, std::string> &request_map, Connection &connection) {
    std::string response;

    response.append("HTTP/1.1 204 No Content\r\n")
            .append("Allow: GET, POST, HEAD, OPTIONS\r\n")
            .append("Content-Length: 0\r\n")
            .append("Connection: close\r\n")
            .append("\r\n");


    return send_all(response, connection);
}

// 只返回头部，不返回内容体
int process_http_head(std::map<std::string, std::string> &request_map, Connection &connection) {
    std::string path = "." + request_map["Path"];

    std::string response;

    if (file_exists(path)) {
        // 文件多大
        int64_t file_size = get_file_size(path);
        if (file_size >= 0) {
            // 成了
            response.append("HTTP/1.1 200 OK\r\n")
                    .append("Content-Type: text/html\r\n")
                    .append("Content-Length: " + std::to_string(file_size) + "\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        } else {
            // 未知错误
            response.append("HTTP/1.1 500 Internal Server Error\r\n")
                    .append("Content-Type: text/plain\r\n")
                    .append("Content-Length: 0\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        }
    } else {
        // 没有
        response.append("HTTP/1.1 404 Not Found\r\n")
                .append("Content-Type: text/plain\r\n")
                .append("Content-Length: 0\r\n")
                .append("Connection: close\r\n")
                .append("\r\n");
    }

    return send_all(response, connection);
}

// 更新资源的一部分
int process_http_patch(std::map<std::string, std::string> &request_map, Connection &connection) {
    std::string path = "." + request_map["Path"];
    std::string body = request_map["Body"];
    std::string response;

    if (file_exists(path)) {
        // 更新文件
        int updated = 0;

        if (updated == 0) {
            // 成了
            response.append("HTTP/1.1 200 OK\r\n")
                    .append("Content-Type: text/plain\r\n")
                    .append("Content-Length: 0\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        } else {
            // 未知错误
            response.append("HTTP/1.1 500 Internal Server Error\r\n")
                    .append("Content-Type: text/plain\r\n")
                    .append("Content-Length: 0\r\n")
                    .append("Connection: close\r\n")
                    .append("\r\n");
        }
    } else {
        // 没有
        response.append("HTTP/1.1 404 Not Found\r\n")
                .append("Content-Type: text/plain\r\n")
                .append("Content-Length: 0\r\n")
                .append("Connection: close\r\n")
                .append("\r\n");
    }

    return send_all(response, connection);
}

// 别的
int process_http_other(std::map<std::string, std::string> &request_map, Connection &connection) {
    std::string response;

    response.append("HTTP/1.1 405 Method Not Allowed\r\n")
            .append("Allow: GET, POST, DELETE, HEAD, OPTIONS\r\n")
            .append("Content-Type: text/plain\r\n")
            .append("Content-Length: 0\r\n")
            .append("Connection: close\r\n")
            .append("\r\n");

    return send_all(response, connection);
}



int send_all(const std::string &response, Connection &connection) {
    int fd = connection.client_fd;
    size_t send_sum = 0;
    size_t all_size = response.size();
    const char *buf = response.c_str();

    while (send_sum < all_size) {
        ssize_t send_part = send(fd, buf + send_sum, all_size - send_sum, MSG_NOSIGNAL);
        ok(running_log_type, "send_part: " + std::to_string(send_part) + " " + "send_all:" + std::to_string(all_size), __FILE__, __LINE__);

        if (send_part > 0) {
            send_sum = send_sum + send_part;
        } else if (send_part == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // usleep(100);
                continue;
            }
            no(running_log_type, "对面关闭了", __FILE__, __LINE__);
            return -1;
        }
    }

    return 0;
}
