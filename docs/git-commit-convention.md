# Git 提交规范

## 1. 核心原则：一次提交只做一件事

```
❌ 错误：把日志修改、bug 修复、文档、构建产物全放一次提交
✅ 正确：日志修改是一个提交，bug 修复是另一个提交
```

**为什么？**
- 回滚：如果 bug 修复引入了新 bug，你可以只回滚那一个提交，不影响日志修改
- Code Review：reviewer 一次只看一个逻辑变更，容易发现问题
- bisect：`git bisect` 找 bug 时，粒度越细越容易定位
- blame：`git blame` 能准确显示每一行是谁、为什么改的

## 2. Commit Message 格式

### 2.1 标准格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

### 2.2 Type 类型

```
feat:     新功能
fix:      修复 bug
refactor: 重构（不改变功能，不修复 bug）
style:    代码风格（格式化、空格、命名）
docs:     文档
test:     测试
chore:    构建、依赖、工具等杂项
perf:     性能优化
```

### 2.3 示例

```
fix(HttpProcess): 修复 EPOLLONESHOT 未注册导致重复处理

主线程在处理 EPOLLIN 时没有检查 connection 是否正在处理，
导致同一 fd 被多次提交到线程池，造成数据竞争。

添加 EPOLLONESHOT 标志和 rearm_queue 机制，
确保每个 fd 同一时刻只有一个 worker 在处理。
```

```
feat(MyLog): 日志输出添加线程 ID

使用 std::this_thread::get_id() 获取当前线程 ID，
方便追踪多线程环境下的日志来源。
```

```
docs: 添加 epoll + 线程池并发处理文档

详细解释 EPOLLET 模式下的竞态条件和 EPOLLONESHOT 解法。
```

```
chore: 添加 .gitignore 排除构建产物和日志文件
```

## 3. 你的提交问题分析

### 3.1 最新提交 `9e6eae3`

```
commit message: fix:日志打印位置修正为调用方

实际改动（58 个文件！）：
├── src/MyLog.cpp              ← 日志改动（符合 message）
├── src/HttpProcess.cpp         ← bug 修复（不符合 message）
├── src/ThreadPool.cpp          ← bug 修复（不符合 message）
├── src/main.cpp                ← 多处改动（不符合 message）
├── include/MyLog.h             ← 接口改动
├── include/ThreadPool.h        ← 接口改动
├── AGENTS.md                   ← 项目文档（不符合 message）
├── docs/*.md × 7              ← 新文档（不符合 message）
├── build/                      ← 构建产物（不该提交）
├── build/*.log                 ← 日志文件（不该提交）
├── test/*.cpp × 3             ← 测试改动
└── CMake 缓存文件              ← 不该提交
```

**这个提交包含了至少 5 种不同类型的变更：**
1. 日志 API 改动 → 应该单独提交
2. ThreadPool bug 修复 → 应该单独提交
3. 新文档 → 应该单独提交
4. 测试修复 → 应该单独提交
5. 构建产物 + 日志文件 → 根本不该提交

### 3.2 应该怎么拆

```
提交 1: chore: 添加 .gitignore
提交 2: refactor(MyLog): 日志 API 添加文件名和行号参数
提交 3: fix(ThreadPool): 修复 stop_ 竞态条件，改用 atomic<bool>
提交 4: fix(HttpProcess): 添加 EPOLLONESHOT 防止重复处理
提交 5: docs: 添加 C++11 并发和 epoll 相关文档
提交 6: test: 更新测试文件适配新日志 API
```

### 3.3 另一个提交 `571a416`

```
commit message: 修改遇到的问题的文件

问题：
- "修改遇到的问题" 太模糊，不知道改了什么
- 没有 type 前缀
- 没有 scope
- 改了 problem.md（25 行），但 message 没说改了什么
```

应该写成：
```
docs(problem): 更新 EPOLLONESHOT 竞态问题分析
```

## 4. 实操流程

### 4.1 提交前检查

```bash
# 1. 看改了什么
git status
git diff

# 2. 确认没有构建产物
git status | grep -E "\.o$|\.log$|build/" && echo "有不该提交的文件！"

# 3. 只暂存本次提交相关的文件
git add src/MyLog.cpp include/MyLog.h  # 只加日志相关

# 4. 提交
git commit -m "refactor(MyLog): 日志 API 添加文件名和行号参数"

# 5. 下一批
git add src/ThreadPool.cpp include/ThreadPool.h
git commit -m "fix(ThreadPool): 修复 stop_ 竞态条件，改用 atomic<bool>"
```

### 4.2 如果已经混在一起了

```bash
# 撤回最近一次提交（保留改动在工作区）
git reset HEAD~1

# 现在所有改动都在工作区，可以重新分批提交
git add src/MyLog.cpp include/MyLog.h
git commit -m "refactor(MyLog): ..."

git add src/ThreadPool.cpp include/ThreadPool.h
git commit -m "fix(ThreadPool): ..."

# 剩下的文件
git add docs/
git commit -m "docs: ..."
```

### 4.3 强制规则

```
1. build/ 目录永远不要提交
2. *.log 文件永远不要提交
3. .o / .d / .so / .a 文件永远不要提交
4. 一次提交 = 一个逻辑变更
5. commit message 必须说清楚"做了什么"和"为什么"
```

## 5. 快速参考

```
场景                        type       scope              示例
──────────────────────────────────────────────────────────────────
修复了一个 bug               fix        模块名             fix(HttpProcess): 修复 EAGAIN 未处理
添加新功能                   feat       模块名             feat(MyLog): 添加日志级别过滤
重构代码但功能不变            refactor   模块名             refactor(ThreadPool): 提取 task 获取逻辑
改了文档                     docs       (可选)             docs: 添加 epoll 并发处理指南
改了测试                     test       模块名             test(MyLogTest): 适配新 API 签名
改了构建配置                 chore      (可选)             chore: 添加 .gitignore
只改格式/命名                style      模块名             style(HttpProcess): 统一变量命名
性能优化                     perf       模块名             perf(send_all): 减少 epoll_ctl 调用
```
