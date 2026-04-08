# CMake 教学文档：从零理解构建系统的逻辑必然性

## 目录

1. [为什么需要构建系统](#1-为什么需要构建系统)
2. [CMake 的本质：元构建系统](#2-cmake-的本质元构建系统)
3. [CMakeLists.txt 逐行解析](#3-cmakeliststxt-逐行解析)
4. [构建系统的有机整体联系](#4-构建系统的有机整体联系)
5. [第三方库管理](#5-第三方库管理)
6. [软件工程实践](#6-软件工程实践)
7. [常见问题与最佳实践](#7-常见问题与最佳实践)

---

## 1. 为什么需要构建系统

### 1.1 手动编译的困境

假设你只有一个文件 `main.cpp`：

```bash
g++ -o server main.cpp
```

很简单。但当项目变成这样：

```
src/
├── main.cpp          # 调用 HttpProcess, ThreadPool
├── HttpProcess.cpp   # 调用 FileProcess, OpenSSL
├── FileProcess.cpp   # 调用 MyLog
├── ThreadPool.cpp    # 调用 MyLog
└── MyLog.cpp         # 无依赖
```

手动编译变成：

```bash
# 编译每个 .cpp → .o
g++ -c -I./include src/MyLog.cpp -o MyLog.o
g++ -c -I./include src/FileProcess.cpp -o FileProcess.o
g++ -c -I./include src/ThreadPool.cpp -o ThreadPool.o
g++ -c -I./include src/HttpProcess.cpp -o HttpProcess.o
g++ -c -I./include src/main.cpp -o main.o

# 链接所有 .o + 第三方库
g++ -o server main.o HttpProcess.o FileProcess.o ThreadPool.o MyLog.o \
    -lssl -lcrypto -lpthread -lnlohmann_json
```

**问题来了：**
- 每次改一个文件，要手动重新编译哪些？
- 第三方库的头文件路径在哪？
- 如果项目有 100 个文件呢？
- 如果要支持 Windows、Linux、macOS 呢？

### 1.2 构建系统解决的问题

| 问题 | 手动编译 | 构建系统 |
|------|----------|----------|
| 依赖追踪 | 人工判断 | 自动分析 |
| 增量编译 | 全量重编 | 只编修改的 |
| 跨平台 | 写多套脚本 | 一套配置 |
| 第三方库 | 手动指定路径 | 自动发现 |
| 多目标 | 复制命令 | 声明式配置 |

### 1.3 构建系统的本质

**构建系统是一个"依赖关系求解器"。**

你告诉它：
1. 有哪些源文件
2. 它们之间的依赖关系
3. 最终要生成什么

它帮你：
1. 决定编译顺序
2. 只重编变化的部分
3. 正确链接所有依赖

---

## 2. CMake 的本质：元构建系统

### 2.1 CMake 不是构建工具

这是一个关键认知：

```
CMakeLists.txt → [CMake] → Makefile/Ninja/VS Project → [make/ninja/msbuild] → 可执行文件
     ↑                              ↑                        ↑
   你写                        CMake 生成              真正的构建
```

**CMake 是"构建系统的生成器"，不是构建工具本身。**

它读取 `CMakeLists.txt`，生成对应平台的构建文件：
- Linux → Makefile 或 Ninja
- Windows → Visual Studio .sln
- macOS → Xcode project

### 2.2 为什么选择 CMake

| 构建系统 | 跨平台 | 学习曲线 | 生态 | 适用场景 |
|----------|--------|----------|------|----------|
| **CMake** | ✅ | 中等 | 最大 | 通用，事实标准 |
| Meson | ✅ | 低 | 中等 | 新项目 |
| Bazel | ✅ | 高 | 中等 | 大型单体仓库 |
| Make | ❌ | 中等 | 大 | Unix 小项目 |
| Premake | ✅ | 低 | 小 | 游戏开发 |

**CMake 是 C/C++ 生态的事实标准。** vcpkg、Conan、Google Test、OpenSSL 都提供 CMake 集成。

---

## 3. CMakeLists.txt 逐行解析

以本项目为例，逐行解释**为什么这行存在，去掉会怎样**。

### 3.1 版本声明

```cmake
cmake_minimum_required(VERSION 3.22.1)
```

**为什么需要？**

CMake 不断演进，不同版本支持不同语法。这行告诉 CMake："我的配置用了 3.22.1 的特性，如果你版本更低，请报错。"

**去掉会怎样？**
- CMake 会警告，可能用默认行为
- 如果用了新特性（如 `FetchContent`），低版本 CMake 会报语法错误

**类比**：C++ 的 `#include <version>` 或 Python 的 `pyproject.toml` 中的 `requires-python`。

### 3.2 项目声明

```cmake
project(my_web_server)
```

**为什么需要？**

1. 定义项目名称（用于生成的文件命名）
2. 设置默认语言检测（C/C++）
3. 定义一些变量：`${PROJECT_NAME}`、`${PROJECT_SOURCE_DIR}`

**去掉会怎样？**
- `${PROJECT_NAME}` 为空
- CMake 不知道项目名，生成的文件名可能不对

**隐含逻辑**：这行同时设置了 `CMAKE_PROJECT_NAME`，影响后续所有 `add_executable` 的默认行为。

### 3.3 第三方库路径

```cmake
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};$ENV{HOME}/vcpkg/installed/x64-linux")
```

**为什么需要？**

`find_package()` 需要知道去哪找库。`CMAKE_PREFIX_PATH` 是搜索路径列表。

这行把 vcpkg 的安装目录加进去，让 CMake 能找到：
- `~/vcpkg/installed/x64-linux/lib/cmake/openssl/`
- `~/vcpkg/installed/x64-linux/lib/cmake/nlohmann_json/`
- `~/vcpkg/installed/x64-linux/lib/cmake/GTest/`

**去掉会怎样？**
- `find_package(OpenSSL REQUIRED)` 报错：`Could not find a package configuration file`
- 需要手动指定 `-DOPENSSL_ROOT_DIR=...`

**软件工程视角**：这是"依赖发现"阶段。构建系统需要知道依赖在哪，才能正确链接。

### 3.4 C++ 标准

```cmake
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

**为什么需要？**

- `CMAKE_CXX_STANDARD 11`：告诉编译器用 C++11 标准
- `CMAKE_CXX_STANDARD_REQUIRED ON`：如果编译器不支持 C++11，报错（而不是降级）

**去掉会怎样？**
- 默认用编译器的默认标准（GCC 11 默认 C++17）
- 如果代码用了 C++11 特性（如 `std::thread`），在旧编译器上可能编译过
- 但如果代码用了 C++17 特性，在只支持 C++11 的编译器上会编译失败

**软件工程视角**：这是"显式契约"。告诉所有人："这个项目用 C++11，不要用更新的特性。"

### 3.5 查找依赖包

```cmake
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
```

**为什么需要？**

`find_package` 做两件事：
1. **验证依赖存在** — 找不到就报错
2. **导入目标（Target）** — 创建 `OpenSSL::SSL`、`nlohmann_json::nlohmann_json` 等

这些目标封装了：
- 头文件路径（`-I/path/to/include`）
- 库文件路径（`-L/path/to/lib`）
- 链接库名（`-lssl -lcrypto`）

**去掉会怎样？**
- 后续 `target_link_libraries(... OpenSSL::SSL)` 报错：`target not found`
- 需要手动指定所有路径和库名

**CONFIG vs MODULE 模式**：
- `CONFIG`：查找 `.cmake` 配置文件（vcpkg 安装的库提供这个）
- `MODULE`：查找 `FindXXX.cmake` 脚本（CMake 内置）

### 3.6 源文件集合

```cmake
set(SOURCES
    src/main.cpp
    src/HttpProcess.cpp
    src/FileProcess.cpp
    src/ThreadPool.cpp
    src/MyLog.cpp
)
```

**为什么需要？**

把相关源文件分组，方便复用。`SOURCES` 变量被 `add_executable(WebServer ${SOURCES})` 使用。

**去掉会怎样？**
- 需要在每个 `add_executable` 里重复写文件列表
- 改一个文件要改多处

**软件工程视角**：这是"单一来源原则（Single Source of Truth）"。源文件列表只维护一处。

### 3.7 添加可执行文件

```cmake
add_executable(WebServer ${SOURCES})
add_executable(HttpParseTest test/HttpParseTest.cpp test/TestMocks.cpp ...)
```

**为什么需要？**

告诉 CMake："我要生成这些可执行文件，用这些源文件。"

CMake 会：
1. 分析源文件依赖（`#include`）
2. 决定编译顺序
3. 生成编译和链接命令

**去掉会怎样？**
- 不生成可执行文件
- `make` 什么都不做

### 3.8 头文件路径

```cmake
target_include_directories(WebServer PRIVATE ${CMAKE_SOURCE_DIR}/include)
```

**为什么需要？**

告诉编译器去哪找 `#include "HttpProcess.h"`。等价于 `g++ -I./include`。

**PRIVATE vs PUBLIC vs INTERFACE**：

| 关键字 | 含义 | 使用场景 |
|--------|------|----------|
| `PRIVATE` | 只有当前目标用 | 可执行文件 |
| `PUBLIC` | 当前目标 + 链接它的都用 | 库的公共头文件 |
| `INTERFACE` | 只有链接它的用 | Header-only 库 |

**去掉会怎样？**
- 编译器找不到头文件：`'HttpProcess.h' file not found`

### 3.9 链接库

```cmake
target_link_libraries(WebServer PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(WebServer PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(HttpParseTest PRIVATE GTest::gtest GTest::gtest_main pthread)
```

**为什么需要？**

告诉链接器："这个可执行文件需要这些库。" 等价于 `g++ -lssl -lcrypto -lpthread`。

**为什么用 `OpenSSL::SSL` 而不是 `ssl`？**

`OpenSSL::SSL` 是 CMake 的 **Imported Target**，封装了：
- 头文件路径
- 库文件路径
- 依赖关系（`OpenSSL::SSL` 依赖 `OpenSSL::Crypto`）
- 编译选项

用 `ssl` 的话，需要手动指定所有这些。

**去掉会怎样？**
- 链接错误：`undefined reference to 'SSL_library_init'`

### 3.10 测试注册

```cmake
enable_testing()
add_test(NAME HttpParseTest COMMAND HttpParseTest)
add_test(NAME FileProcessTest COMMAND FileProcessTest)
add_test(NAME ThreadPoolGTest COMMAND ThreadPoolGTest)
```

**为什么需要？**

- `enable_testing()`：启用 CTest 支持
- `add_test()`：注册测试到 CTest

这样可以用 `ctest` 命令运行所有测试：
```bash
ctest --output-on-failure
```

**去掉会怎样？**
- `ctest` 命令找不到任何测试
- CI/CD 无法自动运行测试

---

## 4. 构建系统的有机整体联系

### 4.1 依赖关系图

```
┌─────────────────────────────────────────────────────────────────┐
│                        CMakeLists.txt                           │
│                                                                 │
│   find_package(OpenSSL)  ──→  OpenSSL::SSL, OpenSSL::Crypto    │
│   find_package(nlohmann_json) ──→ nlohmann_json::nlohmann_json  │
│   find_package(GTest) ──→ GTest::gtest, GTest::gtest_main      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                        可执行文件目标                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   WebServer              HttpParseTest         FileProcessTest  │
│   ├─ main.cpp            ├─ HttpParseTest.cpp  ├─ FileProcessTest.cpp
│   ├─ HttpProcess.cpp     ├─ TestMocks.cpp      ├─ FileProcess.cpp
│   ├─ FileProcess.cpp     ├─ HttpProcess.cpp    ├─ MyLog.cpp
│   ├─ ThreadPool.cpp      ├─ FileProcess.cpp    └─ link: GTest  │
│   ├─ MyLog.cpp           ├─ ThreadPool.cpp                       │
│   └─ link: OpenSSL       ├─ MyLog.cpp           ThreadPoolGTest │
│          nlohmann_json    └─ link: OpenSSL       ├─ ThreadPoolGTest.cpp
│                  pthread        nlohmann_json    ├─ ThreadPool.cpp
│                          GTest  pthread          ├─ MyLog.cpp    │
│                                                 └─ link: GTest  │
│                                                         pthread │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 编译过程追踪

以 `WebServer` 为例：

```
CMakeLists.txt
    │
    ├─ add_executable(WebServer main.cpp HttpProcess.cpp FileProcess.cpp ThreadPool.cpp MyLog.cpp)
    │
    ├─ target_include_directories(WebServer PRIVATE ./include)
    │   └─ 编译器参数: -I/home/yuan/workspace/clion/my-web-server/include
    │
    └─ target_link_libraries(WebServer OpenSSL::SSL nlohmann_json::nlohmann_json)
        └─ 链接器参数: -lssl -lcrypto -lnlohmann_json

生成的编译命令（简化）：
g++ -c -I./include -std=c++11 src/main.cpp -o main.o
g++ -c -I./include -std=c++11 src/HttpProcess.cpp -o HttpProcess.o
...
g++ -o WebServer main.o HttpProcess.o FileProcess.o ThreadPool.o MyLog.o \
    -lssl -lcrypto -lpthread
```

### 4.3 增量编译原理

```
第一次构建：
    main.cpp → main.o           (编译)
    HttpProcess.cpp → HttpProcess.o  (编译)
    ...
    *.o → WebServer             (链接)

修改 HttpProcess.cpp 后：
    main.cpp → main.o           (跳过，未变)
    HttpProcess.cpp → HttpProcess.o  (重新编译)
    FileProcess.cpp → FileProcess.o  (跳过，未变)
    ...
    *.o → WebServer             (重新链接)
```

**CMake 的依赖追踪**：
1. 扫描 `#include` 语句
2. 建立 `.cpp` → `.h` → `.h` 的依赖链
3. 如果任何 `.h` 变了，重新编译依赖它的 `.cpp`

---

## 5. 第三方库管理

### 5.1 库管理的三种方式

#### 方式 1：系统包管理器（apt/brew）

```bash
apt install libssl-dev libgtest-dev libnlohmann-json-dev
```

```cmake
find_package(OpenSSL REQUIRED)  # 找系统安装的库
```

**优点**：简单
**缺点**：
- 版本不可控（Ubuntu 20.04 和 22.04 的版本不同）
- 跨平台困难（Windows 没有 apt）
- 可能与系统其他软件冲突

#### 方式 2：vcpkg（本项目使用）

```bash
~/vcpkg/vcpkg install openssl nlohmann-json gtest --triplet x64-linux
```

```cmake
set(CMAKE_PREFIX_PATH "$ENV{HOME}/vcpkg/installed/x64-linux")
find_package(OpenSSL REQUIRED)
```

**优点**：
- 版本可控（`vcpkg.json` 锁定版本）
- 跨平台（同一套配置）
- 隔离（不污染系统）

**缺点**：
- 需要手动安装 vcpkg
- 首次构建慢（编译依赖）

#### 方式 3：FetchContent（CMake 3.11+）

```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)
```

**优点**：
- 完全自包含（克隆仓库就能构建）
- 版本精确控制

**缺点**：
- 每次构建都要下载（除非缓存）
- 增加构建时间

### 5.2 vcpkg 工作原理

```
vcpkg install openssl --triplet x64-linux
    │
    ├─ 下载源码
    ├─ 编译
    ├─ 安装到 ~/vcpkg/installed/x64-linux/
    │   ├─ include/openssl/
    │   ├─ lib/libssl.a, libcrypto.a
    │   └─ lib/cmake/openssl/  ← CMake 配置文件
    │
    └─ 生成 OpenSSLConfig.cmake（供 find_package 使用）
```

**关键文件**：`~/vcpkg/installed/x64-linux/lib/cmake/openssl/OpenSSLConfig.cmake`

这个文件告诉 CMake：
- 头文件在哪
- 库文件在哪
- 有哪些 Imported Target（`OpenSSL::SSL`、`OpenSSL::Crypto`）

### 5.3 依赖关系的传递性

```
WebServer
├─ 链接 OpenSSL::SSL
│  └─ OpenSSL::SSL 自动带上 OpenSSL::Crypto（传递依赖）
├─ 链接 nlohmann_json::nlohmann_json
└─ 链接 pthread（系统库，自动处理）
```

**Imported Target 的好处**：你只需要链接 `OpenSSL::SSL`，CMake 自动处理它依赖的 `OpenSSL::Crypto`。

如果手动链接：
```cmake
# 错误方式：手动指定
target_link_libraries(WebServer ssl crypto)  # 忘了 crypto 就链接失败
```

---

## 6. 软件工程实践

### 6.1 声明式 vs 命令式

**命令式（Makefile）**：
```makefile
server: main.o HttpProcess.o FileProcess.o ThreadPool.o MyLog.o
	g++ -o server main.o HttpProcess.o FileProcess.o ThreadPool.o MyLog.o -lssl -lcrypto

main.o: src/main.cpp include/HttpProcess.h include/ThreadPool.h
	g++ -c -I./include src/main.cpp -o main.o
# ... 每个文件都要写规则
```

**声明式（CMake）**：
```cmake
add_executable(WebServer main.cpp HttpProcess.cpp ...)
target_link_libraries(WebServer OpenSSL::SSL)
# 告诉 CMake "我要什么"，不是 "怎么做"
```

**软件工程原则**：声明式配置更容易维护，因为你在描述意图，而不是步骤。

### 6.2 目标（Target）为中心的设计

现代 CMake 的核心理念：

```
# 旧风格（全局变量）
include_directories(./include)           # 所有目标都用
link_libraries(ssl crypto)               # 所有目标都链接

# 新风格（Target 为中心）
target_include_directories(WebServer PRIVATE ./include)  # 只有 WebServer
target_link_libraries(WebServer PRIVATE OpenSSL::SSL)     # 只有 WebServer
```

**为什么新风格更好？**

| 问题 | 全局变量 | Target 为中心 |
|------|----------|---------------|
| 多目标隔离 | ❌ 所有目标共享 | ✅ 每个目标独立 |
| 可读性 | ❌ 不知道哪个目标用什么 | ✅ 一目了然 |
| 可维护性 | ❌ 改一处影响所有 | ✅ 改一处只影响目标 |

### 6.3 依赖管理的 SOLID 原则

| 原则 | 在 CMake 中的体现 |
|------|-------------------|
| **单一职责** | 每个 `add_executable` 只定义一个目标 |
| **开闭原则** | 添加新目标不修改现有目标 |
| **依赖倒置** | 用 Imported Target（`OpenSSL::SSL`），不直接用库名（`ssl`） |
| **接口隔离** | `PRIVATE`/`PUBLIC`/`INTERFACE` 控制依赖可见性 |

### 6.4 构建配置与代码分离

```
项目根目录/
├── CMakeLists.txt       # 构建配置
├── src/                 # 源代码
├── include/             # 头文件
├── test/                # 测试代码
└── docs/                # 文档
```

**软件工程原则**：构建配置、源代码、测试、文档分离。改动一个不影响其他。

### 6.5 可重复构建

```cmake
cmake_minimum_required(VERSION 3.22.1)  # 锁定 CMake 版本
set(CMAKE_CXX_STANDARD 11)              # 锁定 C++ 标准
set(CMAKE_PREFIX_PATH ".../vcpkg/...")   # 锁定依赖路径
find_package(OpenSSL REQUIRED)           # 锁定依赖
```

**软件工程原则**：任何人、任何时间、任何机器，用相同步骤能构建出相同结果。

这就是 **"可重复构建（Reproducible Build）"**。

---

## 7. 常见问题与最佳实践

### 7.1 常见错误

#### 错误 1：`find_package` 找不到

```
CMake Error: Could not find a package configuration file provided by "OpenSSL"
```

**原因**：`CMAKE_PREFIX_PATH` 没设对

**解决**：
```cmake
set(CMAKE_PREFIX_PATH "$ENV{HOME}/vcpkg/installed/x64-linux")
```

或命令行指定：
```bash
cmake -DCMAKE_PREFIX_PATH=~/vcpkg/installed/x64-linux ..
```

#### 错误 2：链接错误 `undefined reference`

```
undefined reference to `SSL_library_init'
```

**原因**：`target_link_libraries` 没链接对应的库

**解决**：
```cmake
target_link_libraries(WebServer PRIVATE OpenSSL::SSL OpenSSL::Crypto)
```

#### 错误 3：头文件找不到

```
'HttpProcess.h' file not found
```

**原因**：`target_include_directories` 没设置

**解决**：
```cmake
target_include_directories(WebServer PRIVATE ${CMAKE_SOURCE_DIR}/include)
```

### 7.2 最佳实践

#### 实践 1：用 `PRIVATE` 除非需要传递

```cmake
# 可执行文件：总是 PRIVATE
target_link_libraries(WebServer PRIVATE OpenSSL::SSL)

# 库：公共头文件用 PUBLIC，私有用 PRIVATE
target_include_directories(MyLib PUBLIC include PRIVATE src)
```

#### 实践 2：用 Imported Target，不用裸库名

```cmake
# ✅ 好
target_link_libraries(WebServer PRIVATE OpenSSL::SSL)

# ❌ 不好
target_link_libraries(WebServer PRIVATE ssl crypto)  # 容易漏
```

#### 实践 3：变量用 `${}`，命令用小写

```cmake
# ✅ 推荐
set(MY_VAR "value")
message(${MY_VAR})

# ⚠️ 可用但不推荐
SET(MY_VAR "value")
MESSAGE(${MY_VAR})
```

#### 实践 4：目录结构清晰

```
project/
├── CMakeLists.txt          # 顶层配置
├── src/
│   ├── CMakeLists.txt      # 可选：子目录配置
│   └── *.cpp
├── include/
│   └── *.h
├── test/
│   ├── CMakeLists.txt      # 可选：测试配置
│   └── *.cpp
└── third_party/            # 可选：第三方库源码
```

#### 实践 5：版本锁定

```cmake
cmake_minimum_required(VERSION 3.22.1)  # 最低版本
set(CMAKE_CXX_STANDARD 11)              # C++ 标准
```

```json
// vcpkg.json（可选，锁定依赖版本）
{
    "name": "my-web-server",
    "version-string": "1.0.0",
    "dependencies": [
        "openssl",
        "nlohmann-json",
        "gtest"
    ]
}
```

---

## 附录：CMake 命令速查

### 项目配置

| 命令 | 用途 |
|------|------|
| `cmake_minimum_required(VERSION x.y)` | 指定最低 CMake 版本 |
| `project(NAME)` | 定义项目 |
| `set(VAR value)` | 设置变量 |
| `option(VAR "desc" ON/OFF)` | 定义布尔选项 |

### 目标定义

| 命令 | 用途 |
|------|------|
| `add_executable(NAME sources...)` | 定义可执行文件 |
| `add_library(NAME STATIC/SHARED sources...)` | 定义库 |
| `target_include_directories(NAME PRIVATE/PUBLIC/INTERFACE dirs...)` | 头文件路径 |
| `target_link_libraries(NAME PRIVATE/PUBLIC/INTERFACE libs...)` | 链接库 |
| `target_compile_definitions(NAME PRIVATE/PUBLIC/INTERFACE defs...)` | 预处理器定义 |
| `target_compile_options(NAME PRIVATE/PUBLIC/INTERFACE opts...)` | 编译选项 |

### 依赖查找

| 命令 | 用途 |
|------|------|
| `find_package(NAME CONFIG/MODULE REQUIRED)` | 查找依赖包 |
| `FetchContent_Declare(NAME GIT_REPOSITORY url GIT_TAG tag)` | 声明远程依赖 |
| `FetchContent_MakeAvailable(NAME)` | 下载并可用 |

### 测试

| 命令 | 用途 |
|------|------|
| `enable_testing()` | 启用 CTest |
| `add_test(NAME name COMMAND cmd)` | 注册测试 |

---

## 推荐阅读

- [CMake 官方教程](https://cmake.org/cmake/help/latest/guide/tutorial/index.html)
- [Modern CMake](https://cliutils.gitlab.io/modern-cmake/)
- [An Introduction to Modern CMake](https://cmake.org/cmake/help/book/mastering-cmake/chapter/An%20Introduction%20to%20Modern%20CMake.html)
- [vcpkg 文档](https://vcpkg.io/en/docs/examples/installing-and-using-packages.html)

---

*文档版本：v1.0*
*最后更新：2026-04-08*
*适用项目：my-web-server*
