# C++11 并发标准：编译器行为与数据竞争

## 核心问题

```cpp
bool stop_ = false;  // 全局/成员变量

// 线程 A (主线程)
void set_stop() {
    stop_ = true;  // 写
}

// 线程 B (工作线程)
void worker() {
    while (!stop_) {  // 读
        // 做一些工作
    }
}
```

**这段代码是 undefined behavior (UB)。** 原因不是 CPU 架构问题，而是 C++ 标准的规定。

---

## 1. C++11 标准的明确规定

### 1.1 数据竞争 (Data Race) 的定义

C++11 标准 §1.10/21：

> "The execution of a program contains a **data race** if it contains two conflicting actions in different threads, at least one of which is not atomic, and neither happens before the other."

翻译：
- 两个不同线程访问同一内存位置
- 至少有一个是写操作
- 至少有一个不是原子操作
- 两个操作没有 happens-before 关系

**→ 这就是 undefined behavior。**

### 1.2 未定义行为的后果

C++11 标准 §1.3.24：

> "undefined behavior: behavior for which this International Standard imposes no requirements"

这意味着编译器可以：
- 假设 UB 永远不会发生
- 基于这个假设做任意优化
- 生成的代码可以做任何事情，包括崩溃、死循环、读到垃圾值

---

## 2. 编译器会做什么优化

### 2.1 寄存器缓存 (Register Caching)

```cpp
// 源代码
while (!stop_) {
    do_work();
}

// 编译器看到的（伪代码）
bool cached_stop = stop_;  // 第一次读取，加载到寄存器
while (!cached_stop) {      // 之后直接用寄存器值
    do_work();
    // 注意：没有重新读取 stop_ 的指令
}
```

**编译器的推理过程：**

1. 在这个函数内部，没有代码修改 `stop_`
2. 编译器看不到其他线程（C++ 抽象机是单线程的）
3. 所以 `stop_` 在循环中不会改变
4. 所以可以把 `stop_` 缓存到寄存器，避免重复内存访问

**结果：** 工作线程永远看不到主线程对 `stop_` 的写入。

### 2.2 代码重排 (Instruction Reordering)

```cpp
// 源代码
data = 42;
ready = true;

// 编译器可能重排为
ready = true;  // 先写 ready
data = 42;     // 后写 data
```

在单线程下，这个重排是合法的，因为结果相同。

但在多线程下，另一个线程可能看到 `ready == true` 但 `data` 还是旧值。

### 2.3 死代码消除 (Dead Code Elimination)

```cpp
// 源代码
void worker() {
    bool local_stop = stop_;
    while (!local_stop) {
        do_work();
        local_stop = stop_;  // 重新读取
    }
}

// 编译器优化后（如果 do_work() 不修改 stop_）
void worker() {
    // 编译器：stop_ 在 do_work() 中不会被修改（单线程视角）
    // 所以 local_stop 永远是 false
    // 所以这是死循环
    // 所以可以优化掉循环条件检查
    while (true) {
        do_work();
    }
}
```

### 2.4 合并写入 (Write Combining)

```cpp
// 源代码
for (int i = 0; i < 100; i++) {
    counter = i;  // 写 100 次
}

// 编译器可能优化为
counter = 99;  // 只写最后一次
```

---

## 3. 实际编译器行为示例

### 3.1 GCC -O2 优化

```cpp
// test.cpp
bool flag = false;

void writer() {
    flag = true;
}

void reader() {
    while (!flag) {
        // 空循环
    }
}
```

编译输出（GCC 11, -O2）：

```asm
reader():
        movzx   eax, BYTE PTR flag[rip]  ; 只读一次
        test    al, al
        je      .L4                       ; 如果是 false，死循环
        ret
.L4:
        jmp     .L4                       ; 无限循环，不再读 flag
```

**看清楚：`movzx` 只执行一次，之后就是死循环 `jmp .L4`。**

### 3.2 加上 `volatile`

```cpp
volatile bool flag = false;

void reader() {
    while (!flag) {
        // 空循环
    }
}
```

编译输出：

```asm
reader():
.L3:
        movzx   eax, BYTE PTR flag[rip]  ; 每次循环都读
        test    al, al
        je      .L3                       ; 读到 false 继续
        ret
```

**`volatile` 强制每次读内存，但在 C++ 多线程中不是正确的解决方案（见下文）。**

### 3.3 使用 `std::atomic<bool>`

```cpp
std::atomic<bool> flag{false};

void reader() {
    while (!flag.load(std::memory_order_relaxed)) {
        // 空循环
    }
}
```

编译输出（GCC 11, -O2）：

```asm
reader():
.L3:
        movzx   eax, BYTE PTR flag[rip]  ; 每次循环都读
        test    al, al
        je      .L3
        ret
```

**和 `volatile` 生成相同的汇编，但语义正确。**

---

## 4. 为什么 `volatile` 不够

### 4.1 `volatile` 解决了什么

- 告诉编译器：这个变量可能被外部修改（硬件、信号处理程序等）
- 禁止编译器缓存到寄存器
- 禁止编译器消除"看似无用"的读写

### 4.2 `volatile` 没有解决什么

**C++11 标准 §29.3/1 明确规定：**

> "volatile 类型的操作不提供线程同步保证。"

具体问题：

```
线程 A:                    线程 B:
data = 42;                if (ready) {
ready = true; (volatile)      use(data);
                          }
```

- `volatile` 保证 `ready` 真的写入内存
- 但**不保证写入顺序**：编译器/CPU 可能先写 `ready` 再写 `data`
- 线程 B 可能看到 `ready == true` 但 `data` 还是旧值

### 4.3 C++11 标准的明确说法

§29.3/3:

> "Note: volatile 不提供 happens-before 保证，不提供同步。它只保证访问不被编译器优化掉。"

---

## 5. `std::atomic` 解决了什么

### 5.1 编译器层面

```cpp
std::atomic<bool> flag{false};
```

告诉编译器：
1. **禁止缓存**：每次访问必须读/写内存
2. **禁止删除**：即使"看似无用"的读写也不能删除
3. **禁止重排**：根据 memory order 约束重排行为

### 5.2 语言层面

C++11 标准 §29.3/2：

> "原子对象上的操作是原子的...它们在不同线程上的执行不会导致数据竞争。"

**关键点：**
- 原子操作没有数据竞争
- 原子操作提供 happens-before 关系（取决于 memory order）
- 编译器必须遵守这些保证

### 5.3 `memory_order_relaxed` 的语义

```cpp
flag.store(true, std::memory_order_relaxed);
bool val = flag.load(std::memory_order_relaxed);
```

C++11 标准 §29.3/3：

> "relaxed 操作没有同步保证，只有原子性保证。"

具体来说：
- **原子性**：读到的值一定是某个完整写入的值（不会读到半写入的位）
- **可见性**：写入最终会可见（缓存一致性保证）
- **无顺序保证**：relaxed 操作前后的其他读写可以重排

**对于 `stop_` 标志，`relaxed` 就够了：**
- 我们只关心 `stop_` 本身是否变为 `true`
- 不关心 `stop_` 之前或之后的其他操作的顺序

---

## 6. 编译器如何实现 atomic

### 6.1 x86 架构

```cpp
// stop_.store(true, std::memory_order_relaxed)
// 编译为：
mov BYTE PTR [rip+stop_], 1

// stop_.load(std::memory_order_relaxed)
// 编译为：
movzx eax, BYTE PTR [rip+stop_]
```

**就是普通读写，但编译器知道不能优化掉。**

### 6.2 需要顺序保证时

```cpp
// stop_.store(true, std::memory_order_seq_cst)
// 编译为：
mov BYTE PTR [rip+stop_], 1
mfence  ; 内存屏障

// 或者用 xchg（隐含 lock 前缀）
xchg BYTE PTR [rip+stop_], 1
```

### 6.3 ARM 架构

```cpp
// stop_.load(std::memory_order_relaxed)
// 编译为：
ldrb w0, [x1]  ; 可能需要额外的 barrier 指令

// stop_.store(true, std::memory_order_relaxed)
// 编译为：
strb w0, [x1]
```

**ARM 的 relaxed 也可能需要屏障（比 x86 的 relaxed 贵）。**

---

## 7. 完整示例：你的线程池

### 7.1 有问题的版本

```cpp
class ThreadPool {
    bool stop_{false};  // 非原子
    
public:
    void stop() {
        stop_ = true;  // 写
    }
    
    void worker() {
        while (!stop_) {  // 读 - DATA RACE
            auto task = get_task();
            if (task) task();
        }
    }
};
```

**编译器可能的优化：**

```asm
worker():
        movzx   eax, BYTE PTR [rdi+offsetof(stop_)]  ; 读一次
        test    al, al
        je      .L_loop
        ret
.L_loop:
        ; get_task() 和 task() 的代码
        jmp     .L_loop  ; 直接跳回循环开始，不再检查 stop_
```

### 7.2 正确的版本

```cpp
class ThreadPool {
    std::atomic<bool> stop_{false};  // 原子
    
public:
    void stop() {
        stop_.store(true, std::memory_order_relaxed);
    }
    
    void worker() {
        while (!stop_.load(std::memory_order_relaxed)) {
            auto task = get_task();
            if (task) task();
        }
    }
};
```

**编译器保证：**
- 每次循环真的读取内存中的 `stop_`
- `stop_ = true` 最终会被工作线程看到
- 没有 data race，不是 UB

---

## 8. 标准条款索引

| 条款 | 内容 |
|------|------|
| §1.10 | 多线程执行和数据竞争 |
| §1.10/21 | 数据竞争的定义 |
| §1.10/22-24 | happens-before 关系 |
| §29.3 | 原子操作的顺序和同步 |
| §29.3/1 | volatile 不提供线程同步 |
| §29.3/2-3 | 原子操作的保证 |
| §29.3/11-14 | memory_order 的语义 |

---

## 9. 总结

| 问题 | 答案 |
|------|------|
| 编译器会缓存变量到寄存器吗？ | 会，如果它认为变量不会变 |
| 编译器知道多线程存在吗？ | 不知道，C++ 抽象机是单线程的 |
| `volatile` 能解决吗？ | 部分，但标准明确说不能用于多线程同步 |
| `atomic` 解决了什么？ | 编译器层面：禁止优化；语言层面：无 data race |
| x86 上 `relaxed` 有开销吗？ | 几乎为零，就是普通 `mov` |
| 其他架构呢？ | 可能需要额外屏障，但这是跨平台正确性的代价 |
