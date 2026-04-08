# C++11 线程同步深度指南

> 面向已经掌握基础线程用法，但在内存模型和原子操作上仍有困惑的中级 C++ 开发者。

---

## 目录

1. [原子操作与内存模型](#1-原子操作与内存模型)
2. [内存序语义详解](#2-内存序语义详解)
3. [内存屏障](#3-内存屏障)
4. [Mutex 与 Atomic 的取舍](#4-mutex-与-atomic-的取舍)
5. [常见陷阱](#5-常见陷阱)
6. [最佳实践](#6-最佳实践)

---

## 1. 原子操作与内存模型

### 1.1 为什么普通变量会出问题

先看一段"看似正确"的代码：

```cpp
#include <thread>
#include <iostream>

int counter = 0;  // 普通 int，没有任何同步保护

void increment() {
    for (int i = 0; i < 100000; ++i) {
        counter++;  // 这不是原子操作！
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter << std::endl;
    // 期望 200000，实际经常小于这个值
}
```

`counter++` 看起来是一行代码，但编译后至少包含三步：

1. **读取** counter 的值到寄存器
2. **加一**
3. **写回** 内存

两个线程可能同时执行到第 1 步，读到相同的旧值，各自加一后写回，结果只增加了一次。这就是**数据竞争（data race）**。

更隐蔽的问题是**编译器优化**和**CPU 乱序执行**。编译器可能把变量缓存在寄存器里不回写内存，CPU 可能为了性能打乱指令顺序。在单线程里这些优化是安全的，但在多线程环境下会破坏你的预期。

### 1.2 std::atomic 基础

`std::atomic<T>` 提供了无锁的原子操作，保证读-改-写操作的不可分割性：

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> counter{0};  // 原子整数

void increment() {
    for (int i = 0; i < 100000; ++i) {
        counter++;  // 现在是原子操作了
        // 等价于 counter.fetch_add(1, std::memory_order_seq_cst)
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter.load() << std::endl;
    // 保证输出 200000
}
```

`std::atomic` 的核心操作：

```cpp
std::atomic<int> val{0};

// 读写
val.store(42);           // 原子写入
int x = val.load();      // 原子读取

// 读-改-写
val.fetch_add(1);        // 返回旧值，val 变成 val+1
val.fetch_sub(1);        // 返回旧值，val 变成 val-1
val++;                   // 等价于 fetch_add(1)
++val;                   // 同上

// 比较并交换（CAS）
int expected = 0;
int desired = 42;
// 如果 val == expected，则 val = desired，返回 true
// 否则 expected = val，返回 false
bool success = val.compare_exchange_strong(expected, desired);

// 弱版本，可能在 val == expected 时也返回 false（允许伪失败）
// 适合循环场景，性能更好
while (!val.compare_exchange_weak(expected, desired)) {
    // expected 已被更新为当前值
}
```

### 1.3 C++11 内存模型的核心思想

C++11 内存模型回答了一个根本问题：**一个线程对内存的写入，何时对另一个线程可见？**

在没有同步的情况下，答案是**不确定的**。编译器和 CPU 可以自由重排指令，只要在单线程视角下结果一致就行。

内存模型定义了两种关系：

- **happens-before（先行关系）**：操作 A 的效果对操作 B 可见
- **synchronizes-with（同步关系）**：建立跨线程的 happens-before

```cpp
std::atomic<bool> ready{false};
int data = 0;

// 线程 1
void producer() {
    data = 42;                          // (1) 写数据
    ready.store(true, std::memory_order_release);  // (2) 发布就绪信号
}

// 线程 2
void consumer() {
    while (!ready.load(std::memory_order_acquire))  // (3) 等待就绪
        ;  // 自旋
    // 此处保证 (1) happens-before (4)
    std::cout << data << std::endl;     // (4) 读数据，保证看到 42
}
```

这里 `ready.store(release)` 和 `ready.load(acquire)` 建立了 synchronizes-with 关系，从而让 (1) happens-before (4)。

---

## 2. 内存序语义详解

`std::memory_order` 告诉编译器和 CPU：这个原子操作周围的操作可以怎样重排。从强到弱有四种主要模式。

### 2.1 顺序一致性（memory_order_seq_cst）

**默认选项，最强保证，最简单推理。**

所有使用 `seq_cst` 的操作形成一个全局的全序关系，所有线程看到的顺序一致。

```cpp
#include <atomic>
#include <thread>
#include <cassert>

std::atomic<bool> x{false}, y{false};
std::atomic<int> z{0};

void write_x() {
    x.store(true, std::memory_order_seq_cst);  // (1)
}

void write_y() {
    y.store(true, std::memory_order_seq_cst);  // (2)
}

void read_x_then_y() {
    while (!x.load(std::memory_order_seq_cst))
        ;
    if (y.load(std::memory_order_seq_cst)) {   // (3)
        ++z;
    }
}

void read_y_then_x() {
    while (!y.load(std::memory_order_seq_cst))
        ;
    if (x.load(std::memory_order_seq_cst)) {   // (4)
        ++z;
    }
}

int main() {
    std::thread a(write_x);
    std::thread b(write_y);
    std::thread c(read_x_then_y);
    std::thread d(read_y_then_x);
    a.join(); b.join(); c.join(); d.join();
    assert(z.load() != 0);  // 顺序一致性保证：z 一定不为 0
}
```

**为什么 z 一定不为 0？**

顺序一致性保证存在一个全局的操作顺序。要么 x 先写，要么 y 先写：
- 如果 x 先写：`read_y_then_x` 一定能看到 x=true，z++
- 如果 y 先写：`read_x_then_y` 一定能看到 y=true，z++

**代价**：在 x86 上几乎零开销（因为 x86 本身是强内存序），但在 ARM/PowerPC 上需要额外的内存屏障指令。

### 2.2 获取-释放语义（acquire-release）

**比顺序一致性弱，但仍然提供有用的同步保证。**

- **release**：本线程中，此操作之前的所有读写不能重排到此操作之后
- **acquire**：本线程中，此操作之后的所有读写不能重排到此操作之前
- **acq_rel**：同时具有 acquire 和 release 语义

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

std::atomic<bool> ready{false};
int payload[100];  // 非原子数据

void producer() {
    // 准备数据
    for (int i = 0; i < 100; ++i) {
        payload[i] = i * i;
    }
    // 发布：release 保证上面的写入不会被重排到 store 之后
    ready.store(true, std::memory_order_release);
}

void consumer() {
    // 等待：acquire 保证 load 之后的读取不会被重排到 load 之前
    while (!ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    // 安全读取 payload
    for (int i = 0; i < 100; ++i) {
        std::cout << payload[i] << " ";
    }
    std::cout << std::endl;
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
}
```

**关键点**：release 和 acquire 必须配对使用。单独的 release 或 acquire 没有意义。

```cpp
// 错误示例：只有 release 没有 acquire
std::atomic<int> flag{0};

// 线程 1
data = 42;
flag.store(1, std::memory_order_release);

// 线程 2
if (flag.load(std::memory_order_relaxed) == 1) {  // 用了 relaxed！
    // 不能保证看到 data == 42
    std::cout << data << std::endl;  // 未定义行为
}
```

### 2.3 松弛序（memory_order_relaxed）

**最弱的保证，只保证原子性，不保证顺序。**

适用于只需要计数器、统计量等不需要同步其他数据的场景。

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<int> counter{0};

void increment() {
    for (int i = 0; i < 100000; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

int main() {
    std::thread t1(increment);
    std::thread t2(increment);
    t1.join();
    t2.join();
    std::cout << "counter = " << counter.load(std::memory_order_relaxed) << std::endl;
    // 保证输出 200000（原子性），但不保证其他任何顺序
}
```

**relaxed 的典型用途**：

```cpp
// 统计请求计数（不需要同步其他数据）
std::atomic<uint64_t> request_count{0};

void handle_request() {
    request_count.fetch_add(1, std::memory_order_relaxed);
    // 处理请求...
}

// 监控线程定期读取
void monitor() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Requests/sec: " 
                  << request_count.exchange(0, std::memory_order_relaxed) 
                  << std::endl;
    }
}
```

### 2.4 内存序选择速查表

| 场景 | 推荐内存序 | 理由 |
|------|-----------|------|
| 简单计数器、统计量 | `relaxed` | 不需要同步其他数据 |
| 发布-订阅模式 | `release`/`acquire` | 需要同步数据，但不需要全局顺序 |
| 自旋锁 | `release`/`acquire` | 锁获取用 acquire，释放用 release |
| 多个原子变量需要全局顺序 | `seq_cst` | 需要全局一致的观察顺序 |
| 不确定时 | `seq_cst` | 安全第一，性能问题以后再优化 |

---

## 3. 内存屏障

### 3.1 什么是内存屏障

内存屏障（memory fence）是一条 CPU 指令，告诉处理器：在这条指令之前的所有内存操作必须完成，才能执行这条指令之后的内存操作。

在 C++11 中，`std::atomic_thread_fence` 提供了独立于原子操作的内存屏障：

```cpp
#include <atomic>
#include <thread>

std::atomic<bool> ready{false};
int data = 0;

void producer() {
    data = 42;  // (1) 普通写入
    // release 屏障：保证 (1) 在屏障之前完成
    std::atomic_thread_fence(std::memory_order_release);
    ready.store(true, std::memory_order_relaxed);  // (2) 松弛写入
}

void consumer() {
    while (!ready.load(std::memory_order_relaxed))  // (3) 松弛读取
        ;
    // acquire 屏障：保证 (4) 在屏障之后执行
    std::atomic_thread_fence(std::memory_order_acquire);
    std::cout << data << std::endl;  // (4) 保证看到 42
}
```

### 3.2 屏障与内存序的关系

内存屏障本质上是把内存序语义从原子操作中"拆分"出来。上面的代码等价于：

```cpp
void producer() {
    data = 42;
    ready.store(true, std::memory_order_release);  // 隐含 release 屏障
}

void consumer() {
    while (!ready.load(std::memory_order_acquire))  // 隐含 acquire 屏障
        ;
    std::cout << data << std::endl;
}
```

**什么时候需要显式屏障？**

当你需要同步非原子数据，但原子操作本身不需要强内存序时：

```cpp
// 场景：flag 用 relaxed 就够了，但需要同步 data
std::atomic<int> flag{0};
std::string data;

void writer() {
    data = "hello";  // (1)
    std::atomic_thread_fence(std::memory_order_release);  // 屏障
    flag.store(1, std::memory_order_relaxed);  // (2) 只需 relaxed
}

void reader() {
    while (flag.load(std::memory_order_relaxed) != 1)  // (3) 只需 relaxed
        ;
    std::atomic_thread_fence(std::memory_order_acquire);  // 屏障
    std::cout << data << std::endl;  // (4) 保证看到 "hello"
}
```

### 3.3 单向屏障

C++11 还提供了单向屏障：

```cpp
// 只保证写操作不越过屏障（写屏障）
std::atomic_thread_fence(std::memory_order_release);

// 只保证读操作不越过屏障（读屏障）
std::atomic_thread_fence(std::memory_order_acquire);

// 双向屏障（读写都不能越过）
std::atomic_thread_fence(std::memory_order_seq_cst);
```

### 3.4 实际应用：Dekker 算法

Dekker 算法需要保证两个写操作的可见性顺序，这正是内存屏障的用武之地：

```cpp
#include <atomic>
#include <thread>
#include <iostream>

std::atomic<bool> flag1{false}, flag2{false};
std::atomic<int> turn{0};

void critical_section_1() {
    flag1.store(true, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);  // 全序屏障
    while (flag2.load(std::memory_order_relaxed)) {
        if (turn.load(std::memory_order_relaxed) != 0) {
            flag1.store(false, std::memory_order_relaxed);
            while (turn.load(std::memory_order_relaxed) != 0)
                ;
            flag1.store(true, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
        }
    }
    // 临界区
    std::cout << "Thread 1 in critical section" << std::endl;
    turn.store(1, std::memory_order_relaxed);
    flag1.store(false, std::memory_order_relaxed);
}
```

---

## 4. Mutex 与 Atomic 的取舍

### 4.1 性能对比

```cpp
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>

// 方案 1：使用 mutex
std::mutex mtx;
int counter_mutex = 0;

void increment_mutex() {
    for (int i = 0; i < 1000000; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        ++counter_mutex;
    }
}

// 方案 2：使用 atomic
std::atomic<int> counter_atomic{0};

void increment_atomic() {
    for (int i = 0; i < 1000000; ++i) {
        counter_atomic.fetch_add(1, std::memory_order_relaxed);
    }
}

int main() {
    // 测试 mutex 版本
    auto start = std::chrono::high_resolution_clock::now();
    std::thread t1(increment_mutex);
    std::thread t2(increment_mutex);
    t1.join(); t2.join();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Mutex: " 
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() 
              << " us" << std::endl;

    // 测试 atomic 版本
    start = std::chrono::high_resolution_clock::now();
    std::thread t3(increment_atomic);
    std::thread t4(increment_atomic);
    t3.join(); t4.join();
    end = std::chrono::high_resolution_clock::now();
    std::cout << "Atomic: " 
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() 
              << " us" << std::endl;
}
```

典型结果（x86-64）：
- Mutex: ~50000 us
- Atomic: ~5000 us（快 10 倍）

### 4.2 何时用 Mutex

**适合 mutex 的场景**：

1. **保护复杂数据结构**：链表、树、哈希表等需要多步操作
2. **临界区较大**：需要保护多行代码的执行
3. **需要条件变量**：等待某个条件成立
4. **代码可读性优先**：mutex 的语义更直观

```cpp
// 适合 mutex 的例子：保护共享的请求队列
class RequestQueue {
public:
    void push(Request req) {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push_back(std::move(req));
        cv_.notify_one();
    }

    Request pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        Request req = std::move(queue_.front());
        queue_.pop_front();
        return req;
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Request> queue_;
};
```

### 4.3 何时用 Atomic

**适合 atomic 的场景**：

1. **简单计数器**：引用计数、统计量
2. **标志位**：停止信号、就绪标志
3. **无锁数据结构**：队列、栈、哈希表
4. **性能关键路径**：mutex 成为瓶颈时

```cpp
// 适合 atomic 的例子：引用计数
class SharedObject {
public:
    SharedObject() : ref_count_(1) {}

    void add_ref() {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

private:
    std::atomic<int> ref_count_;
};
```

### 4.4 混合使用

实际项目中经常混合使用 mutex 和 atomic：

```cpp
class ConnectionPool {
public:
    Connection* acquire() {
        // 快速路径：atomic 检查
        if (available_.load(std::memory_order_acquire) == 0) {
            return nullptr;
        }
        
        // 慢速路径：mutex 保护实际操作
        std::lock_guard<std::mutex> lock(mtx_);
        if (pool_.empty()) {
            return nullptr;
        }
        Connection* conn = pool_.back();
        pool_.pop_back();
        available_.fetch_sub(1, std::memory_order_release);
        return conn;
    }

    void release(Connection* conn) {
        std::lock_guard<std::mutex> lock(mtx_);
        pool_.push_back(conn);
        available_.fetch_add(1, std::memory_order_release);
    }

private:
    std::mutex mtx_;
    std::vector<Connection*> pool_;
    std::atomic<int> available_{0};
};
```

---

## 5. 常见陷阱

### 5.1 数据竞争

**最隐蔽的数据竞争**：看似有同步，实际没有。

```cpp
// 陷阱 1：双重检查锁定的经典错误
class Singleton {
public:
    static Singleton* instance() {
        if (instance_ == nullptr) {  // (1) 第一次检查，无锁
            std::lock_guard<std::mutex> lock(mtx_);
            if (instance_ == nullptr) {  // (2) 第二次检查，有锁
                instance_ = new Singleton();  // (3) 问题所在！
            }
        }
        return instance_;
    }

private:
    static Singleton* instance_;
    static std::mutex mtx_;
};

// 正确做法：使用 atomic 或 C++11 的静态局部变量
class Singleton {
public:
    static Singleton& instance() {
        static Singleton inst;  // C++11 保证线程安全
        return inst;
    }
};
```

**为什么双重检查锁定有问题？**

`new Singleton()` 包含三步：
1. 分配内存
2. 构造对象
3. 让 instance_ 指向新对象

编译器/CPU 可能重排为 1→3→2。另一个线程可能在步骤 3 之后、步骤 2 之前看到 instance_ 不为 nullptr，返回一个未构造完成的对象。

### 5.2 伪共享（False Sharing）

```cpp
// 陷阱 2：伪共享导致性能下降
struct BadLayout {
    std::atomic<int> counter1;  // 线程 1 修改
    std::atomic<int> counter2;  // 线程 2 修改
    // 这两个变量可能在同一缓存行（64 字节）
    // 导致两个线程不断使对方的缓存失效
};

// 正确做法：填充到缓存行大小
struct alignas(64) GoodLayout {
    std::atomic<int> counter1;
    char padding[60];  // 填充到 64 字节
};

struct alignas(64) GoodLayout2 {
    std::atomic<int> counter2;
    char padding[60];
};

// 或者使用 C++17 的 hardware_destructive_interference_size
struct alignas(std::hardware_destructive_interference_size) OptimalLayout {
    std::atomic<int> counter1;
};
```

**性能影响**：伪共享可能导致 10 倍以上的性能下降。

### 5.3 ABA 问题

```cpp
// 陷阱 3：ABA 问题（无锁栈的经典陷阱）
template<typename T>
class LockFreeStack {
public:
    void push(T value) {
        Node* new_node = new Node(std::move(value));
        new_node->next = head_.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(
            new_node->next, new_node,
            std::memory_order_release,
            std::memory_order_relaxed))
            ;
    }

    std::optional<T> pop() {
        Node* old_head = head_.load(std::memory_order_acquire);
        while (old_head && 
               !head_.compare_exchange_weak(
                   old_head, old_head->next,
                   std::memory_order_acquire,
                   std::memory_order_relaxed))
            ;
        if (old_head) {
            T value = std::move(old_head->value);
            // 问题：这里 old_head 可能已经被其他线程 push 回来了
            // delete old_head 可能导致 double-free
            delete old_head;
            return value;
        }
        return std::nullopt;
    }

private:
    struct Node {
        T value;
        Node* next;
        Node(T v) : value(std::move(v)), next(nullptr) {}
    };
    std::atomic<Node*> head_{nullptr};
};
```

**ABA 场景**：
1. 线程 A 读取 head = A
2. 线程 A 被中断
3. 线程 B pop A, pop B, push A（A 又回来了）
4. 线程 A 恢复，CAS 成功（因为 head 还是 A）
5. 但 A->next 已经不是原来的值了

**解决方案**：使用带版本号的指针（tagged pointer）或 hazard pointer。

### 5.4 忘记处理 CAS 伪失败

```cpp
// 陷阱 4：compare_exchange_weak 的伪失败
std::atomic<int> value{0};

// 错误：假设 weak 版本只在值不匹配时失败
int expected = 0;
if (value.compare_exchange_weak(expected, 42)) {
    // 成功
} else {
    // 错误处理：以为 expected 一定是旧值
    // 实际上 weak 版本可能在值匹配时也返回 false
}

// 正确：用循环处理 weak 版本
int expected = 0;
while (!value.compare_exchange_weak(expected, 42)) {
    // expected 会被更新为当前值
    // 继续重试
}

// 或者直接用 strong 版本
int expected = 0;
if (value.compare_exchange_strong(expected, 42)) {
    // 成功
} else {
    // 这里 expected 一定是当前值
}
```

### 5.5 过度使用 seq_cst

```cpp
// 陷阱 5：所有操作都用 seq_cst（性能杀手）
std::atomic<int> counter{0};

void bad_example() {
    // 不需要全局顺序，用 relaxed 就够了
    counter.fetch_add(1, std::memory_order_seq_cst);  // 过度同步
}

void good_example() {
    counter.fetch_add(1, std::memory_order_relaxed);  // 正确选择
}
```

---

## 6. 最佳实践

### 6.1 选择同步原语的决策树

```
需要保护什么？
├── 简单计数器/标志位
│   └── 使用 atomic
│       ├── 只需原子性 → memory_order_relaxed
│       ├── 需要同步其他数据 → memory_order_acquire/release
│       └── 不确定 → memory_order_seq_cst
├── 复杂数据结构
│   └── 使用 mutex
│       ├── 需要等待条件 → mutex + condition_variable
│       └── 不需要等待 → mutex + lock_guard
└── 无锁数据结构
    └── 使用 atomic + CAS
        ├── 注意 ABA 问题
        └── 注意内存回收
```

### 6.2 代码规范

```cpp
// 1. 原子变量命名：加 _atomic 后缀或使用明显前缀
std::atomic<int> counter_atomic{0};
std::atomic<bool> stop_flag{false};

// 2. 总是显式指定内存序
// 好：
counter.fetch_add(1, std::memory_order_relaxed);
// 坏：依赖默认值（虽然默认是 seq_cst，但不明确）
counter.fetch_add(1);

// 3. 使用 RAII 管理锁
// 好：
{
    std::lock_guard<std::mutex> lock(mtx);
    // 临界区
}
// 坏：
mtx.lock();
// 临界区
mtx.unlock();  // 可能忘记，或异常时不会释放

// 4. 避免嵌套锁（死锁风险）
// 如果必须嵌套，使用 std::scoped_lock（C++17）
std::scoped_lock lock(mtx1, mtx2);  // 自动按顺序加锁

// 5. 使用 static 局部变量代替手动单例
Singleton& get_instance() {
    static Singleton instance;  // C++11 保证线程安全
    return instance;
}
```

### 6.3 性能优化技巧

```cpp
// 1. 批量操作减少原子操作
std::atomic<int> counter{0};

// 慢：每次循环都原子操作
void slow() {
    for (int i = 0; i < 1000; ++i) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
}

// 快：本地累加，最后一次性更新
void fast() {
    int local = 0;
    for (int i = 0; i < 1000; ++i) {
        ++local;
    }
    counter.fetch_add(local, std::memory_order_relaxed);
}

// 2. 读写分离：读用 atomic，写用 mutex
class Config {
public:
    std::string get_name() const {
        return name_.load(std::memory_order_acquire);
    }
    
    void set_name(std::string name) {
        std::lock_guard<std::mutex> lock(mtx_);
        name_ = name;
    }

private:
    mutable std::mutex mtx_;
    std::atomic<std::string*> name_{nullptr};  // 简化示例
};

// 3. 避免热缓存行：分散热点数据
struct alignas(64) PerThreadCounter {
    std::atomic<int> value{0};
};

PerThreadCounter counters[std::thread::hardware_concurrency()];
```

### 6.4 调试技巧

```cpp
// 1. 使用 Thread Sanitizer（TSan）
// 编译时加 -fsanitize=thread
// g++ -fsanitize=thread -g -o program program.cpp

// 2. 使用 assert 验证不变量
void transfer(Account& from, Account& to, int amount) {
    assert(amount > 0);
    assert(from.balance.load() >= amount);  // 可能有竞争，仅用于调试
    
    from.balance.fetch_sub(amount, std::memory_order_relaxed);
    to.balance.fetch_add(amount, std::memory_order_relaxed);
}

// 3. 使用原子变量记录状态转换
std::atomic<int> state{0};
// 0: idle, 1: running, 2: stopping

void start() {
    int expected = 0;
    if (state.compare_exchange_strong(expected, 1)) {
        // 成功启动
    } else {
        // 记录错误状态
        std::cerr << "Invalid state transition: " << expected << std::endl;
    }
}
```

### 6.5 推荐阅读

- **书籍**：
  - 《C++ Concurrency in Action》第2版 - Anthony Williams
  - 《Effective Modern C++》第40-44条 - Scott Meyers
  
- **在线资源**：
  - [cppreference: std::memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)
  - [Herb Sutter: atomic<> Weapons](https://herbsutter.com/2013/02/11/atomic-weapons-the-c-memory-model-and-modern-hardware/)
  
- **工具**：
  - Thread Sanitizer（检测数据竞争）
  - [Relacy Race Detector](https://github.com/dvyukov/relacy)（专门测试并发代码）

---

## 总结

| 概念 | 要点 |
|------|------|
| 原子操作 | 保证读-改-写的不可分割性，但不自动保证顺序 |
| 内存序 | 控制操作重排的自由度，从强到弱：seq_cst > acquire/release > relaxed |
| 内存屏障 | 独立的同步原语，用于需要同步非原子数据的场景 |
| Mutex | 适合保护复杂数据结构，语义直观，有开销 |
| Atomic | 适合简单操作，性能高，但推理困难 |
| 选择原则 | 能用 atomic 就用 atomic，不确定就用 mutex，内存序不确定就用 seq_cst |

记住：**正确性永远比性能重要**。先写正确的代码，再用性能分析工具找出真正的瓶颈，最后才考虑优化同步策略。
