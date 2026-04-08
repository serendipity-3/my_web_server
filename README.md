# My Web Server

一个基于 **epoll (边缘触发) + 线程池** 的 C++11 HTTP Web 服务器。

单二进制文件，支持 GET、POST、PUT、DELETE、HEAD、OPTIONS、PATCH 方法。

## 特性

- 🚀 **高性能 I/O**：Linux epoll 边缘触发 (ET) + 非阻塞 socket
- 🧵 **线程池并发**：自动匹配 CPU 核心数的线程池
- 🔒 **线程安全**：EPOLLONESHOT 防止同一 fd 并发处理
- 📝 **日志系统**：支持控制台/文件/双输出模式
- 🔐 **TLS 不支持**：OpenSSL 集成（当前已注释，不可启用）
- 📦 **轻量级**：极低内存占用（~5 MB）

## 快速开始

### 环境要求

- Linux (epoll 特有)
- C++11 编译器 (GCC 4.8+ / Clang 3.3+)
- CMake 3.22.1+
- [vcpkg](https://github.com/microsoft/vcpkg)（管理依赖）

### 安装依赖

```bash
# 安装 vcpkg
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh

# 安装项目依赖
~/vcpkg/vcpkg install openssl nlohmann-json --triplet x64-linux
```

### 编译

```bash
cd cmake-build-debug && cmake .. && make
```

编译产物：
- `WebServer` - 主服务器
- `MyLogTest` - 日志测试
- `ThreadPoolTest` - 线程池测试
- `FileTest` - 文件处理测试
- `JsonTest` - JSON 解析测试

### 运行

```bash
./cmake-build-debug/WebServer
```

服务器启动后监听 `0.0.0.0:8888`。

### 测试

```bash
# 简单请求
curl http://localhost:8888/

# 压力测试（需要 wrk）
wrk -t4 -c100 -d30s --latency http://localhost:8888/
```

## 架构

### 请求处理流程

```
┌─────────────────────────────────────────────────────────────┐
│                        Main Thread                          │
│                                                             │
│   epoll_wait ──► accept() ──► 注册 EPOLLIN | EPOLLET | EPOLLONESHOT  │
│       │                                                     │
│       ▼                                                     │
│   ┌─────────────────────────────────────────────────────┐   │
│   │              Thread Pool (N workers)                 │   │
│   │                                                     │   │
│   │   recv() ──► HTTP 解析 ──► 方法分发 ──► 构建响应    │   │
│   │                                          │          │   │
│   │                                          ▼          │   │
│   │                                     send_all()      │   │
│   │                                          │          │   │
│   │                                          ▼          │   │
│   │                                    defer_close_fd() │   │
│   └─────────────────────────────────────────────────────┘   │
│       │                                                     │
│       ▼                                                     │
│   处理 close_queue + rearm_queue（主线程专用）               │
└─────────────────────────────────────────────────────────────┘
```

### 线程安全规则

| 操作 | 规则 |
|------|------|
| 访问 `connections` | 必须持有 `connections_mutex` |
| 关闭 fd | 只能主线程通过 `close_queue` |
| 重连 fd | 只能主线程通过 `rearm_queue` |
| 防止重复处理 | 设置 `connection.processing = true` |

### 文件结构

```
my-web-server/
├── src/
│   ├── main.cpp          # epoll 事件循环、连接生命周期
│   ├── HttpProcess.cpp   # HTTP 解析、方法分发、响应构建
│   ├── FileProcess.cpp   # 文件 I/O 工具
│   ├── ThreadPool.cpp    # 线程池实现
│   └── MyLog.cpp         # 日志系统
├── include/
│   ├── HttpProcess.h     # HTTP 处理头文件
│   ├── FileProcess.h     # 文件处理头文件
│   ├── ThreadPool.h      # 线程池头文件
│   └── MyLog.h           # 日志头文件
├── test/                 # 单元测试
├── docs/                 # 技术文档
├── CMakeLists.txt        # 构建配置
└── README.md             # 本文件
```

## 支持的 HTTP 方法

| 方法 | 说明 | 状态 |
|------|------|------|
| GET | 获取资源 | ✅ 完整支持 |
| POST | 创建资源 | ⚠️ 仅支持 `./file` 路径 |
| PUT | 更新资源 | ⚠️ 未实现 |
| DELETE | 删除资源 | ⚠️ 未做路径遍历检查 |
| HEAD | 获取响应头 | ✅ 完整支持 |
| OPTIONS | 查询支持的方法 | ✅ 完整支持 |
| PATCH | 部分更新 | ⚠️ 未实现 |

## 性能

测试环境：Intel i5-10200H (4核8线程)，WSL2

| 并发连接 | QPS | Avg Latency | P99 Latency |
|---------|-----|-------------|-------------|
| 10 | 2,155 | 4.87ms | 19.80ms |
| 100 | 2,035 | 49.09ms | 98.54ms |
| 1,000 | 2,391 | 415.42ms | 544.90ms |
| 5,000 | 2,432 | 1.69s | 2.00s |

详细报告见 [docs/benchmark-report.md](docs/benchmark-report.md)

## 配置限制

| 参数 | 值 | 说明 |
|------|-----|------|
| 监听端口 | 8888 | 硬编码 |
| epoll backlog | 2048 | 最大等待连接数 |
| 最大 Header | 8 KB | `MAX_HEADER_SIZE` |
| 最大 Body | 4 MB | `MAX_BODY_SIZE` |
| epoll 事件数 | 1024 | 每次最多处理事件 |

## 日志

服务器运行时生成两个日志文件：
- `ok.log` - 正常运行日志
- `no.log` - 错误日志

日志模式在 `src/main.cpp` 中配置：

```cpp
LOG_TYPE running_log_type = LOG_TYPE::CONSOLE;  // CONSOLE / FILE / BOTH
```

## 已知限制

- **无 EPOLLOUT 处理**：`send()` 遇到 `EAGAIN` 时使用 `usleep(1000)` 阻塞等待
- **无 Keep-Alive**：所有响应使用 `Connection: close`
- **单 Content-Type**：GET 响应只返回 `text/html`
- **无路由表**：POST 仅处理固定路径
- **PUT/PATCH 未实现**：Body 写入逻辑为占位

## 优化方向

1. **EPOLLOUT 处理**：解决 send 阻塞问题，预期 QPS 提升 5-10 倍
2. **HTTP Keep-Alive**：减少 TCP 握手开销
3. **sendfile 零拷贝**：大文件传输优化
4. **路由系统**：支持 URL 路由和参数解析

## 技术文档

项目包含以下技术文档（位于 `docs/` 目录）：

- [EPOLLONESHOT 线程池方案](docs/epoll-oneshot-thread-pool.md)
- [性能测试指南](docs/benchmark-guide.md)
- [性能测试报告](docs/benchmark-report.md)
- [Git 提交规范](docs/git-commit-convention.md)
- [C++11 线程同步机制](docs/cpp11-thread-synchronization.md)
- [CPU 缓存一致性](docs/cpu-cache-coherence.md)
- [HTTP/1.1 协议详解](docs/HTTP11_Xianxia.md)

## 贡献

欢迎提交 Issue 和 Pull Request。

### 代码规范

- **命名**：类型用 CamelCase，函数/变量用 snake_case，私有成员尾部加 `_`
- **缩进**：4 空格
- **大括号**：开括号同行
- **头文件**：使用 `#ifndef` 保护

详见 [AGENTS.md](AGENTS.md)

## 许可证

暂无许可证声明。

## 致谢

- 使用 [epoll](https://man7.org/linux/man-pages/man7/epoll.7.html) 实现高性能 I/O 多路复用
- 依赖 [OpenSSL](https://www.openssl.org/) 和 [nlohmann/json](https://github.com/nlohmann/json)
- 参考 [muduo](https://github.com/chenshuo/muduo) 网络库设计思想
