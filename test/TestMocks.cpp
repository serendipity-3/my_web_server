//
// TestMocks.cpp
// 测试用的 mock 实现
//

#include "HttpProcess.h"
#include <queue>

int epoll_fd = -1;
std::unordered_map<int, Connection> connections;
std::mutex connections_mutex;
std::mutex rearm_queue_mutex;
std::queue<int> close_queue;
std::queue<int> rearm_queue;
std::mutex close_queue_mutex;

void defer_close_fd(int fd) {}
void defer_rearm_fd(int fd) {}
