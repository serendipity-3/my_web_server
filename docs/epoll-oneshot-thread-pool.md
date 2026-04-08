# EPOLLET + 线程池：并发处理同一 fd 的竞态问题与 EPOLLONESHOT 解法

## 1. 问题描述

### 1.1 架构概览

我们的服务器使用 epoll 边缘触发（EPOLLET）+ 线程池的架构：

```
主线程                          线程池
────────                       ──────────
epoll_wait()                   ┌─────────────┐
    │                          │ Worker 1    │
    ▼                          │ Worker 2    │
EPOLLIN(client_fd) ──────────► │ Worker 3    │ ──► handle_client_read()
    │                          │ ...         │
    ▼                          │ Worker N    │
continue epoll_wait()          └─────────────┘
```

主线程负责 epoll 事件循环，收到读事件后把 fd 提交给线程池处理。线程池的 worker 负责 `recv()` 读数据、解析 HTTP、构建响应、`send()` 回客户端。

### 1.2 竞态条件

当前代码中，EPOLLIN 事件的处理没有任何保护：

```cpp
// main.cpp:244-253
if (curr_event.events & EPOLLIN) {
    thread_pool.submit([curr_fd]() {
        handle_client_read(curr_fd);  // 直接提交，不检查是否已在处理
    });
    continue;
}
```

**问题时序：**

```
主线程  ─────────────────────────────────────────────────────────►
        EPOLLIN(A)         EPOLLIN(A)         EPOLLIN(A)
           │                   │                   │
           ▼                   ▼                   ▼
        submit(A)          submit(A)          submit(A)
           │                   │                   │
           ▼                   ▼                   ▼
Worker 1 ████                 │                   │
Worker 2                       ████                │
Worker 3                                            ████
           │                   │                   │
           ▼                   ▼                   ▼
        recv(A)            recv(A)            recv(A)   ← 三个线程同时 recv 同一个 fd
        recv(A)            recv(A)            recv(A)
        recv(A)            recv(A)            recv(A)
```

**三个线程同时 `recv()` 同一个 socket fd，会发生什么？**

1. **数据交错**：多个线程的 `recv()` 调用顺序不确定，读到的数据块顺序随机
2. **HTTP 解析失败**：`connection.recv_request_buffer` 被多个线程同时 `append()`，数据乱序，请求解析必然失败
3. **内存竞争**：`connection.recv_request_buffer` 没有锁保护，多线程同时写入是数据竞争（data race），未定义行为
4. **EAGAIN 误判**：一个线程读空了缓冲区拿到 EAGAIN，另一个线程此时也来读，也拿到 EAGAIN，认为"没有更多数据"，但实际上第一个线程已经读完了

### 1.3 为什么 EPOLLET 让这个问题更严重

EPOLLET（边缘触发）的语义是：**只有状态从"无数据"变为"有数据"时才通知一次**。

对比水平触发（LT）：

```
水平触发 (LT)：
  数据到达 ──► epoll_wait 返回 ──► 如果没读完，下次 epoll_wait 还会返回
  不怕错过事件，因为只要缓冲区有数据，就一直通知

边缘触发 (ET)：
  数据到达 ──► epoll_wait 返回 ──► 如果没读完，下次 epoll_wait 不会返回
  必须一次读到 EAGAIN，否则数据永远卡在缓冲区
```

如果不用 EPOLLONESHOT，ET 模式下：
- Worker A 正在处理 `client_fd`（读到一半）
- 新数据到达，ET 触发第二次 EPOLLIN
- 主线程把 `client_fd` 再次提交到线程池
- Worker B 开始处理同一个 fd
- **Worker A 和 B 同时读，数据全部乱掉**

而且因为 ET 只在边缘触发，**如果你错过了这次事件（比如正在处理中忽略了它），数据就永远读不到了**——直到下次有新数据到达。

---

## 2. 为什么其他方案不行

### 2.1 方案 A：只加 `processing` 标志，跳过重复提交

```cpp
// 伪代码：只靠 processing 标志
if (curr_event.events & EPOLLIN) {
    lock(connections_mutex);
    auto it = connections.find(curr_fd);
    if (it != connections.end() && it->second.processing) {
        // 已经在处理了，跳过
        unlock();
        continue;
    }
    it->second.processing = true;
    unlock();
    thread_pool.submit([curr_fd]() {
        handle_client_read(curr_fd);
    });
}
```

**问题：丢失事件**

```
Worker A:    ████ 处理中 (processing=true) ████ 完成 (processing=false) ████
                                                                  ↑
                                                         defer_close_fd(client_fd)
                                                         但 close_queue 还没处理

主线程:                           EPOLLIN(A) 到来
                                      │
                                      ▼
                                 检查 processing=false
                                      │
                                      ▼
                                 又提交了！Worker B 也开始处理
                                      │
                                      ▼
                                 两个线程又同时处理了
```

更隐蔽的竞态窗口：worker 把 `processing` 设为 `false` 的瞬间，到 `defer_close_fd()` 执行之间的窗口期。如果此时 EPOLLIN 到来，又会重复提交。

即使 `processing` 守护了"处理中不重复提交"，还有一个根本问题：**处理完一个请求后（比如 HTTP/1.1 Keep-Alive），连接没关，客户端发第二个请求。** 此时新数据到达，ET 触发 EPOLLIN，你必须处理它。但如果没有正确管理，可能：
- 第一次处理时 `processing=false` 设置太早 → 第二次提交被允许 → 并发
- `processing=false` 设置太晚 → 第二次事件被跳过 → 数据丢失

### 2.2 方案 B：加读写锁

```cpp
// 伪代码：每个 fd 一个读写锁
std::unordered_map<int, std::shared_mutex> fd_locks;

// 主线程
fd_locks[curr_fd].lock();  // 独占锁
thread_pool.submit([curr_fd]() {
    handle_client_read(curr_fd);
    fd_locks[curr_fd].unlock();
});
```

**问题：**
1. 你锁住的是主线程，不是 worker。主线程 `lock()` 之后提交任务，然后呢？unlock 放在哪？放 worker 里？那主线程 block 住了
2. 即使改成 `try_lock`，本质上还是"跳过"事件，和方案 A 一样的问题
3. 引入了死锁风险：worker 处理完要 unlock，但 worker 可能提前 return（客户端关闭、错误），锁永远不释放
4. 性能差：每次 epoll_wait 都要 lock/unlock

### 2.3 方案 C：水平触发（LT）代替边缘触发（ET）

把 `EPOLLET` 换成水平触发。

**问题：**
1. LT 模式下，如果 worker 没读完，epoll_wait 会反复返回 → 反复提交任务 → 还是并发
2. LT 的通知频率更高，性能比 ET 差（每次 epoll_wait 都要遍历所有有数据的 fd）
3. 没有解决根本问题，只是让问题更容易触发了

---

## 3. 正确方案：EPOLLONESHOT

### 3.1 EPOLLONESHOT 语义

`EPOLLONESHOT` 是 epoll 的一个标志位：

> 一个 fd 注册了 `EPOLLONESHOT` 后，epoll 最多为该 fd 触发一次事件。触发后，该 fd 被自动"静默"——不再产生任何事件，直到显式调用 `epoll_ctl(EPOLL_CTL_MOD)` 重新注册。

```cpp
// 注册时
epoll_event ev{};
ev.data.fd = client_fd;
ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);

// 事件触发后（内核自动行为）：
//   - fd 从 epoll 的就绪列表中移除
//   - 后续的数据到达不会产生新事件
//   - 必须显式 epoll_ctl(EPOLL_CTL_MOD) 才会重新监听
```

### 3.2 配合 rearm 机制

核心思路：

1. 主线程提交任务时，fd 已经被 EPOLLONESHOT "静默"了，不会重复触发
2. Worker 处理完后，通知主线程"可以重新监听这个 fd 了"
3. 主线程在 `epoll_wait` 之前，统一 rearm 所有需要重新监听的 fd

**完整的事件流：**

```
时间线 ─────────────────────────────────────────────────────────────────────────────►

主线程    epoll_wait()    EPOLLIN(A)     epoll_wait()         EPOLLIN(A)
              │               │               │                   │
              │               ▼               │                   │
              │           submit(A)           │                   │
              │            (EPOLLONESHOT      │                   │
              │             生效，A不会       │                   │
              │             再触发)           │                   │
              │               │               │                   │
              ▼               ▼               ▼                   ▼
                          ┌───────────────────────┐
Worker 1                  │  recv() until EAGAIN  │
                          │  HTTP parse           │
                          │  process request      │
                          │  send() response      │
                          │  defer_rearm_fd(A) ◄──┤ ──► rearm_queue: [A]
                          └───────────────────────┘        │
                                                           │
主线程                            epoll_wait() 之前         │
                                      │                    │
                                      ▼                    ▼
                                  rearm_queue 取出 A
                                      │
                                      ▼
                              epoll_ctl(EPOLL_CTL_MOD, A, EPOLLIN|EPOLLET|EPOLLONESHOT)
                                      │
                                      ▼
                                  epoll_wait()
                                      │
                                  (如果有数据已经在缓冲区) ──► 立即返回 EPOLLIN(A)
                                  (如果没数据) ──► 阻塞等待
```

### 3.3 为什么不会丢事件

**关键：ET 边缘触发 + EPOLLONESHOT + rearm = 不丢事件**

rearm 时（`epoll_ctl EPOLL_CTL_MOD`），epoll 会检查当前 fd 的状态：

```
rearm 时刻 fd 的状态           epoll 行为
──────────────────────────────────────────────────
缓冲区有数据（有新 EPOLLIN）    epoll 立即将 fd 加入就绪列表
                              下一次 epoll_wait() 立即返回该事件
                              不会丢！

缓冲区没数据                    epoll 正常注册，等待下次数据到达触发
                              正常处理
```

这是 epoll 的保证：**rearm 操作会触发一次状态检查**。如果 fd 当前已经可读，rearm 后会立即触发 EPOLLIN（因为 EPOLLET 看的是"边缘变化"，从"未注册"到"已注册且可读"也是一个边缘）。

### 3.4 为什么不用在 worker 里直接 rearm

你可能会想：为什么不让 worker 直接 `epoll_ctl(EPOLL_CTL_MOD)`？

**原因：epoll 不是线程安全的**

虽然 `epoll_ctl()` 和 `epoll_wait()` 在 Linux 上可以跨线程调用（内核保证线程安全），但在实际服务器设计中，**epoll 的 fd 应该只由一个线程管理**，原因：

1. **简化模型**：所有 epoll 操作（add/del/mod）集中在主线程，逻辑清晰
2. **避免惊群**：如果多个 worker 同时 rearm，epoll_wait 可能被多个线程唤醒
3. **错误处理集中**：rearm 失败的错误在主线程统一处理
4. **扩展性**：后续如果要用 EPOLLOUT，也需要主线程统一管理

所以我们用"队列"的方式：worker 把需要 rearm 的 fd 放到队列里，主线程在 `epoll_wait()` 之前统一处理。

---

## 4. 实现细节

### 4.1 数据结构：rearm 队列

参考已有的 `close_queue` 模式：

```cpp
// main.cpp

// 已有的 close_queue
std::mutex close_queue_mutex;
std::queue<int> close_queue;
void defer_close_fd(int fd) {
    std::lock_guard<std::mutex> lock(close_queue_mutex);
    close_queue.push(fd);
}

// 新增 rearm_queue（同样的模式）
std::mutex rearm_queue_mutex;
std::queue<int> rearm_queue;
void defer_rearm_fd(int fd) {
    std::lock_guard<std::mutex> lock(rearm_queue_mutex);
    rearm_queue.push(fd);
}
```

### 4.2 修改 epoll 注册

```cpp
// main.cpp:231
// 原来
client_event.events = EPOLLIN | EPOLLET;

// 改为
client_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
```

### 4.3 主线程：epoll_wait 之前处理 rearm

```cpp
// main.cpp 主循环，epoll_wait() 之前
while (!epoll_loop_stop) {
    // ★ 新增：处理 rearm 队列
    {
        std::lock_guard<std::mutex> lock(rearm_queue_mutex);
        while (!rearm_queue.empty()) {
            int fd = rearm_queue.front();
            rearm_queue.pop();

            epoll_event ev{};
            ev.data.fd = fd;
            ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;

            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0) {
                // fd 可能已经被关闭了（defer_close_fd），忽略 EBADF
                if (errno != EBADF) {
                    no(running_log_type, "rearm fd failed", __FILE__, __LINE__);
                }
            }
        }
    }

    // 原有的 epoll_wait
    int fds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    // ...
}
```

### 4.4 Worker：处理完后 rearm

```cpp
// HttpProcess.cpp - handle_client_read() 的各个退出路径

void handle_client_read(int client_fd) {
    // ... recv loop ...

    // 正常处理完请求
    if (connection.request_completed) {
        // ... process_http() ...
        // ... send_all() ...

        // ★ 处理完一个请求后，重新监听这个 fd（等待下一个请求）
        defer_rearm_fd(client_fd);
        return;
    }

    // recv 返回 0，客户端关闭
    if (bytes_received == 0) {
        defer_close_fd(client_fd);
        // 关闭路径不需要 rearm
        return;
    }

    // 请求非法或太大
    if (/* bad request */) {
        defer_close_fd(client_fd);
        // 关闭路径不需要 rearm
        return;
    }

    // recv 返回 EAGAIN（请求还没读完）
    // 正常情况不会走到这里，因为 while loop 会一直读到 EAGAIN
    // 但如果确实走到这里了：
    defer_rearm_fd(client_fd);
}
```

**注意：关闭路径不调用 defer_rearm_fd**，因为：
1. fd 马上要被关闭了，rearm 没意义
2. 即使 rearm 了，`epoll_ctl MOD` 对已关闭的 fd 会返回 EBADF，无害但多余

### 4.5 处理顺序

主线程每一轮事件循环的顺序：

```
1. 处理 rearm_queue    → epoll_ctl(EPOLL_CTL_MOD) 重新注册
2. epoll_wait()         → 等待事件
3. 处理事件             → accept / submit / error
4. 处理 close_queue     → epoll_ctl(EPOLL_CTL_DEL) + close()
```

为什么 rearm 在 epoll_wait **之前**：
- rearm 后，如果缓冲区有数据，epoll_wait 会**立即返回**（不会阻塞）
- 如果 rearm 在 epoll_wait **之后**，那本轮 epoll_wait 可能阻塞很久，即使有数据已经到了

为什么 close 在 epoll_wait **之后**：
- 事件处理中可能有新的 fd 要关闭（`defer_close_fd`）
- 统一在最后批量关闭，减少 `epoll_ctl` 调用次数

---

## 5. 完整的时序对比

### 5.1 没有 EPOLLONESHOT（当前代码 - 有 bug）

```
时间     主线程                    Worker 1              Worker 2
──────────────────────────────────────────────────────────────────
T0       epoll_wait()
T1       EPOLLIN(A)
         submit(A)
T2                                recv(A) - 读到 1KB
                                  recv(A) - EAGAIN
T3       EPOLLIN(A) ← 新数据到达
         submit(A) ← 又提交了！
T4                                recv(A) - 继续读       recv(A) - 开始读
                                  recv(A)                 recv(A)
                                  append(buffer1)         append(buffer2)
                                  ↑                       ↑
                                  两个线程同时写 connection.recv_request_buffer
                                  数据竞争！未定义行为！
T5                                HTTP parse - 失败      HTTP parse - 失败
```

### 5.2 有 EPOLLONESHOT（修复后）

```
时间     主线程                    Worker 1
──────────────────────────────────────────────────────────────────
T0       epoll_wait()
T1       EPOLLIN(A)
         submit(A)
         (EPOLLONESHOT 生效，A 静默)
T2                                recv(A) - 读到 1KB
                                  recv(A) - EAGAIN
T3                                HTTP parse
                                  process request
                                  send response
                                  defer_rearm_fd(A)
         ★ 新数据到达，但 epoll 不会通知
         ★ 因为 EPOLLONESHOT，A 还在静默中
T4       epoll_wait() 之前
         rearm_queue: [A]
         epoll_ctl(MOD, A, EPOLLIN|EPOLLET|EPOLLONESHOT)
         ★ rearm 完成，epoll 检查 A 的状态
         ★ 发现缓冲区有数据，立即将 A 加入就绪列表
T5       epoll_wait() 返回
         EPOLLIN(A) ← 安全地拿到事件
         submit(A)
T6                                recv(A) - 读到新数据
                                  正常处理...
```

---

## 6. 边界情况

### 6.1 rearm 时 fd 已经关闭

Worker A 处理完，调用 `defer_rearm_fd(A)`。但同时 Worker B 处理另一个请求时，调用了 `defer_close_fd(A)`（比如客户端在响应后立即关闭）。

时序：
```
Worker A:  defer_rearm_fd(A)  →  rearm_queue: [A]
Worker B:  defer_close_fd(A)  →  close_queue: [A]

主线程:
  1. 处理 rearm_queue: epoll_ctl(MOD, A, ...)  ← 成功（fd 还没关）
  2. epoll_wait()
  3. EPOLLHUP(A) ← 因为客户端关闭了
  4. 处理 close_queue: epoll_ctl(DEL, A) + close(A) ← 正常关闭
```

**无害**。rearm 时 fd 还活着，操作成功。下一轮 epoll_wait 会收到 EPOLLHUP，正常关闭。

如果时序反过来（close 先执行）：
```
主线程:
  1. 处理 close_queue: epoll_ctl(DEL, A) + close(A)  ← fd 关了
  2. 处理 rearm_queue: epoll_ctl(MOD, A, ...)  ← EBADF，忽略
```

**也无害**。`epoll_ctl` 对已关闭的 fd 返回 EBADF，我们忽略它。

### 6.2 rearm 后立即有新数据

rearm 完成后，缓冲区已经有新数据了。

epoll 的行为：
- `EPOLL_CTL_MOD` 会检查 fd 的当前状态
- 发现缓冲区有数据，且是 EPOLLET 模式（边缘触发）
- 将 fd 加入就绪列表
- 下一次 `epoll_wait()` 立即返回，不会阻塞

**不会丢事件**。

### 6.3 请求处理期间客户端关闭

Worker 正在处理请求，客户端突然关闭连接。

```
Worker:
  recv() 返回 0（FIN 包）
  → defer_close_fd(client_fd)
  → return（不调用 defer_rearm_fd）
```

**正常**。close 路径不调用 rearm，fd 被正常关闭。

### 6.4 epoll_wait 被信号中断

`epoll_wait()` 返回 -1，`errno == EINTR`。

当前代码直接 `continue`，无影响。rearm 在 epoll_wait **之前**处理，不会阻塞。

### 6.5 多个 rearm 和 close 同时排队

同一轮事件循环中，fd A 被 rearm 了，也被 close 了。

```
rearm_queue: [A, B, C]
close_queue: [A, D]

主线程处理顺序：
1. rearm A, B, C
2. epoll_wait()
3. 处理事件
4. close A, D
```

A 先被 rearm，然后被 close。`epoll_ctl(DEL)` 成功，`close()` 成功。无问题。

---

## 7. 与 close_queue 的对比

我们的 rearm_queue 和已有的 close_queue 是完全一样的模式：

```
             close_queue                rearm_queue
────────────────────────────────────────────────────────
线程安全     mutex + queue              mutex + queue
写入方       Worker (defer_close_fd)    Worker (defer_rearm_fd)
读取方       主线程 (epoll_wait 后)      主线程 (epoll_wait 前)
操作         epoll_ctl(DEL) + close()   epoll_ctl(MOD)
失败处理     忽略（fd 可能已被关）       忽略 EBADF
```

这是一个通用的"线程间通信"模式：worker 不能直接操作 epoll，通过队列把操作请求传给主线程执行。

---

## 8. 总结

| 问题 | 根因 | 解法 |
|------|------|------|
| 多线程同时处理同一 fd | epoll 没有"一个 fd 同时只触发一次"的保证 | EPOLLONESHOT |
| Worker 完成后需要重新监听 | 主线程不知道 worker 什么时候处理完 | rearm_queue |
| Rearm 可能和 close 冲突 | Worker A 要 rearm，Worker B 要 close | 先处理 rearm，再 close，无冲突 |
| rearm 时 fd 可能已关闭 | 时序不确定 | 忽略 EBADF |

**EPOLLONESHOT + rearm_queue** 是 epoll + 线程池架构的标准模式。nginx、libevent、libev、muduo 等成熟网络库都使用这个模式。

---

## 9. 参考

- `man 2 epoll` - EPOLLONESHOT 的语义说明
- Linux 内核源码 `fs/eventpoll.c` - `ep_modify()` 函数，rearm 时的状态检查
- 陈硕《Linux 多线程服务端编程》第 8 章 - muduo 网络库的 epoll + 线程池设计
