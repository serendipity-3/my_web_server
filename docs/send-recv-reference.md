# send() / recv() 完整参考

## 函数签名

```cpp
#include <sys/socket.h>

ssize_t send(int sockfd, const void* buf, size_t len, int flags);
ssize_t recv(int sockfd, void* buf, size_t len, int flags);
```

---

## 1. 典型请求流程

```
客户端                          服务端
  |                               |
  |  -------- TCP 握手 -------->  |
  |  <--------------------------  |
  |  -------------------------->  |
  |                               |
  |  recv() 返回 >0              |  send() 返回 >0
  |  (读到请求数据)                |  (发送请求数据)
  |                               |
  |  <======== HTTP 请求 =======  |
  |                               |
  |  send() 返回 >0              |  recv() 返回 >0
  |  (发送响应数据)                |  (读到响应数据)
  |  <======= HTTP 响应 ========  |
  |                               |
  |  -------- FIN 包 ---------->  |  recv() 返回 0
  |  <-------- FIN 包 -----------  |  (对端关闭)
  |  -------- ACK ------------->  |
```

---

## 2. send() 返回值

### 2.1 返回值 > 0

**含义：** 成功发送了 N 个字节到内核发送缓冲区。

**注意：** 
- 不代表对端已经收到
- 只是写入了本地内核的发送队列
- TCP 协议栈负责后续传输

```cpp
ssize_t n = send(fd, buf, 1024, 0);
// n = 512  → 内核接受了 512 字节，缓冲区可能满了
// n = 1024 → 全部接受
```

**为什么可能发送不完：**
- 内核发送缓冲区满了
- 对端接收慢，TCP 滑动窗口为 0
- 非阻塞 socket 时更容易发生

### 2.2 返回值 == 0

**含义：** 发送了 0 字节。

**唯一场景：** 你传入的 `len` 就是 0。

```cpp
send(fd, buf, 0, 0);  // 返回 0
```

**这不是错误，也没有实际意义。**

### 2.3 返回值 == -1

**含义：** 发送失败，查看 `errno`。

#### 常见 errno 值：

| errno | 含义 | 何时发生 | 处理方式 |
|-------|------|----------|----------|
| `EAGAIN` / `EWOULDBLOCK` | 内核缓冲区满 | 非阻塞 socket，发送缓冲区已满 | 等 epoll 可写事件后重试 |
| `EINTR` | 被信号中断 | 发送过程中收到信号 | 直接重试，不是错误 |
| `EPIPE` | 对端已关闭 | 对端调用了 close()，你还在发 | 关闭本地 socket |
| `ECONNRESET` | 连接被重置 | 对端进程崩溃或发送 RST 包 | 关闭本地 socket |
| `ECONNREFUSED` | 连接被拒绝 | 对端没有进程监听该端口 | 关闭本地 socket |
| `ENOTCONN` | 未建立连接 | socket 未 connect() 或已断开 | 关闭本地 socket |
| `EBADF` | 无效的文件描述符 | fd 已关闭或无效 | 代码 bug |
| `EFAULT` | 缓冲区地址无效 | buf 指针指向非法内存 | 代码 bug |
| `EMSGSIZE` | 消息太大 | UDP 下超过最大报文长度 | 减小发送长度 |
| `ENOBUFS` | 内存不足 | 系统内存耗尽 | 关闭一些连接 |

#### 阻塞 vs 非阻塞 socket 的区别

**阻塞 socket (默认)：**
```
send() 调用
    ↓
内核缓冲区有空间？
    ├── 是 → 立即返回 n > 0
    └── 否 → 阻塞等待，直到有空间或出错
```

**非阻塞 socket (O_NONBLOCK)：**
```
send() 调用
    ↓
内核缓冲区有空间？
    ├── 是 → 立即返回 n > 0
    └── 否 → 立即返回 -1, errno = EAGAIN
```

---

## 3. recv() 返回值

### 3.1 返回值 > 0

**含义：** 读到了 N 个字节的数据。

```cpp
char buf[4096];
ssize_t n = recv(fd, buf, sizeof(buf), 0);
// n = 1024 → 读到了 1024 字节
// n = 100  → 缓冲区只有 100 字节可用（不一定读满你的 buf）
```

**关键理解：**
- `recv()` 不保证读满你请求的长度
- 返回的是当前可用的数据量
- 可能比你请求的少

### 3.2 返回值 == 0

**含义：** 对端关闭了连接（TCP FIN 包到达）。

**这是 `recv()` 独有的语义，`send()` 没有。**

```
对端调用 close()
    ↓
内核发送 FIN 包
    ↓
你的 recv() 返回 0
```

**必须立即关闭本地 socket，不要再发送数据。**

```cpp
ssize_t n = recv(fd, buf, sizeof(buf), 0);
if (n == 0) {
    close(fd);  // 对端已关闭
    return;
}
```

### 3.3 返回值 == -1

**含义：** 接收失败，查看 `errno`。

#### 常见 errno 值：

| errno | 含义 | 何时发生 | 处理方式 |
|-------|------|----------|----------|
| `EAGAIN` / `EWOULDBLOCK` | 没有数据可读 | 非阻塞 socket，接收缓冲区为空 | 等 epoll 可读事件后重试 |
| `EINTR` | 被信号中断 | 接收过程中收到信号 | 直接重试，不是错误 |
| `ECONNRESET` | 连接被重置 | 对端发送 RST 包（进程崩溃） | 关闭本地 socket |
| `ETIMEDOUT` | 连接超时 | TCP keepalive 超时 | 关闭本地 socket |
| `EBADF` | 无效的文件描述符 | fd 已关闭或无效 | 代码 bug |
| `EFAULT` | 缓冲区地址无效 | buf 指针指向非法内存 | 代码 bug |
| `EINVAL` | 参数无效 | 传入了无效的 flags | 代码 bug |

#### 阻塞 vs 非阻塞 socket 的区别

**阻塞 socket (默认)：**
```
recv() 调用
    ↓
内核缓冲区有数据？
    ├── 是 → 立即返回 n > 0
    └── 否 → 阻塞等待，直到有数据或对端关闭或出错
```

**非阻塞 socket (O_NONBLOCK)：**
```
recv() 调用
    ↓
内核缓冲区有数据？
    ├── 是 → 立即返回 n > 0
    └── 否 → 立即返回 -1, errno = EAGAIN
```

---

## 4. send() 和 recv() 配合使用

### 4.1 标准模式：阻塞 socket

```cpp
// 发送请求
ssize_t send_all(int fd, const void* buf, size_t len) {
    const char* ptr = (const char*)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = send(fd, ptr, remaining, MSG_NOSIGNAL);
        
        if (n > 0) {
            ptr += n;
            remaining -= n;
        } else if (n == -1) {
            if (errno == EINTR) {
                continue;  // 信号中断，重试
            }
            return -1;  // 真正的错误
        }
    }
    return len;
}

// 接收响应
ssize_t recv_all(int fd, void* buf, size_t len) {
    char* ptr = (char*)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = recv(fd, ptr, remaining, 0);
        
        if (n > 0) {
            ptr += n;
            remaining -= n;
        } else if (n == 0) {
            return len - remaining;  // 对端关闭，返回已接收的字节数
        } else {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
    }
    return len;
}
```

### 4.2 epoll + 非阻塞模式

```cpp
void handle_read(int fd) {
    char buf[4096];
    
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        
        if (n > 0) {
            // 处理数据
            process_data(buf, n);
        } else if (n == 0) {
            // 对端关闭
            close_connection(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据读完了，等待下次 epoll 通知
                return;
            } else if (errno == EINTR) {
                continue;
            } else {
                // 真正的错误
                close_connection(fd);
                return;
            }
        }
    }
}

void handle_write(int fd, const char* data, size_t len) {
    size_t sent = 0;
    
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        
        if (n > 0) {
            sent += n;
        } else if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 内核缓冲区满了
                // 保存未发送的数据，注册 EPOLLOUT 事件
                save_pending_data(fd, data + sent, len - sent);
                epoll_mod(fd, EPOLLIN | EPOLLOUT);
                return;
            } else if (errno == EINTR) {
                continue;
            } else {
                close_connection(fd);
                return;
            }
        }
    }
}
```

### 4.3 epoll 事件与 recv/send 的关系

```
EPOLLIN 触发
    ↓
recv() 调用
    ├── 返回 >0  → 继续处理数据
    ├── 返回 0   → 对端关闭，移除 epoll 监听
    └── 返回 -1  → 
          ├── EAGAIN → 数据读完，正常返回
          └── 其他   → 错误，移除 epoll 监听

EPOLLOUT 触发
    ↓
send() 调用
    ├── 返回 >0  → 数据已发送，检查是否还有剩余
    │               ├── 有剩余 → 继续发送
    │               └── 无剩余 → 移除 EPOLLOUT 监听
    └── 返回 -1  →
          ├── EAGAIN → 缓冲区又满了（罕见），等待下次
          └── 其他   → 错误，移除 epoll 监听
```

---

## 5. 常见错误处理

### 5.1 忽略 SIGPIPE

```cpp
#include <signal.h>

// main() 开头调用
signal(SIGPIPE, SIG_IGN);
```

**为什么：**
- 对端关闭后，你调用 `send()` 会收到 `SIGPIPE` 信号
- 默认行为是终止进程
- 忽略后，`send()` 返回 -1, errno = EPIPE，你可以正常处理

### 5.2 MSG_NOSIGNAL 标志

```cpp
send(fd, buf, len, MSG_NOSIGNAL);
```

**作用：** 等同于忽略 `SIGPIPE`，但只对本次调用生效。

**推荐：** 使用 `MSG_NOSIGNAL` 而不是全局忽略信号。

### 5.3 完整的错误处理表

| 场景 | send() 行为 | recv() 行为 |
|------|-------------|-------------|
| 正常通信 | 返回 >0 | 返回 >0 |
| 缓冲区满 | -1, EAGAIN | — |
| 无数据可读 | — | -1, EAGAIN |
| 对端关闭 | -1, EPIPE | 返回 0 |
| 对端崩溃 | -1, ECONNRESET | -1, ECONNRESET |
| 信号中断 | -1, EINTR | -1, EINTR |
| 超时 | — | -1, ETIMEDOUT |

---

## 6. 常见问题

### 6.1 为什么 recv() 返回的比我请求的少？

```cpp
recv(fd, buf, 4096, 0);  // 请求 4096 字节
// 返回 100 → 只有 100 字节可用
```

**原因：**
- TCP 是流协议，没有消息边界
- 对端可能分多次发送
- 内核缓冲区当前只有这么多

**解决：** 循环接收，直到收满或对端关闭。

### 6.2 为什么 send() 发送不完？

```cpp
send(fd, buf, 4096, 0);  // 请求发送 4096 字节
// 返回 1024 → 只发送了 1024 字节
```

**原因：**
- 内核发送缓冲区空间不足
- TCP 滑动窗口限制（对端接收慢）
- 网络拥塞

**解决：** 循环发送，直到发完。

### 6.3 EAGAIN 和 EWOULDBLOCK 是什么关系？

```cpp
if (errno == EAGAIN || errno == EWOULDBLOCK) {
    // 相同的含义，不同的宏名
}
```

**历史原因：** 有些系统定义 `EAGAIN`，有些定义 `EWOULDBLOCK`，有些两个都定义但值相同。

**处理：** 总是两个都检查。

### 6.4 非阻塞 socket 怎么设？

```cpp
#include <fcntl.h>

int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

或者在 `accept()` 时直接设置：

```cpp
int client_fd = accept(listen_fd, ...);
fcntl(client_fd, F_SETFL, O_NONBLOCK);
```
