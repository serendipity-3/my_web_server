#include <csignal>
#include <cstring>
#include <iostream>
#include <arpa/inet.h>
#include <sys/epoll.h> // epoll
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

extern LOG_TYPE log_type;
// 控制 epoll 循环
bool epoll_loop_stop = false;

// 为了关闭所有的 fd，所以用一个集合存 epoll 里的所有 fd
std::unordered_set<int> epoll_fds;

void set_epoll_add_fd(int epoll_fd, int other_fd, epoll_event *event) {
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, other_fd, event);
    epoll_fds.insert(other_fd);
}

// 先从 epoll 中去掉 fd，然后关掉 fd，最后从集合中去掉 fd
void set_epoll_remove_fd(int epoll_fd, int other_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, other_fd, nullptr);
    close(other_fd);
    epoll_fds.erase(other_fd);
}

void set_epoll_clean_all(int epoll_fd) {
    for (int fd : epoll_fds) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
    }
    epoll_fds.clear();
}

// 让 socket fd 和客户端 fd 不阻塞
bool set_non_blocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        std::string err = "fcntl failed, error: ";
        err.append(strerror(errno));

        no(err, LOG_TYPE::CONSOLE);
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

int init_socket(int port) {
    const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::cerr << "socket() failed" << std::endl;
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
    set_non_blocking(socket_fd);

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
    const int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        no("epoll_create failed", LOG_TYPE::CONSOLE);
    }

    // socket 事件初始化
    epoll_event socket_event{};
    socket_event.events = EPOLLIN | EPOLLET;
    socket_event.data.fd = socket_fd;

    set_epoll_add_fd(epoll_fd, socket_fd, &socket_event);

    // 放准备好的事件
    epoll_event events[MAX_EVENTS];
    // 初始化线程池
    ThreadPool thread_pool{};

    ok("server started at port 8888", log_type);

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
                ok("closing fd, it will be auto removed from epoll", log_type);
                set_epoll_remove_fd(epoll_fd, curr_fd);
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
                            no("accept failed", log_type);
                            set_epoll_remove_fd(epoll_fd, client_fd);
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
                    set_epoll_add_fd(epoll_fd, client_fd, &client_event);
                }

                continue;
            }

            // 是读事件
            if (curr_event.events & EPOLLIN) {
                // 这里是从客户端连接读数据
                // 将这个读客户端的操作放到线程池里
                thread_pool.submit([curr_fd]() {
                    handle_client(curr_fd);
                });
                continue;
            }

            //TODO: 是写事件
            if (curr_event.events & EPOLLOUT) {

                continue;
            }
        }
    }

    // 通知所有子线程推出，然后等所有子线程推出
    thread_pool.stop();

    // 服务端和所有客户端 fd 关掉
    set_epoll_clean_all(epoll_fd);

    // epoll 关掉
    close(epoll_fd);

    ok("server stopped", log_type);

    return 0;
}