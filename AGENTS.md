# Agent Instructions for my-web-server

## Project Overview
C++11 HTTP web server using epoll (edge-triggered, non-blocking) + thread pool for concurrent request processing. Supports GET, POST, PUT, DELETE, HEAD, OPTIONS, PATCH. Single binary, no framework.

## Build & Run

### Prerequisites
- **vcpkg** installed at `~/vcpkg` with `x64-linux` triplet. Dependencies: `openssl`, `nlohmann-json`.
- CMake 3.22.1+, C++11 compiler, pthread.

### Build
```bash
cd cmake-build-debug && cmake .. && make
```
All targets: `WebServer`, `MyLogTest`, `ThreadPoolTest`, `FileTest`, `JsonTest`.

### Run
```bash
./cmake-build-debug/WebServer        # starts on port 8888
./cmake-build-debug/MyLogTest        # logging tests
./cmake-build-debug/ThreadPoolTest   # thread pool tests
./cmake-build-debug/FileTest         # file processing tests
./cmake-build-debug/JsonTest         # JSON parsing tests
```

### Notes
- `test/MyServerTest.cpp` exists but is **NOT** in CMakeLists.txt (not built).
- TLS/SSL code in `main.cpp` is **commented out** — server runs plaintext only currently.
- Logs write to `ok.log` and `no.log` in project root.

## Architecture

### Entry Point
`src/main.cpp` — epoll event loop on port 8888, backlog 2048.

### Flow
1. Main thread: epoll_wait → accept new connections → add to epoll (EPOLLIN | EPOLLET)
2. On EPOLLIN: submit `handle_client_read()` to thread pool
3. Thread pool worker: recv loop (non-blocking) → parse HTTP → dispatch to `process_http_*()` → `send_all()` → `defer_close_fd()`
4. Main thread: processes `close_queue` each epoll iteration (child threads never close fds directly)

### File Boundaries
| File | Purpose |
|------|---------|
| `src/main.cpp` | epoll loop, socket setup, signal handling, connection lifecycle |
| `src/HttpProcess.cpp` | HTTP parsing, method dispatch, response building, file serving |
| `src/FileProcess.cpp` | File I/O utilities (exists, size, content, time-based naming) |
| `src/ThreadPool.cpp` | Thread pool implementation |
| `src/MyLog.cpp` | Logging (console/file/both via `ok()` / `no()`) |

### Key Data Structures
```cpp
struct Connection {
    int client_fd;
    int port;
    std::string ip;
    std::string read_buffer;
    bool request_completed;
    bool processing;       // prevents concurrent processing of same fd
};

extern std::unordered_map<int, Connection> connections;  // guarded by connections_mutex
extern std::mutex connections_mutex;
extern void defer_close_fd(int fd);  // thread-safe: queues fd for main-thread close
```

### Limits
- Max header: 8KB (`MAX_HEADER_SIZE`)
- Max body: 4MB (`MAX_BODY_SIZE`)
- epoll max events: 1024

## Thread Safety Rules
- **ALWAYS** lock `connections_mutex` before accessing `connections` map
- Set `connection.processing = true` immediately after acquiring lock to prevent double-processing
- **NEVER** close fds from worker threads — use `defer_close_fd()` instead
- `close_queue` has its own `close_queue_mutex` — main thread drains it each epoll cycle

## Known Limitations & TODOs
- **No EPOLLOUT handling**: `send_all()` blocks with `usleep(1000)` on `EAGAIN`. Large responses may stall.
- **PUT/PATCH incomplete**: Body writing logic is stubbed (`int updated = 0` always).
- **Path traversal**: DELETE does not sanitize `../` — noted as TODO in code.
- **No routing table**: POST only handles `./file` path. TODO: global route map.
- **No Keep-Alive**: All responses use `Connection: close`.
- **Single Content-Type**: Only `text/html` served for GET responses.

## Code Style
- **Naming**: CamelCase for types, snake_case for functions/vars, trailing `_` for private members
- **Braces**: Opening on same line
- **Indent**: 4 spaces
- **Headers**: Include guards (`#ifndef XXX_H`), CLion creator comment
- **Include order**: Standard library → System → Third-party → Project headers
- **JSON**: `using json = nlohmann::json;` after includes
- **Return convention**: `0`/`true` = success, `-1`/`false` = failure

## Dependencies
- **OpenSSL** (`OpenSSL::SSL`, `OpenSSL::Crypto`) — via vcpkg
- **nlohmann_json** (`nlohmann_json::nlohmann_json`) — via vcpkg
- **pthread** — auto-linked on Linux
