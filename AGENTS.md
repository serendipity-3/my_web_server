# Agent Instructions for my-web-server

## Project Overview
This is a C++11 HTTP web server using epoll for I/O multiplexing and a thread pool for concurrent request processing. It supports HTTP methods (GET, POST, PUT, DELETE, HEAD, OPTIONS, PATCH) and TLS/SSL encryption via OpenSSL.

## Build Commands

### Full Build
```bash
# Using cmake-build-debug (CLion default)
cd cmake-build-debug && cmake .. && make

# Using build directory
cd build && cmake .. && make
```

### Running Single Tests
```bash
# Run MyLog tests
./cmake-build-debug/MyLogTest

# Run ThreadPool tests
./cmake-build-debug/ThreadPoolTest

# Run FileProcess tests
./cmake-build-debug/FileTest

# Run JSON tests
./cmake-build-debug/JsonTest

# Run the web server
./cmake-build-debug/WebServer
```

### Rebuilding a Single Target
```bash
cd cmake-build-debug && make WebServer      # rebuild only WebServer
cd cmake-build-debug && make MyLogTest      # rebuild only MyLogTest
```

### Clean Build
```bash
rm -rf cmake-build-debug/* build/*
```

## Code Style Guidelines

### File Structure
- **Headers**: `include/*.h`
- **Sources**: `src/*.cpp`
- **Tests**: `test/*.cpp`
- Use include guards: `#ifndef XXX_H` / `#define XXX_H` / `#endif`
- Standard CLion copyright header on new files:
  ```cpp
  //
  // Created by 15565 on YYYY/MM/DD.
  //
  ```

### Naming Conventions
| Element | Convention | Example |
|---------|------------|---------|
| Classes/Structs | CamelCase | `ThreadPool`, `Connection` |
| Functions | snake_case | `file_exists`, `process_http_get` |
| Variables | snake_case | `client_fd`, `read_buffer` |
| Constants | CamelCase with k prefix | Not used; use `constexpr` instead |
| Enum values | CamelCase | `HttpParseResult::Complete` |
| Private members | trailing underscore | `threads_`, `stop_`, `mutex_` |
| Global variables | descriptive, lowercase | `running_log_type`, `epoll_fd` |

### Code Formatting
- **Indentation**: 4 spaces (match existing code)
- **Braces**: Opening brace on same line for functions/classes
- **Pointer/Reference**: `Type& ref`, `Type* ptr` (space before *, not after)
- **Include order**: System headers → Third-party → Project headers
- **Line length**: Keep under 120 characters

### Types & Modern C++
- **C++ Standard**: C++11
- Use `std::string` instead of C-strings when possible
- Use `std::map` and `std::unordered_map` for collections
- Use `auto` sparingly; explicit types preferred for clarity
- Use `constexpr` for compile-time constants
- Use `enum class` instead of plain enums

### Thread Safety
- Always lock `connections_mutex` when accessing `connections` map
- Use `std::unique_lock<std::mutex>` for scoped locking
- Use `std::lock_guard<std::mutex>` for simple RAII locks
- Set `processing = true` immediately after acquiring lock to prevent concurrent processing

### Error Handling
- Use logging functions: `ok(msg, log_type)` for success, `no(msg, log_type)` for errors
- Return `-1` or `false` on failure, `0` or `true` on success
- Close file descriptors via `defer_close_fd()` to let main thread handle cleanup
- Handle `EAGAIN`/`EWOULDBLOCK` for non-blocking I/O operations

### Logging Usage
```cpp
ok("Operation succeeded", LOG_TYPE::CONSOLE);   // Print to console
no("Operation failed", LOG_TYPE::FILE);         // Write to no.log
ok("Debug info", LOG_TYPE::ALL);                 // Both console and file
```

### HTTP Response Building
Use chained `append()` for response strings:
```cpp
response.append("HTTP/1.1 200 OK\r\n")
        .append("Content-Type: text/html\r\n")
        .append("Content-Length: " + std::to_string(size) + "\r\n")
        .append("Connection: close\r\n")
        .append("\r\n")
        .append(body);
```

### Import Organization
1. C++ Standard Library headers (`<vector>`, `<string>`, etc.)
2. System headers (`<sys/socket.h>`, `<netinet/in.h>`, etc.)
3. Third-party libraries (`<nlohmann/json.hpp>`, OpenSSL)
4. Project headers (`"MyLog.h"`, `"ThreadPool.h"`)

### ThreadPool Usage
```cpp
ThreadPool pool;                    // Default: hardware_concurrency threads
pool.submit([]() {
    // task lambda
});
pool.stop();                        // Graceful shutdown
```

### Common Patterns

**Checking map entries**:
```cpp
auto it = request_map.find("Method");
if (it == request_map.end()) {
    no("Key not found", running_log_type);
    return -1;
}
```

**Non-blocking recv loop**:
```cpp
while (!connection.request_completed) {
    ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);
    if (bytes > 0) { /* process */ }
    else if (bytes == 0) { /* client closed */ }
    else if (errno == EAGAIN || errno == EWOULDBLOCK) { break; }
    else { /* error */ }
}
```

## Dependencies
- **OpenSSL**: SSL/TLS support (`OpenSSL::SSL`, `OpenSSL::Crypto`)
- **nlohmann_json**: JSON parsing (`nlohmann_json::nlohmann_json`)
- **pthread**: Threading (linked automatically on Linux)

## Configuration
- Default server port: `8888`
- Default epoll max events: `1024`
- Max header size: `8KB`
- Max body size: `4MB`
