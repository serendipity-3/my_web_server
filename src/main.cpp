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
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "HttpProcess.h"
#include "MyLog.h"
#include "ThreadPool.h"


extern LOG_TYPE start_log_type;

// keep epoll-loop running
bool epoll_loop_stop = false;
// epoll file descriptor
int epoll_fd;
// OpenSSL using
// SSL_CTX* global_ssl_ctx = nullptr;

// Don't have the child-thread close a fd.
// So there use a queue. When the child-thread wants to close a fd, then put the fd in the queue.
// The main thread will use the queue to close them.
std::mutex close_queue_mutex;
std::queue<int> close_queue;
void defer_close_fd(int other_fd) {
    std::lock_guard<std::mutex> lock(close_queue_mutex);
    close_queue.push(other_fd);
}

// 可能子线程处理一个 client_fd 时，epoll 又收到了这个 fd 的读事件。导致主线程将这个 client_fd 加入任务队列。
// 结果多个线程同时处理一个 client_fd 的 recv()，导致 HTTP 请求数据错乱。

// 为了子线程处理当前 client_fd 时，不会触发多次 epoll 读事件。
// 所以用了 EPOLLONESHOT。它只会通知一次，然后等我重新添加到 epoll 里，再通知下一次。

// 为了让主线程知道[子线程处理完当前 clien_fd 了，并且数据没有读完，要下一次 epoll 交到任务队列，才能读完]。
// 所以用一个队列，存没有读完的 client_fd，让主线程重新把它加入 epoll 里。
std::mutex rearm_queue_mutex;
std::queue<int> rearm_queue;
void defer_rearm_fd(int fd) {
    std::lock_guard<std::mutex> lock(rearm_queue_mutex);
    rearm_queue.push(fd);
}

// 用一个 map 存所有的客户端连接
// 一个客户端 socket fd 对应一个 Connection 结构体
std::unordered_map<int, Connection> connections;
std::mutex connections_mutex;

void map_epoll_add_fd(int epoll_fd, int client_fd, epoll_event *event, Connection &connection) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, event) < 0) {
        std::ostringstream oss;
        oss << "epoll_ctl ADD failed, fd: " << client_fd << ", error: " << strerror(errno);
        no(running_log_type, oss.str(), __FILE__, __LINE__);

        close(client_fd);
        return; // 不加入 connections
    }

    std::unique_lock<std::mutex> lock(connections_mutex);
    connections.emplace(client_fd, connection);
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
    for (const auto& kv : connections) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, kv.first, nullptr);
        close(kv.first);
    }
    connections.clear();
}

// 让 socket fd 和客户端 fd 不阻塞
int set_non_blocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        std::string err = "fcntl failed, error: ";
        err.append(strerror(errno));

        no(start_log_type, err, __FILE__, __LINE__);
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int init_socket(int port) {
    const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        no(LOG_TYPE::CONSOLE, "socket() failed",  __FILE__, __LINE__);
        return -1;
    }

    struct sockaddr_in socket_in = {};
    socket_in.sin_family = AF_INET; // ipv4
    socket_in.sin_port = htons(port); // 本机字节序转为网络字节序
    socket_in.sin_addr.s_addr = INADDR_ANY; // 所有 IP 都能连

    // 为了服务可以快速重启，所以启动端口复用
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        no(LOG_TYPE::CONSOLE, "setsockeopt failed",  __FILE__, __LINE__);
        close(socket_fd);
        return -1;
    }

    if (bind(socket_fd, reinterpret_cast<struct sockaddr *>(&socket_in), sizeof(socket_in)) < 0) {
        no(LOG_TYPE::CONSOLE, "port bind failed", __FILE__, __LINE__);
        close(socket_fd);
        return -1;
    }

    if (listen(socket_fd, 2048) < 0) {
        no(LOG_TYPE::CONSOLE, "listen failed", __FILE__, __LINE__);
        close(socket_fd);
        return -1;
    }

    // 设置 socket 非阻塞，accept 不会等
    if (set_non_blocking(socket_fd) < 0) {
        no(LOG_TYPE::CONSOLE, "set_non_blocking failed",  __FILE__, __LINE__);
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

    const int port = 8888;
    const int MAX_EVENTS = 1024;

    // 初始化日志文件
    init_log();

    // SSL_library_init();                // 初始化 TLS 加密算法，静态全局
    // SSL_load_error_strings();         // 错误描述注册，静态全局
    // OpenSSL_add_all_algorithms();     // 加载所有加密算法，静态全局

    // 初始化 socket
    const int socket_fd = init_socket(port);
    // 初始化 epoll, 放入 socket fd
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        no( LOG_TYPE::CONSOLE, "epoll_create failed", __FILE__, __LINE__);
        return -1;
    }

    // 初始化一个socket 事件，放到在内核中的 epoll 红黑树数据结构里
    epoll_event socket_event{};
    socket_event.events = EPOLLIN | EPOLLET;
    socket_event.data.fd = socket_fd;
    // 放到 epoll 里
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &socket_event) < 0) {
        no(LOG_TYPE::CONSOLE, "epoll_ctl ADD socket_fd failed", __FILE__, __LINE__);
        close(socket_fd);
        close(epoll_fd);
        return -1;
    }

    // 放准备好的事件
    epoll_event events[MAX_EVENTS];
    // 来一个能处理读写事件的线程池
    ThreadPool thread_pool{};

    // 注册信号，用来合理关闭 server 进程
    signal(SIGINT, handle_stop);
    signal(SIGTERM, handle_stop);

    ok(running_log_type, "server started at port 8888", __FILE__, __LINE__);

    // GO, GO, GO
    while (!epoll_loop_stop) {
        // 将未读完的 client_fd 重新加入 epoll里
        {
            std::unique_lock<std::mutex> lock(rearm_queue_mutex);
            while (!rearm_queue.empty()) {
                int fd = rearm_queue.front();
                rearm_queue.pop();

                epoll_event client_event{};
                client_event.data.fd = fd;
                client_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

                // 重新加入 epoll 里
                if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &client_event) < 0) {
                    // EBADF 说明 fd 已经被 close_queue 关了，不用重复关闭
                    // 其他错误（EINVAL/EPERM）说明 fd 有问题，需要关闭
                    if (errno != EBADF) {
                        std::ostringstream oss;
                        oss << "epoll_ctl MOD failed, fd: " << fd << ", error: " << strerror(errno);
                        no(running_log_type, oss.str(), __FILE__, __LINE__);
                        defer_close_fd(fd);
                    }
                }
            }
        }


        // 拿到一堆准备好的 fd
        int fds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (fds == -1) {
            continue;
        }

        // 处理所有 epoll 里的 fd
        for (int i = 0; i < fds; i++) {
            epoll_event curr_event = events[i];
            int curr_fd = events[i].data.fd;

            // 出错了,或客户端关了
            if (curr_event.events & (EPOLLERR | EPOLLHUP)) {
                ok(running_log_type, "closing fd, it will be auto removed from epoll", __FILE__, __LINE__);
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
                        }
                        // 错了
                        no(running_log_type, "accept failed", __FILE__, __LINE__);
                        break;
                    }
                    // 设置非阻塞
                    set_non_blocking(client_fd);

                    // 加到 epoll 里。
                    // 读事件 + 边缘触发 + 只通知一次
                    epoll_event client_event{};
                    client_event.data.fd = client_fd;
                    client_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
                    int port = ntohs(client_addr.sin_port);

                    Connection new_connection {client_fd, port, std::string(ip_str), std::string(), false, ConnectionState::PROCESSING};
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

        // socket 已经收到了所有 fd, 所有读事件交给了任务队列后，最后关掉所有要关闭的 fd
        {
            std::unique_lock<std::mutex> lock(close_queue_mutex);
            while (!close_queue.empty()) {

                // 从队列里，拿到一个 fd，准备关闭
                int fd = close_queue.front();
                // 在队列里，消除第一个 fd
                close_queue.pop();

                // 在 epoll 中，拿掉这个 fd
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);

                // 在全局变量（）中，拿掉这个 fd 的缓存区
                std::unique_lock<std::mutex> lock_connections(connections_mutex);
                connections.erase(fd);
            }
        }


    }

    // 通知所有子线程要收工了，然后等所有子线程搞定退出
    thread_pool.stop();

    // 所有客户端 fd 关掉
    map_epoll_clean_all(epoll_fd);
    close(socket_fd);

    // epoll 关掉
    close(epoll_fd);

    ok(running_log_type, "server stopped", __FILE__, __LINE__);

    // 关闭日志文件
    close_log();

    return 0;
}