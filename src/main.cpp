#include <csignal>
#include <cstring>
#include <iostream>
#include <arpa/inet.h>
#include <sys/epoll.h> // epoll_create(), ...
#include <fcntl.h>      // fcntl() 设置非阻塞
#include<sys/socket.h> // socket(), bind(), listen(), ...
#include <netinet/in.h> // socket_in
#include<sys/types.h>
#include <unistd.h>
#include <unordered_set>

#include "HttpProcess.h"
#include "MyLog.h"
#include "ThreadPool.h"

using namespace std;

extern LOG_TYPE start_log_type;

// 控制 epoll 循环
bool epoll_loop_stop = false;
// epoll 文件描述符
int epoll_fd;

// 为了防止[子线程刚关闭 fd 主线程就 accept 了相同的 fd]的情况，用一个队列存子线程要关的所有 fd，统一在主线程中关闭。
std::mutex close_queue_mutex;
std::queue<int> close_queue;

void defer_close_fd(int other_fd) {
    std::lock_guard<std::mutex> lock(close_queue_mutex);
    close_queue.push(other_fd);
}

// 用一个 map 存所有的客户端连接
std::unordered_map<int, Connection> connections;
std::mutex connections_mutex;

void map_epoll_add_fd(int epoll_fd, int other_fd, epoll_event *event, Connection &connection) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, other_fd, event) < 0) {
        std::ostringstream oss;
        oss << "epoll_ctl ADD failed, fd: " << other_fd << ", error: " << strerror(errno);
        no(oss.str(), running_log_type);

        close(other_fd);
        return; // 不加入 connections
    }

    std::unique_lock<std::mutex> lock(connections_mutex);
    connections.emplace(other_fd, connection);
    lock.unlock();
}

void map_epoll_remove_fd(int epoll_fd, int other_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, other_fd, nullptr);
    std::unique_lock<std::mutex> lock(connections_mutex);
    close(other_fd);
    connections.erase(other_fd);
    lock.unlock();
}

void map_epoll_clean_all(int epoll_fd) {
    for (const auto& [fd, _] : connections) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
    }
    connections.clear();
}

// 让 socket fd 和客户端 fd 不阻塞
int set_non_blocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        std::string err = "fcntl failed, error: ";
        err.append(strerror(errno));

        no(err, start_log_type);
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int init_socket(int port) {
    const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        no("socket() failed", LOG_TYPE::CONSOLE);
        return -1;
    }

    struct sockaddr_in socket_in = {};
    socket_in.sin_family = AF_INET; // ipv4
    socket_in.sin_port = htons(port); // 本机字节序转为网络字节序
    socket_in.sin_addr.s_addr = INADDR_ANY; // 所有 IP 都能连

    // 为了服务可以快速重启，所以启动端口复用
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        no("setsockeopt failed", LOG_TYPE::CONSOLE);
        close(socket_fd);
        return -1;
    }

    if (bind(socket_fd, reinterpret_cast<struct sockaddr *>(&socket_in), sizeof(socket_in)) < 0) {
        no("port bind failed", LOG_TYPE::CONSOLE);
        close(socket_fd);
        return -1;
    }

    if (listen(socket_fd, 5) < 0) {
        no("listen failed", LOG_TYPE::CONSOLE);
        close(socket_fd);
        return -1;
    }

    // 设置 socket 非阻塞，accept 不会等
    if (set_non_blocking(socket_fd) < 0) {
        no("set_non_blocking failed", LOG_TYPE::CONSOLE);
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

// 信号通知，关闭
void handle_stop(int sig) {
    epoll_loop_stop = true;
}

int main(int argc, char* argv[]) {

    int port = 8888;
    int MAX_EVENTS = 1024;

    // 注册信号通知关闭
    signal(SIGINT, handle_stop);
    signal(SIGTERM, handle_stop);

    // 初始化 socket
    const int socket_fd = init_socket(port);
    // 初始化 epoll, 放入 socket fd
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        no("epoll_create failed", LOG_TYPE::CONSOLE);
        return -1;
    }

    // socket 事件初始化
    epoll_event socket_event{};
    socket_event.events = EPOLLIN | EPOLLET;
    socket_event.data.fd = socket_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &socket_event) < 0) {
        no("epoll_ctl ADD socket_fd failed", LOG_TYPE::CONSOLE);
        close(socket_fd);
        close(epoll_fd);
        return -1;
    }

    // 放准备好的事件
    epoll_event events[MAX_EVENTS];
    // 初始化线程池
    ThreadPool thread_pool{};

    ok("server started at port 8888", running_log_type);

    // GO, GO, GO
    while (!epoll_loop_stop) {
        int fds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (fds == -1) {
            continue;
        }

        // 处理所有 epoll 里的 fd
        for (int i = 0; i< fds; i++) {
            epoll_event curr_event = events[i];
            int curr_fd = events[i].data.fd;

            // 出错了,或客户端关了
            if (curr_event.events & (EPOLLERR | EPOLLHUP)) {
                ok("closing fd, it will be auto removed from epoll", running_log_type);
                map_epoll_remove_fd(epoll_fd, curr_fd);
                continue;
            }

            // 服务端 socket
            if (curr_fd == socket_fd) {
                // 接收新的客户端，将客户端 fd 加到 epoll 里
                // 拿到所有 accept 队列中的 fd
                while (true) {

                    sockaddr_in client_addr{};
                    socklen_t client_addr_length = sizeof(client_addr);
                    int client_fd = accept(socket_fd, (struct sockaddr*)& client_addr, &client_addr_length);

                    // 空了吗，错了吗
                    if (client_fd < 0) {
                        // 空了，兼容一下 EWOULDBLOCK
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // accept 队列空了，等下一轮通知吧
                            break;
                        } else {
                            // 错了
                            no("accept failed", running_log_type);
                            map_epoll_remove_fd(epoll_fd, client_fd);
                            break;
                        }
                    }
                    // 设置非阻塞
                    set_non_blocking(client_fd);

                    // 加到 epoll 里。
                    // 边缘触发 + 读事件
                    epoll_event client_event{};
                    client_event.data.fd = client_fd;
                    client_event.events = EPOLLIN | EPOLLET;

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
                    int port = ntohs(client_addr.sin_port);

                    Connection new_connection {client_fd, port, ip_str, "", false};
                    map_epoll_add_fd(epoll_fd, client_fd, &client_event, new_connection);
                }

                continue;
            }

            // 是读事件
            if (curr_event.events & EPOLLIN) {
                // 这里是从客户端连接读数据
                // 将这个读客户端的操作放到线程池里
                thread_pool.submit([curr_fd]() {
                    handle_client_read(curr_fd);
                });
                continue;
            }

            //TODO: 是写事件
            if (curr_event.events & EPOLLOUT) {

                continue;
            }
        }
        // 关掉所有不用的 fd
        {
            std::unique_lock<std::mutex> lock(close_queue_mutex);
            while (!close_queue.empty()) {
                int fd = close_queue.front();
                close_queue.pop();

                // epoll 中去除
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);

                // 全局状态中去除
                std::unique_lock<std::mutex> lock_conn(connections_mutex);
                connections.erase(fd);
            }
        }


    }

    // 通知所有子线程退出，然后等所有子线程退出
    thread_pool.stop();

    // 所有客户端 fd 关掉
    map_epoll_clean_all(epoll_fd);
    close(socket_fd);

    // epoll 关掉
    close(epoll_fd);

    ok("server stopped", running_log_type);

    return 0;
}