#include <cstring>
#include <iostream>
#include<sys/socket.h> // socket(), bind(), listen(), ...
#include <netinet/in.h> // socket_in
#include<sys/types.h>
#include <unistd.h>
#include "MyLog.h"

using namespace std;

auto log_type = LOG_TYPE::FILE;

int init_socket(int port) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::cerr << "socket() failed" << std::endl;
        return -1;
    }

    struct sockaddr_in socket_in = {};
    socket_in.sin_family = AF_INET; // ipv4
    socket_in.sin_port = htons(port); // 本机字节序转为网络字节序
    socket_in.sin_addr.s_addr = INADDR_ANY; // 所有 IP 都能连

    if (bind(socket_fd, reinterpret_cast<struct sockaddr *>(&socket_in), sizeof(socket_in)) < 0) {
        no("port bind failed", log_type);
        close(socket_fd);
        return -1;
    }

    if (listen(socket_fd, 5) < 0) {
        no("listen failed", log_type);
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

int main(void *argv[], void * argc) {
    // 初始化 socket
    int port = 8888;
    int socket_fd = init_socket(port);

    // 初始化线程池


    // 初始化 epoll


    // 启动服务器


    return 0;
}