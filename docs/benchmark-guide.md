# HTTP 服务器压测指南

## 1. 核心概念

### 1.1 三个关键指标

| 指标 | 含义 | 单位 |
|------|------|------|
| **QPS** (Queries Per Second) | 服务器每秒能处理多少个请求 | req/s |
| **延迟 (Latency)** | 一个请求从发出到收到响应的时间 | ms |
| **吞吐量 (Throughput)** | 服务器每秒传输的数据量 | MB/s |

### 1.2 三个关键参数

| 参数 | 含义 | 你关心什么 |
|------|------|-----------|
| **线程数 (-t)** | 压测工具用多少个线程来发请求 | 影响客户端 CPU 利用率，一般设为客户端 CPU 核心数 |
| **并发数 (-c)** | 同时有多少个连接在飞 | **这是核心参数**，直接影响 QPS 和延迟 |
| **持续时间 (-d)** | 压测跑多久 | 太短结果不稳定，一般 10-30 秒 |

### 1.3 连接数 vs 线程数（重要）

```
线程数 (threads)：压测工具内部的 worker 线程，用来驱动 I/O
并发数 (connections)：同时与服务器建立的 TCP 连接数

两者的关系：
  - 每个线程可以管理多个连接（wrk 使用 epoll/kqueue）
  - 1 个线程可以驱动几千个并发连接
  - 所以：并发数 >> 线程数 是正常的

你的 Python 脚本的问题：
  8 个线程 = 8 个并发连接（每个线程只管 1 个连接）
  wrk：4 个线程 = 256 个并发连接（每个线程管 64 个连接）
```

### 1.4 你的服务器架构限制

```
你的服务器：
  - epoll 主线程：1 个（负责 accept + epoll_wait）
  - 线程池：8 个 worker（默认 = CPU 核心数）
  - 最大并发处理：8 个请求同时在 worker 里执行
  - 但可以同时持有 >>8 个 TCP 连接（epoll 管理）

瓶颈分析：
  连接数 ≤ 8 时：worker 有空闲，QPS 随并发线性增长
  连接数 ≈ 8 时：worker 全满，QPS 达到峰值
  连接数 > 8 时：请求在任务队列里排队，延迟增加，QPS 不再增长
```

---

## 2. 压测工具对比

| 工具 | 并发模型 | 适用场景 | 安装 |
|------|----------|----------|------|
| **wrk** | 单进程 epoll | 标准压测，测峰值 QPS | `sudo apt install wrk` |
| **wrk2** | 单进程 epoll | 固定速率压测，测延迟分布 | 编译安装 |
| **ab** | 多进程 | 简单快速测试 | `sudo apt install apache2-utils` |
| **hey** | goroutine | Go 写的，简单好用 | `go install github.com/rakyll/hey@latest` |
| 你的 Python 脚本 | 多线程 urllib | 仅适合验证功能，不适合压测 | - |

**推荐用 wrk**，业界标准，nginx/redis 官方压测都用它。

---

## 3. wrk 使用方法

### 3.1 基本语法

```bash
wrk -t<线程数> -c<并发数> -d<持续时间> <URL>

# 示例
wrk -t4 -c100 -d10s http://localhost:8888/
```

### 3.2 参数选择

```bash
# 线程数 (-t)：设为客户端 CPU 核心数，或略少
# 你的机器 8 核，用 4 个线程就够（wrk 单线程可以驱动几万连接）
wrk -t4 -c100 -d10s http://localhost:8888/

# 并发数 (-c)：从低到高递增，找 QPS 拐点
wrk -t4 -c10  -d10s http://localhost:8888/   # 低并发
wrk -t4 -c50  -d10s http://localhost:8888/   # 中并发
wrk -t4 -c100 -d10s http://localhost:8888/   # 高并发
wrk -t4 -c200 -d10s http://localhost:8888/   # 超高并发

# 持续时间 (-d)：至少 10 秒，推荐 30 秒
wrk -t4 -c100 -d30s http://localhost:8888/

# 带 Lua 脚本（POST 请求等复杂场景）
wrk -t4 -c100 -d10s -s post.lua http://localhost:8888/
```

### 3.3 输出解读

```
Running 10s test @ http://localhost:8888/
  4 threads and 100 connections                ← 4 个线程，100 个并发连接
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.23ms    0.45ms   12.34ms    92.13%   ← 延迟统计
    Req/Sec    20.56k     1.23k    24.32k    78.45%    ← 每线程每秒请求数
  819832 requests in 10.01s, 123.45MB read             ← 总请求数，总读取量
Requests/sec:  81912.34                                ← QPS（核心指标）
Transfer/sec:     12.34MB                              ← 吞吐量
```

**关键看什么：**
- `Requests/sec`：QPS，越高越好
- `Latency Avg`：平均延迟，越低越好
- `Latency Max`：最大延迟，如果比 Avg 大很多倍，说明有请求卡住了
- `+/- Stdev`：延迟一致性，越高说明延迟越稳定

---

## 4. 压测流程

### 4.1 准备

```bash
# 1. 关闭日志文件写入（会严重影响性能）
#    在 main.cpp 里把 init_log() 改成只输出到 CONSOLE
#    或者临时注释掉 ok()/no() 调用

# 2. 确保服务器已编译并运行
cd cmake-build-debug && make -j8
./WebServer &

# 3. 安装 wrk
sudo apt install wrk

# 4. 准备测试页面（服务器根目录下放一个小文件）
echo "Hello World" > index.html
```

### 4.2 基线测试（warm up）

```bash
# 先跑一轮热身，让 CPU 频率升上来，页表预热
wrk -t2 -c10 -d5s http://localhost:8888/
```

### 4.3 逐步加压

```bash
# 并发数从小到大，记录每次的 QPS 和延迟

wrk -t4 -c1   -d10s http://localhost:8888/  > result_c1.txt
wrk -t4 -c8   -d10s http://localhost:8888/  > result_c8.txt
wrk -t4 -c16  -d10s http://localhost:8888/  > result_c16.txt
wrk -t4 -c32  -d10s http://localhost:8888/  > result_c32.txt
wrk -t4 -c64  -d10s http://localhost:8888/  > result_c64.txt
wrk -t4 -c128 -d10s http://localhost:8888/  > result_c128.txt
wrk -t4 -c256 -d10s http://localhost:8888/  > result_c256.txt
wrk -t4 -c512 -d10s http://localhost:8888/  > result_c512.txt
```

### 4.4 记录结果

```
并发数    QPS        Avg Latency    P99 Latency    错误数
──────   ──────     ──────────     ──────────     ──────
1        5,000      0.20ms         0.50ms         0
8        38,000     0.21ms         0.60ms         0       ← 线程池刚好用满
16       42,000     0.38ms         1.20ms         0       ← QPS 增长变缓
32       43,000     0.74ms         2.50ms         0       ← 几乎不再增长 = 瓶颈
64       43,200     1.48ms         5.00ms         0       ← 延迟翻倍，QPS 不变
128      43,100     2.97ms         10.00ms        0       ← 延迟继续涨
256      43,000     5.95ms         20.00ms        0
```

**分析：**
- QPS 在 c=8 附近快速增长（线程池从空闲到用满）
- QPS 在 c=16~32 附近到达峰值（线程池全满）
- 继续加并发，QPS 不涨，延迟翻倍（请求在队列里排队）

### 4.5 绘制曲线

```
QPS
 ↑
 │            ╭─────────────  ← QPS 饱和点（线程池瓶颈）
 │          ╭─╯
 │        ╭─╯
 │      ╭─╯
 │    ╭─╯
 │  ╭─╯
 │╭─╯
 ├──────────────────────────→ 并发数
 1  8  16  32  64  128 256
```

---

## 5. 你的服务器预期性能

### 5.1 理论上限

```
假设：
  - 8 核 CPU，线程池 8 个 worker
  - 每个请求处理时间 = 0.1ms（纯内存，小响应）
  - 每个 worker 每秒可以处理 1/0.0001 = 10,000 个请求

理论 QPS = 8 workers × 10,000 = 80,000 QPS

实际上：
  - epoll_wait 有开销
  - HTTP 解析有开销
  - send() 有开销
  - 锁竞争有开销
  - 预期实际 QPS ≈ 20,000 ~ 50,000（小静态文件）
```

### 5.2 影响 QPS 的因素

```
因素                      影响程度    说明
──────────────────────────────────────────────────
响应大小                   高        1KB vs 1MB 差距巨大
文件读取（磁盘 I/O）       高        mmap 缓存命中率
HTTP 解析复杂度            中        请求头越多越慢
日志写入                   高        文件 I/O 会卡住 worker
锁竞争                     中        connections_mutex 争用
TCP 拥塞控制               中        高并发下 TCP 窗口抖动
```

---

## 6. 和线上对比

### 6.1 "线上"指什么

| 场景 | 工具 | 预期 QPS | 说明 |
|------|------|----------|------|
| nginx (epoll) | wrk | 100,000+ | C 实现，极度优化，多年打磨 |
| Node.js (epoll) | wrk | 30,000~50,000 | V8 + libuv |
| 你的服务器 (epoll) | wrk | 20,000~50,000 | C++11，刚实现，有优化空间 |
| Python Flask (同步) | wrk | 500~2,000 | 单线程，同步阻塞 |
| 你的 Python 脚本 | - | 几百~1,500 | 并发=8，受客户端限制 |

### 6.2 为什么差这么多

```
nginx 为什么快：
  1. 异步 I/O：send() 非阻塞，用 EPOLLOUT 管理写事件
  2. 内存池：避免频繁 malloc/free
  3. 零拷贝：sendfile() 直接从文件到 socket，不经过用户态
  4. 无锁设计：每个 worker 有独立的 epoll 和连接池
  5. 编译优化：LTO、PGO、CPU 指令集优化

你的服务器当前差距：
  1. send() 阻塞：用 usleep(EAGAIN) 而不是 EPOLLOUT → 高并发下卡 worker
  2. 单个 epoll：所有事件走主线程 → 高并发下主线程成瓶颈
  3. 没有 sendfile()：每次都 read() + send() → 多一次内存拷贝
  4. 有锁：connections_mutex → 高并发下争用
  5. 日志写文件：每次 ok()/no() 都 fwrite → 阻塞
```

---

## 7. 压测脚本对比

### 7.1 你的 Python 脚本

```python
# 问题总结：
# 1. urllib.request 同步阻塞，每线程 1 连接
# 2. 8 线程 = 8 并发，受 GIL 限制
# 3. 每个请求新建 TCP 连接
# 4. 实际测的是 "客户端瓶颈"，不是 "服务器瓶颈"

# 结果：可能测出 500~1,500 QPS
```

### 7.2 wrk

```bash
# 4 线程，100 并发，HTTP Keep-Alive
wrk -t4 -c100 -d10s http://localhost:8888/

# 优势：
# 1. 单线程 epoll 驱动几万连接
# 2. 连接复用（Keep-Alive）
# 3. 无 GIL 限制（C 实现）
# 4. 业界标准，结果可对比

# 结果：可能测出 20,000~50,000 QPS
```

### 7.3 差距

```
你的 Python 脚本：    ~1,000 QPS（受客户端限制）
wrk：                ~30,000 QPS（受服务器限制）

差 30 倍。因为你测的是客户端的瓶颈，不是服务器的瓶颈。
```

---

## 8. 快速开始

```bash
# 1. 启动服务器
cd /home/yuan/workspace/clion/my-web-server/cmake-build-debug
./WebServer &

# 2. 安装 wrk
sudo apt install wrk

# 3. 创建测试文件
echo "Hello World" > /home/yuan/workspace/clion/my-web-server/index.html

# 4. 热身
wrk -t2 -c10 -d5s http://localhost:8888/

# 5. 标准测试
wrk -t4 -c100 -d10s http://localhost:8888/

# 6. 高并发测试
wrk -t4 -c256 -d10s http://localhost:8888/

# 7. 看结果
# 关注 Requests/sec（QPS）和 Latency
```
