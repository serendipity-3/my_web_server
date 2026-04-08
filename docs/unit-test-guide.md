# 单元测试教学文档：从理论到实践

## 目录

1. [为什么需要测试](#1-为什么需要测试)
2. [测试的逻辑必然性](#2-测试的逻辑必然性)
3. [代码的有机整体联系](#3-代码的有机整体联系)
4. [测试用例设计：边界值与等价类](#4-测试用例设计边界值与等价类)
5. [Mock 与依赖解耦](#5-mock-与依赖解耦)
6. [C++ 测试框架对比](#6-c-测试框架对比)
7. [本项目测试架构分析](#7-本项目测试架构分析)
8. [软件工程实践总结](#8-软件工程实践总结)

---

## 1. 为什么需要测试

### 软件工程的根本矛盾

软件工程的核心矛盾是：**人类的有限理性 vs 系统的无限复杂性**。

一个 HTTP 服务器有无穷的输入组合：
- 请求方法：7 种 × 路径无限 × Header 无限组合 × Body 无限变体
- 网络状态：正常、延迟、丢包、分片、乱序
- 并发场景：1 连接、100 连接、10000 连接同时到达

人类无法在脑中穷举所有情况。测试是**用有限的确定性检查来逼近无限的不确定性**。

### 测试金字塔

```
        /\
       /  \        E2E 测试（慢、贵、少）
      /    \       集成测试（中等）
     /      \      单元测试（快、便宜、多）
    /________\
```

本项目的 37 个测试属于**单元测试**：快速（<1秒）、独立（不依赖网络）、可重复。

---

## 2. 测试的逻辑必然性

### 2.1 状态机验证

`check_http_request_status()` 本质上是一个**状态机**，输入是字符串，输出是枚举：

```
输入字符串 → [解析函数] → {Incomplete, Complete, HeaderTooLarge, BodyTooLarge, MalformedHeader}
```

测试的逻辑必然性在于：**每个输出状态必须有对应的输入用例**。

| 状态 | 必然存在的测试用例 | 理由 |
|------|-------------------|------|
| `Incomplete` | 缺少 `\r\n\r\n` 的请求 | 这是最常见的网络分片场景 |
| `Incomplete` | Content-Length 声明 100 字节但只有 10 字节 body | POST 请求 body 不完整 |
| `Complete` | 完整的 GET 请求（无 body） | 最基本的合法请求 |
| `Complete` | 完整的 POST 请求（有 body） | 验证 body 解析逻辑 |
| `HeaderTooLarge` | Header 超过 8KB | 防止内存耗尽攻击 |
| `BodyTooLarge` | Content-Length 声明超过 4MB | 防止磁盘/内存耗尽 |
| `MalformedHeader` | Content-Length: abc | 非法数字格式 |

**如果你不写这些测试，就等于假设这些分支永远不会执行。** 但 HTTP 请求来自不可控的网络，不写测试就是在赌运气。

### 2.2 安全边界验证

`MAX_HEADER_SIZE = 8KB`、`MAX_BODY_SIZE = 4MB` 是**安全边界**。如果不测试边界：

- 攻击者发送 10GB 的 Content-Length → 内存分配崩溃
- 攻击者发送 100MB 的 Header → `std::string` 内存耗尽

**边界测试不是"可选的额外工作"，而是安全防线的必要组成部分。**

### 2.3 回归保护

假设你现在修了一个 bug：`request_str_to_map()` 在 header 值有前导空格时解析错误。

```
"Host:   localhost"  →  应该解析为 "localhost"（去掉空格）
```

如果你不写测试，三个月后有人重构这段代码，可能会引入同样的 bug。

**测试是活的文档，是代码库的"免疫系统"。**

---

## 3. 代码的有机整体联系

### 3.1 模块依赖关系

```
┌─────────────────────────────────────────────────────────┐
│                      main.cpp                           │
│  epoll 事件循环 → accept → 调度到线程池                   │
│                     ↓                                   │
├─────────────────────────────────────────────────────────┤
│                   HttpProcess.cpp                       │
│  handle_client_read()                                   │
│    ├── recv() 收数据                                    │
│    ├── check_http_request_status() ←── 我们测的函数      │
│    ├── request_str_to_map() ←─────── 我们测的函数        │
│    └── process_http()                                   │
│         ├── process_http_get()                          │
│         ├── process_http_post()                         │
│         └── ...其他方法                                  │
│                     ↓                                   │
├─────────────────────────────────────────────────────────┤
│                   FileProcess.cpp                       │
│  file_exists() ←──────────────────── 我们测的函数        │
│  get_file_size() ←────────────────── 我们测的函数        │
│  get_file_content() ←─────────────── 我们测的函数        │
│  generate_filename_by_time() ←────── 我们测的函数        │
│                     ↓                                   │
├─────────────────────────────────────────────────────────┤
│                   ThreadPool.cpp                        │
│  submit() ←───────────────────────── 我们测的函数        │
│  stop() ←─────────────────────────── 我们测的函数        │
│  ~ThreadPool() ←──────────────────── 我们测的函数        │
└─────────────────────────────────────────────────────────┘
```

### 3.2 数据流追踪

一个完整的 HTTP 请求处理流程：

```
客户端发送 "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n"
    ↓
epoll 检测到 EPOLLIN 事件
    ↓
线程池分配线程执行 handle_client_read(fd)
    ↓
recv() 读取数据到 read_buffer
    ↓
check_http_request_status(buffer) → Complete  ← 测试点 1
    ↓
request_str_to_map(buffer, map)                ← 测试点 2
    ↓  map["Method"] = "GET"
    ↓  map["Path"] = "/index.html"
process_http_get(map, connection)
    ↓
file_exists("./index.html") → true             ← 测试点 3
    ↓
get_file_size("./index.html") → 1024           ← 测试点 4
    ↓
get_file_content("./index.html") → "<html>..." ← 测试点 5
    ↓
构建响应 + send_all() 发送
```

**每个测试点都是数据流中的一个验证环节。** 漏掉任何一个，整个链路的信任度都会下降。

### 3.3 为什么先测解析，后测业务？

测试顺序遵循**依赖方向**：

```
底层（无依赖）          中层（有依赖）           上层（复杂依赖）
FileProcess            HttpProcess             main.cpp
ThreadPool             (解析逻辑)              (epoll 循环)
```

- `FileProcess` 和 `ThreadPool` 是独立的底层模块 → 最先测
- `HttpProcess` 的解析函数不依赖网络 → 次要测
- `main.cpp` 的 epoll 循环需要真实网络 → 暂不测（集成测试层面）

---

## 4. 测试用例设计：边界值与等价类

### 4.1 等价类划分（Equivalence Partitioning）

将输入划分为"等价类"——同一类中的输入，程序行为相同。

**`check_http_request_status()` 的等价类：**

| 等价类 | 代表值 | 期望结果 |
|--------|--------|----------|
| 合法 GET（无 body） | `GET / HTTP/1.1\r\n...\r\n\r\n` | Complete |
| 合法 POST（有 body） | `POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello` | Complete |
| Header 未结束 | `GET / HTTP/1.1\r\nHost: x\r\n`（缺 `\r\n\r\n`） | Incomplete |
| Body 不完整 | Content-Length=100 但只有 10 字节 | Incomplete |
| Header 超长 | >8KB | HeaderTooLarge |
| Body 超长 | Content-Length >4MB | BodyTooLarge |
| Content-Length 非数字 | `Content-Length: abc` | MalformedHeader |

### 4.2 边界值分析（Boundary Value Analysis）

| 边界 | 测试用例 | 理由 |
|------|----------|------|
| Header = 8KB - 1 | 刚好不超限 | 验证边界正确 |
| Header = 8KB + 1 | 刚好超限 | 验证边界正确 |
| Body = 4MB - 1 | 刚好不超限 | 验证边界正确 |
| Body = 4MB + 1 | 刚好超限 | 验证边界正确 |
| Content-Length = 0 | 空 body POST | 合法请求 |
| 空字符串 `""` | 无数据 | Incomplete |

### 4.3 正交数组（Orthogonal Array）

对于多参数组合，正交数组用最少的用例覆盖最多的情况。

本项目中，`request_str_to_map()` 的参数组合：
- 方法：GET, POST, DELETE, OPTIONS（4 种）
- 路径：/, /index.html, /api/data, *（4 种）
- Header 数量：0, 1, 5（3 种）
- 是否有 body：是, 否（2 种）

完全组合 = 4 × 4 × 3 × 2 = 96 种。但我们用**正交数组**只需 ~16 种就能覆盖大部分组合。

---

## 5. Mock 与依赖解耦

### 5.1 什么是 Mock？

Mock 是**替身**：用假的实现替代真实的依赖，让测试可以独立运行。

### 5.2 本项目的 Mock 实现

`TestMocks.cpp` 的内容：

```cpp
int epoll_fd = -1;                                    // 假的 epoll fd
std::unordered_map<int, Connection> connections;      // 假的连接表
std::mutex connections_mutex;                         // 假的互斥锁
std::mutex rearm_queue_mutex;
std::queue<int> close_queue;
std::queue<int> rearm_queue;
std::mutex close_queue_mutex;

void defer_close_fd(int fd) {}                        // 空实现
void defer_rearm_fd(int fd) {}                        // 空实现
```

**为什么需要 Mock？**

- `HttpProcess.h` 声明了 `extern std::unordered_map<int, Connection> connections`
- 如果不提供定义，链接器报 `undefined reference`
- 但真实定义在 `main.cpp`，链接 `main.cpp` 会引入 `main()` 函数和 epoll 循环
- Mock 提供**最小化**的假实现，让测试只关注 HTTP 解析逻辑

### 5.3 Mock 的设计原则

**原则 1：最小化实现** — 只提供测试必需的符号，不多不少。

**原则 2：无副作用** — Mock 函数体为空，不会触发真实操作（如关闭 fd）。

**原则 3：可预测** — 全局变量用默认值初始化，测试结果确定。

### 5.4 Mock vs Stub vs Fake

| 概念 | 定义 | 本项目示例 |
|------|------|-----------|
| **Stub** | 返回固定值的替身 | 无 |
| **Mock** | 验证调用行为的替身 | 无（本项目用的是 Stub） |
| **Fake** | 简化实现的替身 | 无 |

严格来说，`TestMocks.cpp` 提供的是 **Stub**（空实现），不是真正的 Mock。但在实际项目中，这两个词经常混用。

---

## 6. C++ 测试框架对比

### 6.1 主流框架一览

| 框架 | 语法 | 编译依赖 | 学习曲线 | 适用场景 |
|------|------|----------|----------|----------|
| **Google Test** | `TEST_F(Fixture, Name)` | 需编译安装 | 中等 | 企业级项目 |
| **Catch2** | `TEST_CASE("name")` | Header-only | 低 | 快速原型、小项目 |
| **doctest** | `TEST_CASE("name")` | Header-only | 低 | 极致轻量 |
| **Boost.Test** | `BOOST_AUTO_TEST_CASE` | 需 Boost 库 | 高 | 已有 Boost 的项目 |
| **CppUnit** | `CPPUNIT_TEST_SUITE` | 需编译安装 | 高 | 遗留项目 |
| **lest** | `TEST("name")` | Header-only | 低 | C++11/14 小项目 |

### 6.2 语法对比

**同一个测试用例在不同框架中的写法：**

#### Google Test（本项目使用）
```cpp
class FileExistsTest : public ::testing::Test {
protected:
    std::string test_file = "/tmp/test.txt";
    void SetUp() override {
        std::ofstream ofs(test_file);
        ofs << "test";
    }
    void TearDown() override {
        std::remove(test_file.c_str());
    }
};

TEST_F(FileExistsTest, ExistingFile) {
    EXPECT_TRUE(file_exists(test_file));
}
```

#### Catch2
```cpp
TEST_CASE("file_exists returns true for existing file") {
    std::string test_file = "/tmp/test.txt";
    std::ofstream ofs(test_file);
    ofs << "test";
    
    REQUIRE(file_exists(test_file));
    
    std::remove(test_file.c_str());
}
```

#### doctest
```cpp
TEST_CASE("file_exists returns true for existing file") {
    std::string test_file = "/tmp/test.txt";
    std::ofstream ofs(test_file);
    ofs << "test";
    
    CHECK(file_exists(test_file));
    
    std::remove(test_file.c_str());
}
```

### 6.3 断言对比

| 功能 | Google Test | Catch2 | doctest |
|------|-------------|--------|---------|
| 相等 | `EXPECT_EQ(a, b)` | `REQUIRE(a == b)` | `CHECK(a == b)` |
| 不等 | `EXPECT_NE(a, b)` | `REQUIRE(a != b)` | `CHECK(a != b)` |
| 小于 | `EXPECT_LT(a, b)` | `REQUIRE(a < b)` | `CHECK(a < b)` |
| 真值 | `EXPECT_TRUE(x)` | `REQUIRE(x)` | `CHECK(x)` |
| 异常 | `EXPECT_THROW(expr, type)` | `REQUIRE_THROWS_AS(expr, type)` | `CHECK_THROWS_AS(expr, type)` |
| 字符串包含 | 无内置 | `REQUIRE_THAT(s, Contains("sub"))` | `CHECK(s.find("sub") != npos)` |

### 6.4 选择建议

| 你的情况 | 推荐框架 | 理由 |
|----------|----------|------|
| 企业项目、多人协作 | Google Test | 生态成熟、文档丰富、CI/CD 集成好 |
| 个人项目、快速迭代 | Catch2 | Header-only、语法简洁、无需安装 |
| 极致轻量、嵌入式 | doctest | 编译快、体积小 |
| 已有 Boost 依赖 | Boost.Test | 无需额外依赖 |
| 需要 BDD 风格 | Catch2 | 支持 SECTION 嵌套 |

### 6.5 Google Test 深度解析

本项目选择 Google Test 的理由：

1. **Fixture 机制**（`TEST_F`）：每个测试类可以有 `SetUp()` 和 `TearDown()`，自动管理资源生命周期。

2. **类型安全的断言**：`EXPECT_EQ` 在编译期检查类型，避免 `==` 被误写成 `=`。

3. **死亡测试**：`EXPECT_DEATH` 可以测试 `assert()` 和 `exit()`。

4. **参数化测试**：同一测试用例用不同参数运行。

5. **vcpkg 集成**：依赖管理无缝衔接。

---

## 7. 本项目测试架构分析

### 7.1 测试覆盖率

| 模块 | 函数数 | 已测试 | 覆盖率 | 状态 |
|------|--------|--------|--------|------|
| HttpProcess（解析） | 2 | 2 | 100% | ✅ |
| FileProcess | 4 | 4 | 100% | ✅ |
| ThreadPool | 3 | 3 | 100% | ✅ |
| HttpProcess（业务） | 8 | 0 | 0% | ⚠️ 待做 |
| main.cpp | N/A | 0 | N/A | 需集成测试 |

### 7.2 测试分层

```
┌─────────────────────────────────────────┐
│           集成测试（未实现）              │
│  真实网络连接 + epoll + 线程池           │
│  需要：启动服务器 → 发请求 → 验证响应    │
├─────────────────────────────────────────┤
│           单元测试（已实现）              │
│  HttpParseTest   (18 cases)             │
│  FileProcessTest (11 cases)             │
│  ThreadPoolGTest (8 cases)              │
├─────────────────────────────────────────┤
│           Mock 层（已实现）              │
│  TestMocks.cpp 提供 stub 实现           │
└─────────────────────────────────────────┘
```

### 7.3 测试的组织原则

**原则 1：一个测试类对应一个被测功能组**

```cpp
class HttpParseStatusTest : public ::testing::Test { ... };
// 测试 check_http_request_status() 的各种场景

class RequestStrToMapTest : public ::testing::Test { ... };
// 测试 request_str_to_map() 的各种场景
```

**原则 2：Fixture 管理资源生命周期**

```cpp
class FileExistsTest : public ::testing::Test {
protected:
    std::string test_file = "/tmp/gtest_file_exists_test.txt";

    void SetUp() override {
        std::ofstream ofs(test_file);
        ofs << "test content";
    }

    void TearDown() override {
        std::remove(test_file.c_str());  // 清理临时文件
    }
};
```

**原则 3：测试命名遵循 `Given-When-Then`**

```cpp
// Given: 一个完整的 GET 请求
TEST_F(HttpParseStatusTest, CompleteGetRequest) {
    std::string request = "GET /index.html HTTP/1.1\r\n...\r\n\r\n";

    // When: 调用解析函数
    // Then: 结果应该是 Complete
    EXPECT_EQ(check_http_request_status(request), HttpParseResult::Complete);
}
```

---

## 8. 软件工程实践总结

### 8.1 测试驱动开发（TDD）

TDD 的三定律：
1. **先写失败的测试** → 确保测试有意义
2. **只写刚好让测试通过的代码** → 避免过度工程
3. **重构时保持测试通过** → 安全网

本项目的测试是在**已有代码**基础上补充的，属于"后补测试"。这是现实中的常见情况，但不如 TDD 理想。

### 8.2 测试的成本与收益

| 投入 | 回报 |
|------|------|
| 编写测试用例的时间 | 回归 bug 减少 80%+ |
| 维护测试代码的时间 | 重构信心提升 |
| CI/CD 集成成本 | 代码审查效率提升 |

**经验法则**：测试覆盖率 70-80% 是甜蜜点。100% 覆盖率的边际成本递减。

### 8.3 测试的反模式

| 反模式 | 表现 | 本项目是否避免 |
|--------|------|----------------|
| 测试实现细节 | 测试私有方法、内部状态 | ✅ 只测公开接口 |
| 脆弱测试 | 改一行代码坏 10 个测试 | ✅ 测试行为而非实现 |
| 慢速测试 | 测试跑 30 分钟 | ✅ 所有测试 <1 秒 |
| 魔法值 | 测试中硬编码不明数字 | ✅ 用命名常量 |
| 测试间耦合 | 测试 B 依赖测试 A 的副作用 | ✅ 每个测试独立 |

### 8.4 持续集成（CI）最佳实践

```
开发者提交代码
    ↓
CI 服务器触发构建
    ↓
编译（cmake + make）
    ↓
运行单元测试（ctest）
    ↓
生成测试报告
    ↓
通过 → 合并；失败 → 阻止合并 + 通知开发者
```

本项目的 CI 命令：
```bash
cd cmake-build-debug
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake
make -j$(nproc)
ctest --output-on-failure
```

### 8.5 从本项目学到的教训

1. **全局状态是测试的敌人** — `extern` 全局变量迫使我们写 Mock
2. **纯函数最好测** — `check_http_request_status()` 是纯函数，测试简单
3. **副作用函数需要隔离** — `defer_close_fd()` 在测试中是空实现
4. **测试是文档** — 测试用例比注释更能说明函数的行为
5. **边界是 bug 的温床** — Content-Length = 0、Header = 8KB 这些边界必须测

---

## 附录：扩展阅读

### 推荐书籍

- 《代码大全》Steve McConnell — 测试章节
- 《测试驱动开发》Kent Beck — TDD 奠基之作
- 《xUnit Test Patterns》Gerard Meszaros — 测试模式大全
- 《Effective Modern C++》Scott Meyers — C++11/14 最佳实践

### 推荐资源

- [Google Test 官方文档](https://google.github.io/googletest/)
- [Catch2 官方文档](https://github.com/catchorg/Catch2)
- [doctest 官方文档](https://github.com/doctest/doctest)
- [Software Testing Anti-patterns](https://blog.codepipes.com/testing-software-testing-antipatterns.html)

---

*文档版本：v1.0*
*最后更新：2026-04-08*
*适用项目：my-web-server*
