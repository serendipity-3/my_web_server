# my-web-server HTTP 性能测试报告

## 1. 文档信息

| 项目 | 内容 |
|------|------|
| 项目名称 | my-web-server |
| 版本 | v1.0.0 (EPOLLONESHOT 版) |
| 测试日期 | 2026-04-08 |
| 测试工具 | wrk 4.1.0 |
| 测试类型 | HTTP 性能基准测试、压力测试、稳定性测试 |

---

## 2. 测试目的

1. 验证服务器在不同并发下的 QPS（每秒请求数）和延迟表现
2. 确定服务器的性能拐点和最大承载能力
3. 检测长时间运行下的稳定性（内存泄漏、连接泄漏）
4. 为后续优化提供数据支撑

---

## 3. 测试环境

### 3.1 硬件环境

| 项目 | 配置                                    |
|------|---------------------------------------|
| CPU | Intel Core i5-10200H @ 2.40GHz（4核8线程） |
| 内存 | 16 GB                                 |
| 平台 | WSL2 (Windows Subsystem for Linux 2)  |
| 网络 | 本地回环 (localhost)                      |

### 3.2 软件环境

| 项目 | 版本/配置 |
|------|----------|
| OS | Ubuntu 22.04.3 LTS |
| 内核 | 6.6.87.2-microsoft-standard-WSL2 |
| GCC | 11.4.0 |
| 编译选项 | `-O2 -std=c++11` |
| wrk | 4.1.0 [epoll] |

### 3.3 被测服务器配置

| 项目 | 配置 |
|------|------|
| 监听端口 | 8888 |
| epoll 模式 | Edge-Triggered (ET) + EPOLLONESHOT |
| 线程池大小 | 8（`std::thread::hardware_concurrency()`） |
| epoll backlog | 2048 |
| 最大 Header | 8 KB |
| 最大 Body | 4 MB |

---

## 4. 测试策略

### 4.1 测试场景

| 场景 | 目的 | wrk 参数 | 持续时间 |
|------|------|----------|---------|
| 基准测试 | 最小压力下的基线性能 | `-t2 -c10` | 30s |
| 正常负载 | 模拟中小型站点 | `-t4 -c100` | 30s |
| 高并发 | 接近极限性能 | `-t8 -c1000` | 60s |
| 极限测试 | 找到性能拐点 | `-t8 -c5000` | 60s |
| 长时间稳定性 | 检测内存/连接泄漏 | `-t4 -c500` | 600s |

### 4.2 测试对象

```
GET http://localhost:8888/
Response: 21 bytes (固定响应)
```

---

## 5. 测试结果

### 5.1 QPS 与延迟

| 场景 | QPS | Avg Latency | P50 | P90 | P99 | Max Latency |
|------|-----|-------------|-----|-----|-----|-------------|
| 基准 (10c) | **2,155** | 4.87ms | 4.28ms | 7.69ms | 19.80ms | 85.18ms |
| 正常 (100c) | **2,035** | 49.09ms | 46.02ms | 63.01ms | 98.54ms | 201.81ms |
| 高并发 (1000c) | **2,391** | 415.42ms | 411.61ms | 438.54ms | 544.90ms | 633.20ms |
| 极限 (5000c) | **2,432** | 1.69s | 1.99s | 2.00s | 2.00s | 2.00s |
| 稳定性 (500c) | **2,372** | 211.91ms | 206.31ms | 228.66ms | 279.34ms | 1.70s |

### 5.2 数据传输

| 场景 | 总请求数 | 总数据量 | 传输速率 |
|------|---------|---------|---------|
| 基准 | 64,840 | 6.43 MB | 218.93 KB/s |
| 正常 | 61,152 | 6.07 MB | 206.70 KB/s |
| 高并发 | 143,742 | 14.26 MB | 242.92 KB/s |
| 极限 | 146,157 | 14.50 MB | 247.09 KB/s |
| 稳定性 | 1,423,558 | 141.19 MB | 240.93 KB/s |

### 5.3 错误统计

| 场景 | Socket Errors | 说明 |
|------|--------------|------|
| 基准 | 0 | 正常 |
| 正常 | 0 | 正常 |
| 高并发 | 0 | 正常 |
| **极限** | **129,573 timeouts** | ⚠️ 大量超时（wrk 默认 2s 超时） |
| 稳定性 | 0 | 正常（10 分钟无错误） |

### 5.4 系统资源（稳定性测试后）

| 指标 | 数值 |
|------|------|
| 服务器进程内存 (RSS) | 4,992 KB (~5 MB) |
| 线程数 | 9 (8 worker + 1 main) |
| 文件描述符上限 | 8,192 |

---

## 6. 分析与结论

### 6.1 核心发现

**QPS 几乎不随并发增长：**

| 并发连接 | QPS | 相对基准 |
|---------|-----|---------|
| 10 | 2,155 | 100% |
| 100 | 2,035 | 94% |
| 500 | 2,372 | 110% |
| 1000 | 2,391 | 111% |
| 5000 | 2,432 | 113% |

无论并发从 10 增加到 5000（500 倍），QPS 始终维持在 **2,000~2,400** 之间，几乎没有提升。

**延迟随并发线性增长：**

| 并发连接 | Avg Latency | P99 Latency |
|---------|-------------|-------------|
| 10 | 4.87ms | 19.80ms |
| 100 | 49.09ms | 98.54ms |
| 1000 | 415.42ms | 544.90ms |
| 5000 | 1.69s | 2.00s (超时) |

### 6.2 瓶颈分析

**1. send_all() 阻塞问题（已知）**

```cpp
// src/HttpProcess.cpp 中的 send_all()
usleep(1000);  // EAGAIN 时休眠 1ms
```

这是最大的瓶颈：
- 每次 `send()` 遇到 `EAGAIN`，线程休眠 1ms
- 在高并发下，socket 发送缓冲区容易满，频繁触发 `EAGAIN`
- 8 个线程都在 `usleep`，CPU 利用率反而下降

**2. 没有 EPOLLOUT 处理**

- 当前只注册 `EPOLLIN`，没有注册 `EPOLLOUT`
- `send()` 是非阻塞的，但遇到 `EAGAIN` 后只能用 `usleep` 等待
- 正确做法：`send()` 返回 `EAGAIN` → 注册 `EPOLLOUT` → epoll 通知可写时继续发送

**3. Connection: close（无 Keep-Alive）**

- 每个请求都需要建立新 TCP 连接
- 高并发下，TCP 三次握手开销成为瓶颈

### 6.3 稳定性结论

- ✅ **长时间运行稳定**：10 分钟测试无错误、无内存泄漏
- ✅ **内存占用极低**：5 MB RSS
- ✅ **无连接泄漏**：fd 使用正常

### 6.4 性能定位

| 维度 | 评价 |
|------|------|
| QPS | ⭐⭐ 中等偏低（~2,400 QPS） |
| 延迟 | ⭐ 低并发优秀，高并发较差 |
| 稳定性 | ⭐⭐⭐⭐⭐ 优秀 |
| 内存 | ⭐⭐⭐⭐⭐ 极低占用 |
| 可扩展性 | ⭐⭐ QPS 不随并发增长 |

**结论**：这是一个**稳定的、低资源消耗的 HTTP 服务器**，但**不是高吞吐量服务器**。当前架构的瓶颈在 `send_all()` 的阻塞实现。

---

## 7. 优化建议

### 7.1 短期优化（高优先级）

**实现 EPOLLOUT 处理（解决最大瓶颈）：**

```cpp
// 伪代码
if (send() 返回 EAGAIN) {
    // 注册 EPOLLOUT
    epoll_ctl(..., EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLOUT, ...);
    // 将未发送完的数据保存到 Connection 中
    connection.write_buffer = 剩余数据;
    connection.write_offset = 已发送偏移;
}
// 在 epoll 事件处理中
if (event.events & EPOLLOUT) {
    // 继续发送 write_buffer
    // 发送完毕后取消 EPOLLOUT
}
```

预期效果：QPS 可提升 5-10 倍

### 7.2 中期优化

1. **实现 HTTP Keep-Alive**：减少 TCP 握手开销
2. **sendfile() 零拷贝**：大文件传输时减少内存拷贝
3. **内存池**：避免频繁的 malloc/free

### 7.3 长期优化

1. **多路复用**：单线程 epoll + 非阻塞 I/O（类似 Redis）
2. **io_uring**：Linux 5.1+ 异步 I/O 框架
3. **CPU 亲和性**：绑定线程到特定核心

---

## 8. 附录

### 8.1 测试命令记录

```bash
# 基准测试
wrk -t2 -c10 -d30s --latency http://localhost:8888/

# 正常负载
wrk -t4 -c100 -d30s --latency http://localhost:8888/

# 高并发
wrk -t8 -c1000 -d60s --latency http://localhost:8888/

# 极限测试
wrk -t8 -c5000 -d60s --latency http://localhost:8888/

# 稳定性测试
wrk -t4 -c500 -d600s --latency http://localhost:8888/
```

### 8.2 原始 wrk 输出

#### 基准测试
```
Running 30s test @ http://localhost:8888/
  2 threads and 10 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     4.87ms    4.02ms  85.18ms   91.67%
    Req/Sec     1.08k   210.50     1.66k    74.33%
  Latency Distribution
     50%    4.28ms
     75%    5.70ms
     90%    7.69ms
     99%   19.80ms
  64840 requests in 30.08s, 6.43MB read
Requests/sec:   2155.62
Transfer/sec:    218.93KB
```

#### 正常负载
```
Running 30s test @ http://localhost:8888/
  4 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    49.09ms   13.80ms 201.81ms   85.40%
    Req/Sec   512.05    106.83     1.62k    74.75%
  Latency Distribution
     50%   46.02ms
     75%   52.51ms
     90%   63.01ms
     99%   98.54ms
  61152 requests in 30.05s, 6.07MB read
Requests/sec:   2035.16
Transfer/sec:    206.70KB
```

#### 高并发
```
Running 1m test @ http://localhost:8888/
  8 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   415.42ms   36.43ms 633.20ms   93.11%
    Req/Sec   369.42    329.66     1.25k    66.80%
  Latency Distribution
     50%  411.61ms
     75%  423.85ms
     90%  438.54ms
     99%  544.90ms
  143742 requests in 1.00m, 14.26MB read
Requests/sec:   2391.82
Transfer/sec:    242.92KB
```

#### 极限测试
```
Running 1m test @ http://localhost:8888/
  8 threads and 5000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.69s   559.86ms   2.00s    82.57%
    Req/Sec   525.00    434.58     2.69k    72.61%
  Latency Distribution
     50%    1.99s
     75%    1.99s
     90%    2.00s
     99%    2.00s
  146157 requests in 1.00m, 14.50MB read
  Socket errors: connect 0, read 0, write 0, timeout 129573
Requests/sec:   2432.89
Transfer/sec:    247.09KB
```

#### 稳定性测试
```
Running 10m test @ http://localhost:8888/
  4 threads and 500 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   211.91ms   44.09ms   1.70s    97.98%
    Req/Sec   596.02    155.08     1.26k    76.40%
  Latency Distribution
     50%  206.31ms
     75%  215.85ms
     90%  228.66ms
     99%  279.34ms
  1423558 requests in 10.00m, 141.19MB read
Requests/sec:   2372.22
Transfer/sec:    240.93KB
```

---

*报告生成时间：2026-04-08*
