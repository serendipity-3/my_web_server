# CMake 可见性：PRIVATE / PUBLIC / INTERFACE 深度解析

## 目录

1. [核心问题：依赖可见性传递](#1-核心问题依赖可见性传递)
2. [三种可见性定义](#2-三种可见性定义)
3. [逻辑必然性分析](#3-逻辑必然性分析)
4. [实际场景推演](#4-实际场景推演)
5. [软件工程原理](#5-软件工程原理)
6. [本项目应用](#6-本项目应用)
7. [速查表与常见错误](#7-速查表与常见错误)

---

## 1. 核心问题：依赖可见性传递

### 1.1 一个具体的困境

假设你在构建一个 HTTP 库：

```
libhttp（库）
├─ include/http/HttpLib.h
├─ src/HttpLib.cpp
└─ 用了 OpenSSL
```

```cpp
// HttpLib.h
#pragma once
#include <openssl/ssl.h>  // ← 问题所在

class HttpLib {
public:
    SSL_CTX* get_ctx();   // ← 返回 OpenSSL 类型
    void send(const std::string& data);
private:
    SSL_CTX* ctx_;
};
```

现在有人用你的库：

```
my_app（可执行文件）
├─ src/main.cpp
└─ 链接 libhttp
```

```cpp
// main.cpp
#include "http/HttpLib.h"  // ← 这会间接引入 <openssl/ssl.h>

int main() {
    HttpLib lib;
    SSL_CTX* ctx = lib.get_ctx();  // ← 需要知道 SSL_CTX 是什么
}
```

**关键问题**：编译 `my_app` 时，编译器需要知道 `SSL_CTX` 是什么。这意味着 `my_app` 必须能找到 OpenSSL 的头文件。

但如果你把 OpenSSL 设为 PRIVATE：
```cmake
target_link_libraries(libhttp PRIVATE OpenSSL::SSL)
```

`my_app` 编译时会报错：`'openssl/ssl.h' file not found`。

### 1.2 依赖传递的本质

CMake 的可见性控制解决的是：**当 A 依赖 B，B 依赖 C 时，A 是否需要知道 C？**

```
my_app → libhttp → OpenSSL
  ↑         ↑         ↑
 A          B         C
```

| 情况 | A 需要 C 吗？ | 用什么可见性 |
|------|--------------|-------------|
| C 只在 B 的 .cpp 里用 | ❌ 不需要 | PRIVATE |
| C 在 B 的 .h 里暴露 | ✅ 需要 | PUBLIC |
| B 是 header-only 库 | ✅ 需要 | INTERFACE |

---

## 2. 三种可见性定义

### 2.1 PRIVATE — "私有财产，谢绝参观"

**定义**：只有当前目标（target）使用，不传递给依赖它的目标。

```cmake
target_link_libraries(libhttp PRIVATE OpenSSL::SSL)
target_include_directories(libhttp PRIVATE src/)         # 内部头文件
target_compile_definitions(libhttp PRIVATE DEBUG_MODE=1) # 内部调试宏
```

**语义**：这是我的内部实现细节，外人不需要知道。

### 2.2 PUBLIC — "公共接口，人人可见"

**定义**：当前目标使用，**同时**传递给依赖它的目标。

```cmake
target_link_libraries(libhttp PUBLIC OpenSSL::SSL)
target_include_directories(libhttp PUBLIC include/)      # 公共头文件路径
target_compile_definitions(libhttp PUBLIC HTTP_V2=1)     # 公共功能开关
```

**语义**：这是我的公共接口的一部分，用我的人也必须能看见它。

### 2.3 INTERFACE — "只给外人用"

**定义**：当前目标**不使用**，只传递给依赖它的目标。

```cmake
target_link_libraries(header_only_lib INTERFACE some_dep)
target_include_directories(header_only_lib INTERFACE include/)
target_compile_definitions(header_only_lib INTERFACE USE_FEATURE=1)
```

**语义**：我自己不用这些（因为我没有 .cpp），但用我的人需要。

---

## 3. 逻辑必然性分析

### 3.1 为什么需要三种而不是一种？

假设只有 PRIVATE：

```cmake
target_link_libraries(libhttp PRIVATE OpenSSL::SSL)
```

```cmake
target_link_libraries(my_app PRIVATE libhttp)
```

编译 `my_app` 时，`#include "http/HttpLib.h"` 会引入 `<openssl/ssl.h>`，但 CMake 没有把 OpenSSL 的包含路径传给 `my_app`。

**结果**：编译错误 `'openssl/ssl.h' file not found`。

**手工解决**（丑陋）：
```cmake
target_link_libraries(my_app PRIVATE libhttp OpenSSL::SSL OpenSSL::Crypto)
# 必须手动记住 libhttp 的所有公共依赖
```

**用 PUBLIC 解决**（优雅）：
```cmake
target_link_libraries(libhttp PUBLIC OpenSSL::SSL)
target_link_libraries(my_app PRIVATE libhttp)
# CMake 自动传递 OpenSSL
```

### 3.2 为什么需要 PRIVATE 而不是全部 PUBLIC？

假设全部用 PUBLIC：

```cmake
target_link_libraries(libhttp PUBLIC OpenSSL::SSL)
target_link_libraries(libhttp PUBLIC nlohmann_json)  # 只在实现里用了 JSON
```

```cmake
target_link_libraries(my_app PRIVATE libhttp)
# my_app 被迫知道 nlohmann_json，即使它完全不用
```

**问题**：
1. `my_app` 的链接命令里多了不需要的库
2. 如果 `libhttp` 换掉 JSON 库，`my_app` 也要改
3. 违反**最小知识原则**

### 3.3 为什么需要 INTERFACE？

场景：header-only 库

```cpp
// mylib/include/mylib/utils.h
#pragma once

inline void helper() {
    // 实现全在头文件里
}
```

这个库：
- 没有 `.cpp` 文件
- 自己不编译
- 只有头文件

```cmake
add_library(mylib INTERFACE)  # 注意：没有 STATIC/SHARED
target_include_directories(mylib INTERFACE include/)
```

如果用 PRIVATE：
```cmake
target_include_directories(mylib PRIVATE include/)
```

`mylib` 自己不编译，PRIVATE 的 include 路径对它无意义。`my_app` 链接 `mylib` 时找不到头文件。

**INTERFACE 的逻辑**：这些设置不是给我自己的（我不编译），是给用我的人的。

---

## 4. 实际场景推演

### 场景 1：库的内部实现用 OpenSSL

```cmake
add_library(libhttp src/HttpLib.cpp)
target_include_directories(libhttp PUBLIC include/)           # 公共头文件
target_link_libraries(libhttp PRIVATE OpenSSL::SSL)           # 实现内部用
```

```cpp
// include/http/HttpLib.h
#pragma once
#include <string>

class HttpLib {
public:
    void send(const std::string& data);  // 没暴露 OpenSSL 类型
};
```

```cpp
// src/HttpLib.cpp
#include "http/HttpLib.h"
#include <openssl/ssl.h>  // 内部使用

void HttpLib::send(const std::string& data) {
    SSL_write(ssl_, data.c_str(), data.size());
}
```

**依赖链**：
```
my_app → libhttp (PUBLIC include, PRIVATE OpenSSL)
           ↓
         OpenSSL（对 my_app 不可见）
```

**my_app 链接时**：
```cmake
target_link_libraries(my_app PRIVATE libhttp)
# my_app 只需要 libhttp，不需要 OpenSSL
# 因为 HttpLib.h 里没有 OpenSSL 的类型
```

### 场景 2：库的公共接口暴露 OpenSSL

```cmake
add_library(libhttp src/HttpLib.cpp)
target_include_directories(libhttp PUBLIC include/)
target_link_libraries(libhttp PUBLIC OpenSSL::SSL)            # 公共接口用了
```

```cpp
// include/http/HttpLib.h
#pragma once
#include <openssl/ssl.h>  // ← 暴露了！

class HttpLib {
public:
    SSL_CTX* get_ctx();           // 返回 OpenSSL 类型
    void send(SSL* ssl, const std::string& data);  // 参数有 OpenSSL 类型
};
```

**依赖链**：
```
my_app → libhttp (PUBLIC include, PUBLIC OpenSSL)
           ↓
         OpenSSL（对 my_app 可见）
```

**my_app 链接时**：
```cmake
target_link_libraries(my_app PRIVATE libhttp)
# CMake 自动传递 OpenSSL 给 my_app
# 因为 libhttp PUBLIC OpenSSL
```

**验证**：`my_app` 可以直接用 OpenSSL 类型：
```cpp
#include "http/HttpLib.h"
SSL_CTX* ctx = lib.get_ctx();  // ✅ 编译通过
```

### 场景 3：Header-only 库

```cmake
add_library(utils INTERFACE)  # 注意：INTERFACE 库
target_include_directories(utils INTERFACE include/)
target_link_libraries(utils INTERFACE fmt::fmt)  # 依赖 fmt 库
```

```cpp
// include/utils/string.h
#pragma once
#include <fmt/format.h>  // 依赖 fmt

inline std::string format_name(const std::string& name) {
    return fmt::format("Hello, {}!", name);
}
```

**依赖链**：
```
my_app → utils (INTERFACE include, INTERFACE fmt)
           ↓
         fmt（对 my_app 可见）
```

**my_app 链接时**：
```cmake
target_link_libraries(my_app PRIVATE utils)
# CMake 自动传递 fmt 给 my_app
```

### 场景 4：混合使用

```cmake
add_library(libhttp src/HttpLib.cpp)
target_include_directories(libhttp
    PUBLIC include/              # 公共头文件
    PRIVATE src/                 # 内部头文件
)
target_link_libraries(libhttp
    PUBLIC OpenSSL::SSL          # 公共接口依赖
    PRIVATE nlohmann_json        # 内部实现依赖
)
```

```
my_app → libhttp
           ├─ PUBLIC: OpenSSL（传递给 my_app）
           └─ PRIVATE: nlohmann_json（不传递）
```

---

## 5. 软件工程原理

### 5.1 封装原则（Encapsulation）

**定义**：隐藏内部实现，只暴露必要的接口。

```
PRIVATE = 隐藏的实现细节
PUBLIC  = 暴露的公共接口
```

**类比**：
- 汽车的**方向盘、油门、刹车**是 PUBLIC 接口
- 汽车的**发动机、变速箱**是 PRIVATE 实现

你开车不需要知道发动机怎么工作（PRIVATE），但必须知道方向盘在哪（PUBLIC）。

### 5.2 最小知识原则（Least Knowledge）

**定义**：一个模块应该尽可能少地了解其他模块的内部。

```cmake
# ❌ 违反：my_app 知道了不需要知道的 nlohmann_json
target_link_libraries(libhttp PUBLIC nlohmann_json)

# ✅ 遵循：nlohmann_json 对 my_app 不可见
target_link_libraries(libhttp PRIVATE nlohmann_json)
```

### 5.3 接口隔离原则（Interface Segregation）

**定义**：客户端不应该被迫依赖它不使用的接口。

```cmake
# ❌ 违反：my_app 被迫知道所有依赖
target_link_libraries(libhttp PUBLIC OpenSSL PUBLIC nlohmann_json PUBLIC zlib)

# ✅ 遵循：my_app 只需要知道必要的依赖
target_link_libraries(libhttp PUBLIC OpenSSL PRIVATE nlohmann_json PRIVATE zlib)
```

### 5.4 依赖倒置原则（Dependency Inversion）

**定义**：高层模块不应该依赖低层模块，两者都应该依赖抽象。

```cmake
# libhttp 用 OpenSSL（具体实现）
target_link_libraries(libhttp PRIVATE OpenSSL::SSL)

# 如果未来换成 BoringSSL，只需改一行：
target_link_libraries(libhttp PRIVATE BoringSSL::SSL)

# my_app 完全不受影响（因为 PRIVATE）
```

---

## 6. 本项目应用

### 6.1 当前配置

```cmake
# WebServer 是可执行文件，没有下游
target_link_libraries(WebServer PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(WebServer PRIVATE nlohmann_json::nlohmann_json)
```

**为什么全用 PRIVATE？**

`WebServer` 是最终的可执行文件，没有"用它的人"。不存在传递依赖的问题。

### 6.2 如果重构为库

假设你把 HTTP 处理逻辑抽成库：

```cmake
# 库：HTTP 处理
add_library(httplib
    src/HttpProcess.cpp
    src/FileProcess.cpp
)
target_include_directories(httplib PUBLIC include/)
target_link_libraries(httplib
    PUBLIC OpenSSL::SSL OpenSSL::Crypto     # HttpProcess.h 里可能暴露了 SSL 类型
    PRIVATE nlohmann_json                    # JSON 只在实现里用
    PRIVATE pthread                          # 线程只在实现里用
)

# 可执行文件
add_executable(WebServer src/main.cpp)
target_link_libraries(WebServer PRIVATE httplib)
# CMake 自动传递：OpenSSL
# CMake 不传递：nlohmann_json, pthread
```

### 6.3 测试目标

```cmake
# HttpParseTest 需要所有依赖（因为 TestMocks.cpp 提供了全局变量）
target_link_libraries(HttpParseTest PRIVATE
    GTest::gtest GTest::gtest_main
    OpenSSL::SSL OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    pthread
)
```

测试用 PRIVATE，因为它是可执行文件。

---

## 7. 速查表与常见错误

### 7.1 速查表

| 场景 | 用什么 | 理由 |
|------|--------|------|
| 可执行文件的依赖 | PRIVATE | 没有下游 |
| 库的内部实现依赖 | PRIVATE | 封装 |
| 库的公共头文件依赖 | PUBLIC | 传递给下游 |
| Header-only 库的依赖 | INTERFACE | 自己不编译 |
| 库的公共头文件路径 | PUBLIC | 下游需要 include |
| 库的私有头文件路径 | PRIVATE | 下游不需要 |

### 7.2 决策流程图

```
这个依赖在库的头文件 (.h) 里暴露了吗？
├─ 是 → 用 PUBLIC
└─ 否 → 用 PRIVATE

这个目标是 header-only 库吗？
├─ 是 → 用 INTERFACE
└─ 否 → 参考上面
```

### 7.3 常见错误

#### 错误 1：头文件暴露了依赖却用 PRIVATE

```cmake
# libhttp 的头文件有 #include <openssl/ssl.h>
target_link_libraries(libhttp PRIVATE OpenSSL::SSL)  # ❌ 错误
```

**症状**：
```
my_app 编译时：'openssl/ssl.h' file not found
```

**修复**：
```cmake
target_link_libraries(libhttp PUBLIC OpenSSL::SSL)   # ✅ 正确
```

#### 错误 2：实现内部用了依赖却用 PUBLIC

```cmake
# libhttp 只在 .cpp 里用了 nlohmann_json
target_link_libraries(libhttp PUBLIC nlohmann_json)  # ❌ 错误
```

**症状**：
- my_app 链接命令多了不需要的库
- libhttp 换 JSON 库时，my_app 也要改

**修复**：
```cmake
target_link_libraries(libhttp PRIVATE nlohmann_json) # ✅ 正确
```

#### 错误 3：可执行文件用 PUBLIC

```cmake
target_link_libraries(WebServer PUBLIC OpenSSL::SSL)  # ⚠️ 不推荐
```

**虽然能工作**，但语义不对。可执行文件没有下游，PUBLIC 和 PRIVATE 效果相同。用 PRIVATE 更清晰。

#### 错误 4：INTERFACE 库用 PRIVATE

```cmake
add_library(mylib INTERFACE)
target_include_directories(mylib PRIVATE include/)  # ❌ 无效
```

INTERFACE 库只能用 INTERFACE 关键字。PRIVATE 和 PUBLIC 对 INTERFACE 库无效。

### 7.4 调试技巧

查看目标的实际依赖：

```bash
cd cmake-build-debug
cmake --build . --target WebServer --verbose  # 查看实际编译命令
```

查看依赖图：

```bash
cmake --graphviz=deps.dot ..
dot -Tpng deps.dot -o deps.png  # 需要 graphviz
```

---

## 附录：完整示例

### 项目结构

```
myproject/
├── CMakeLists.txt
├── libhttp/
│   ├── CMakeLists.txt
│   ├── include/http/HttpLib.h
│   └── src/HttpLib.cpp
├── libutils/
│   ├── CMakeLists.txt
│   └── include/utils/Helper.h
└── app/
    ├── CMakeLists.txt
    └── src/main.cpp
```

### libhttp/CMakeLists.txt

```cmake
add_library(http src/HttpLib.cpp)
target_include_directories(http PUBLIC include/)
target_link_libraries(http
    PUBLIC OpenSSL::SSL OpenSSL::Crypto  # 头文件暴露了 SSL 类型
    PRIVATE nlohmann_json                 # 只在实现里用 JSON
)
```

### libutils/CMakeLists.txt（Header-only）

```cmake
add_library(utils INTERFACE)
target_include_directories(utils INTERFACE include/)
target_link_libraries(utils INTERFACE fmt::fmt)  # 头文件用了 fmt
```

### app/CMakeLists.txt

```cmake
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE http utils)
# CMake 自动传递：
#   - OpenSSL::SSL, OpenSSL::Crypto（来自 http PUBLIC）
#   - fmt::fmt（来自 utils INTERFACE）
# 不传递：
#   - nlohmann_json（来自 http PRIVATE）
```

---

*文档版本：v1.0*
*最后更新：2026-04-08*
*适用项目：my-web-server*
