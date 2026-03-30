# C++ Web Server 项目介绍

## 项目概述

基于 C++ 实现的高性能 HTTP 服务器，采用 epoll I/O 多路复用 + 线程池并发模型，支持静态文件访问。

**技术栈**: C++11, epoll, pthread, OpenSSL, nlohmann_json

**源码**: https://github.com/example/web-server (可根据实际情况填写)

---

## 核心架构

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Main Thread│────▶│   epoll     │────▶│ Thread Pool │
│  (accept)   │     │  (I/O multiplexing) │  (8 workers)│
└─────────────┘     └─────────────┘     └─────────────┘
                                              │
                                              ▼
                                       ┌─────────────┐
                                       │ HTTP Parser │
                                       │  & Response │
                                       └─────────────┘
```

---

## 核心功能

1. **HTTP 服务**: GET/POST/PUT/DELETE/HEAD/OPTIONS/PATCH
2. **静态文件服务**: 读取并返回 HTML/JSON 等文件
3. **TLS/HTTPS**: 支持 OpenSSL 加密连接
4. **并发处理**: epoll ET 模式 + 线程池

---

## 性能数据

- **QPS**: ~2000 (静态文件)
- **并发模型**: epoll + 8 线程
- **backlog**: 2048

---

## 面试可能问到的问题

### 1. epoll 相关

- epoll 的两种触发模式 (LT/ET) 有什么区别？
- 为什么使用 ET 模式？有什么注意事项？
- epoll + 多线程的并发模型是如何工作的？

### 2. 并发与线程安全

- 多个线程同时处理同一个 fd 会出问题吗？如何解决？
- 什么是线程池？为什么要用线程池？
- connections map 和 close_queue 是如何保证线程安全的？

### 3. 网络编程

- listen() backlog 参数是什么意思？
- 非阻塞 socket 有什么作用？
- TCP 半连接队列和全连接队列是什么？

### 4. HTTP 协议

- HTTP 请求头有哪些部分？如何解析？
- Content-Length 的作用是什么？
- 什么是 HTTP keep-alive？

### 5. 性能优化

- 如何进一步提升 QPS？
- sendfile() 是什么？有什么好处？
- 如何处理大文件传输？

### 6. 调试与问题排查

- 服务器响应为空的原因有哪些？
- 如何排查高并发下的性能问题？

---

## 亮点与改进

### 已解决的技术难点

1. **多线程并发处理同一 fd** - 添加 `processing` 标记防止重复处理
2. **fd 提前关闭** - 响应发送完成后再关闭连接
3. **默认路径映射** - `/` 自动映射到 `/index.html`

### 可改进的方向

1. 实现 HTTP keep-alive
2. 使用 sendfile() 零拷贝
3. 实现 EPOLLOUT 支持大文件
4. 支持 HTTP/1.1 分块传输
5. 添加日志库 (spdlog)
6. 性能监控指标

---

## 常见追问

**Q: 为什么不用 libevent / libuv / muduo？**
> 为了学习底层原理，自己动手实现 epoll 封装。生产环境建议用成熟库。

**Q: 如何保证不丢请求？**
> 当前模型是"一请求一连接"，处理完关闭。如需长连接需实现 keep-alive。

**Q: 线程数怎么确定？**
> 当前用 `hardware_concurrency()`，也可以根据业务特点调整（CPU 密集 vs I/O 密集）。
