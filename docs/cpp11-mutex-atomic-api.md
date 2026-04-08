# C++11 多线程编程：mutex 与 atomic 完全指南

C++11 引入了标准的多线程支持库，其中 `<mutex>` 和 `<atomic>` 是并发编程的两大基石。mutex 通过互斥锁保护共享数据，atomic 则提供无锁的原子操作。本文档详细介绍这两个库的核心接口，配合可运行的代码示例。

---

## Part 1: mutex 相关接口

### 1. std::mutex

`std::mutex` 是最基本的互斥量类型，提供独占式的互斥访问。

#### 接口说明

| 方法 | 说明 |
|------|------|
| `lock()` | 加锁，阻塞直到获取锁 |
| `unlock()` | 解锁 |
| `try_lock()` | 尝试加锁，立即返回，成功返回 true |

#### 基本用法

```cpp
#include <iostream>
#include <thread>
#include <mutex>

std::mutex mtx;
int shared_counter = 0;

void increment(int n) {
    for (int i = 0; i < n; ++i) {
        mtx.lock();
        ++shared_counter;
        mtx.unlock();
    }
}

int main() {
    std::thread t1(increment, 10000);
    std::thread t2(increment, 10000);
    
    t1.join();
    t2.join();
    
    std::cout << "counter = " << shared_counter << std::endl;
    return 0;
}
```

#### try_lock 用法

```cpp
#include <iostream>
#include <thread>
#include <mutex>

std::mutex mtx;

void try_lock_example() {
    if (mtx.try_lock()) {
        std::cout << "获取锁成功" << std::endl;
        mtx.unlock();
    } else {
        std::cout << "锁被占用，稍后重试" << std::endl;
    }
}
```

#### 注意事项

- **不要重复加锁**：同一个线程对非递归 mutex 重复加锁会导致未定义行为
- **不要忘记解锁**：手动 lock() 必须对应 unlock()，建议使用 RAII 包装
- **不要在未加锁时解锁**：对未持有的 mutex 调用 unlock() 是未定义行为

---

### 2. std::lock_guard

`std::lock_guard` 是 RAII 风格的锁包装器，构造时加锁，析构时自动解锁。

#### 适用场景

- 保护整个作用域内的共享数据
- 不需要手动管理锁的生命周期
- 异常安全：即使抛出异常也能正确解锁

#### 基本用法

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>

std::mutex mtx;
int shared_data = 0;

void safe_increment(int n) {
    for (int i = 0; i < n; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        ++shared_data;
        // 离开作用域时自动解锁
    }
}

int main() {
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(safe_increment, 2500);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "shared_data = " << shared_data << std::endl;
    return 0;
}
```

#### 异常安全示例

```cpp
void process_data() {
    std::lock_guard<std::mutex> lock(mtx);
    
    // 即使这里抛出异常，lock_guard 析构时也会解锁
    if (some_condition) {
        throw std::runtime_error("error");
    }
    
    shared_data = 42;
}
```

---

### 3. std::unique_lock

`std::unique_lock` 比 `lock_guard` 更灵活，支持延迟加锁、手动解锁、条件变量等场景。

#### 与 lock_guard 的区别

| 特性 | lock_guard | unique_lock |
|------|------------|-------------|
| 灵活性 | 低，构造即加锁 | 高，可延迟加锁 |
| 性能 | 略高 | 略低（需要记录锁状态） |
| 条件变量 | 不支持 | 支持 |
| 手动解锁 | 不支持 | 支持 |

#### 基本用法

```cpp
#include <iostream>
#include <thread>
#include <mutex>

std::mutex mtx;

void unique_lock_example() {
    std::unique_lock<std::mutex> lock(mtx);  // 构造时加锁
    // 临界区
    std::cout << "持有锁" << std::endl;
    
    lock.unlock();  // 手动解锁
    
    // 不持有锁的区域
    std::some_heavy_computation();
    
    lock.lock();  // 重新加锁
    // 再次进入临界区
}
```

#### 延迟加锁

```cpp
void deferred_lock_example() {
    std::unique_lock<std::mutex> lock(mtx, std::defer_lock);
    // 此时未加锁
    
    // 做一些不需要锁的工作
    prepare_data();
    
    lock.lock();  // 现在加锁
    // 临界区
}
```

#### try_lock_for (配合 timed_mutex)

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>

std::timed_mutex tmtx;

void try_lock_for_example() {
    std::unique_lock<std::timed_mutex> lock(tmtx, std::defer_lock);
    
    // 尝试在 100ms 内获取锁
    if (lock.try_lock_for(std::chrono::milliseconds(100))) {
        std::cout << "获取锁成功" << std::endl;
    } else {
        std::cout << "获取锁超时" << std::endl;
    }
}
```

#### 所有权转移

```cpp
std::unique_lock<std::mutex> create_lock() {
    std::unique_lock<std::mutex> lock(mtx);
    return lock;  // 移动语义，所有权转移
}
```

---

### 4. std::lock()

`std::lock()` 可以同时锁住多个 mutex，避免死锁问题。

#### 死锁问题

```cpp
// 错误示例：可能导致死锁
std::mutex m1, m2;

void thread_a() {
    m1.lock();
    m2.lock();  // 如果 thread_b 先锁了 m2，就会死锁
    // ...
    m2.unlock();
    m1.unlock();
}

void thread_b() {
    m2.lock();
    m1.lock();  // 如果 thread_a 先锁了 m1，就会死锁
    // ...
    m1.unlock();
    m2.unlock();
}
```

#### 使用 std::lock 避免死锁

```cpp
#include <iostream>
#include <thread>
#include <mutex>

std::mutex m1, m2;
int data1 = 0, data2 = 0;

void safe_transfer() {
    std::lock(m1, m2);  // 同时锁住两个 mutex，不会死锁
    
    std::lock_guard<std::mutex> lock1(m1, std::adopt_lock);
    std::lock_guard<std::mutex> lock2(m2, std::adopt_lock);
    
    // 两个锁都已持有
    ++data1;
    ++data2;
}
```

#### C++17: std::scoped_lock

C++17 引入了 `std::scoped_lock`，简化了上述写法：

```cpp
// C++17
void safe_transfer_cpp17() {
    std::scoped_lock lock(m1, m2);  // 自动同时加锁
    ++data1;
    ++data2;
}
```

---

### 5. std::condition_variable

`std::condition_variable` 用于线程间的同步，通常配合 `unique_lock` 使用。

#### 接口说明

| 方法 | 说明 |
|------|------|
| `wait(lock)` | 阻塞等待，释放锁，被唤醒后重新加锁 |
| `wait(lock, pred)` | 带谓词的等待，相当于 `while(!pred()) wait(lock)` |
| `notify_one()` | 唤醒一个等待的线程 |
| `notify_all()` | 唤醒所有等待的线程 |

#### 基本用法

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>

std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, []{ return ready; });  // 等待 ready 变为 true
    
    std::cout << "Worker 开始工作" << std::endl;
}

void master() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();  // 通知 worker
    
    std::cout << "Master 发送通知" << std::endl;
}

int main() {
    std::thread t1(worker);
    std::thread t2(master);
    
    t1.join();
    t2.join();
    return 0;
}
```

#### 生产者-消费者示例

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

std::mutex mtx;
std::condition_variable cv;
std::queue<int> data_queue;
const int MAX_SIZE = 10;
bool finished = false;

void producer(int id) {
    for (int i = 0; i < 20; ++i) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // 等待队列有空间
        cv.wait(lock, []{ return data_queue.size() < MAX_SIZE; });
        
        data_queue.push(i);
        std::cout << "生产者 " << id << " 生产: " << i << std::endl;
        
        lock.unlock();
        cv.notify_all();  // 通知消费者
    }
}

void consumer(int id) {
    while (true) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // 等待队列有数据或生产结束
        cv.wait(lock, []{ return !data_queue.empty() || finished; });
        
        if (data_queue.empty() && finished) {
            break;  // 生产结束且队列为空，退出
        }
        
        int value = data_queue.front();
        data_queue.pop();
        std::cout << "消费者 " << id << " 消费: " << value << std::endl;
        
        lock.unlock();
        cv.notify_all();  // 通知生产者
    }
}

int main() {
    std::thread producers[2];
    std::thread consumers[2];
    
    for (int i = 0; i < 2; ++i) {
        producers[i] = std::thread(producer, i);
        consumers[i] = std::thread(consumer, i);
    }
    
    for (auto& t : producers) {
        t.join();
    }
    
    {
        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
    }
    cv.notify_all();
    
    for (auto& t : consumers) {
        t.join();
    }
    
    return 0;
}
```

---

### 6. std::shared_mutex (C++17)

`std::shared_mutex` 支持共享锁（读锁）和独占锁（写锁），适用于读多写少的场景。

#### 读写锁示例

```cpp
// C++17
#include <iostream>
#include <thread>
#include <shared_mutex>
#include <vector>

std::shared_mutex smtx;
int shared_value = 0;

// 读操作：多个线程可以同时读取
void reader(int id) {
    std::shared_lock<std::shared_mutex> lock(smtx);
    std::cout << "Reader " << id << ": " << shared_value << std::endl;
}

// 写操作：独占访问
void writer(int id, int value) {
    std::unique_lock<std::shared_mutex> lock(smtx);
    shared_value = value;
    std::cout << "Writer " << id << " 写入: " << value << std::endl;
}
```

---

## Part 2: atomic 相关接口

### 1. std::atomic<T> 基本操作

`std::atomic<T>` 提供了原子操作，无需加锁即可保证线程安全。

#### 基本接口

```cpp
#include <iostream>
#include <thread>
#include <atomic>

std::atomic<int> counter{0};

void atomic_basic_example() {
    // load: 读取值
    int val = counter.load();
    
    // store: 写入值
    counter.store(42);
    
    // exchange: 交换并返回旧值
    int old = counter.exchange(100);
    
    // fetch_add: 原子加法，返回旧值
    int before = counter.fetch_add(10);  // counter += 10, 返回加之前的值
    
    // fetch_sub: 原子减法，返回旧值
    int after = counter.fetch_sub(5);    // counter -= 5, 返回减之前的值
    
    // 运算符重载
    counter++;     // 原子自增
    counter--;     // 原子自减
    counter += 3;  // 原子加
    counter -= 2;  // 原子减
}
```

#### 多线程计数示例

```cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

std::atomic<int> atomic_counter{0};

void increment_atomic(int n) {
    for (int i = 0; i < n; ++i) {
        atomic_counter++;  // 原子操作，无需加锁
    }
}

int main() {
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(increment_atomic, 10000);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "atomic_counter = " << atomic_counter << std::endl;
    // 输出: atomic_counter = 40000
    return 0;
}
```

---

### 2. CAS 操作 (Compare-And-Swap)

CAS 是无锁编程的核心操作，用于实现乐观并发控制。

#### compare_exchange_strong

```cpp
#include <iostream>
#include <atomic>

std::atomic<int> value{0};

void strong_cas_example() {
    int expected = 0;
    int desired = 42;
    
    // 如果 value == expected，则 value = desired，返回 true
    // 否则 expected = value 的当前值，返回 false
    bool success = value.compare_exchange_strong(expected, desired);
    
    if (success) {
        std::cout << "CAS 成功，value = " << value << std::endl;
    } else {
        std::cout << "CAS 失败，expected = " << expected << std::endl;
    }
}
```

#### compare_exchange_weak

```cpp
void weak_cas_example() {
    int expected = 0;
    int desired = 42;
    
    // weak 版本可能虚假失败（spurious failure），即使 expected == value 也可能返回 false
    // 通常需要在循环中使用
    while (!value.compare_exchange_weak(expected, desired)) {
        // expected 会被更新为当前值
        // 可以在这里更新 desired
    }
}
```

#### CAS 实现无锁累加

```cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

std::atomic<int> lock_free_counter{0};

void lock_free_increment(int n) {
    for (int i = 0; i < n; ++i) {
        int current = lock_free_counter.load();
        // 使用 CAS 循环直到成功
        while (!lock_free_counter.compare_exchange_weak(current, current + 1)) {
            // current 会被更新为最新值
        }
    }
}

int main() {
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(lock_free_increment, 10000);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "lock_free_counter = " << lock_free_counter << std::endl;
    return 0;
}
```

#### strong 与 weak 的区别

| 特性 | compare_exchange_strong | compare_exchange_weak |
|------|------------------------|----------------------|
| 虚假失败 | 无 | 可能有 |
| 性能 | 可能较慢 | 可能较快 |
| 使用场景 | 不需要循环 | 通常需要循环 |

---

### 3. std::atomic<bool>

`std::atomic<bool>` 常用作线程间的标志位。

#### 作为标志位使用

```cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

std::atomic<bool> stop_flag{false};

void worker() {
    while (!stop_flag.load()) {
        // 执行工作
        std::cout << "工作中..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "收到停止信号，退出" << std::endl;
}

int main() {
    std::thread t(worker);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop_flag.store(true);  // 设置停止标志
    
    t.join();
    return 0;
}
```

#### 一次性初始化标志

```cpp
#include <iostream>
#include <thread>
#include <atomic>

std::atomic<bool> initialized{false};
int shared_resource = 0;

void init_once() {
    bool expected = false;
    // 只有一个线程能成功设置为 true
    if (initialized.compare_exchange_strong(expected, true)) {
        shared_resource = 42;
        std::cout << "初始化完成" << std::endl;
    }
}

int main() {
    std::thread t1(init_once);
    std::thread t2(init_once);
    std::thread t3(init_once);
    
    t1.join();
    t2.join();
    t3.join();
    
    std::cout << "shared_resource = " << shared_resource << std::endl;
    return 0;
}
```

---

### 4. std::atomic<T*>

`std::atomic<T*>` 支持指针的原子操作。

#### 指针原子操作示例

```cpp
#include <iostream>
#include <atomic>

int buffer[100];
std::atomic<int*> ptr{buffer};

void atomic_pointer_example() {
    // load: 原子读取指针
    int* p = ptr.load();
    
    // store: 原子写入指针
    ptr.store(buffer + 10);
    
    // fetch_add: 指针算术，原子移动指针
    int* old = ptr.fetch_add(5);  // ptr += 5, 返回旧值
    
    // fetch_sub: 指针算术
    ptr.fetch_sub(3);  // ptr -= 3
    
    // 运算符重载
    ptr++;  // 原子自增
    ptr--;  // 原子自减
    ptr += 2;  // 原子加
}
```

#### 无锁栈示例

```cpp
#include <iostream>
#include <atomic>

struct Node {
    int data;
    Node* next;
    
    Node(int d) : data(d), next(nullptr) {}
};

class LockFreeStack {
    std::atomic<Node*> head{nullptr};
    
public:
    void push(int value) {
        Node* new_node = new Node(value);
        new_node->next = head.load();
        
        // CAS 循环：如果 head 没变，就设置 head = new_node
        while (!head.compare_exchange_weak(new_node->next, new_node)) {
            // new_node->next 会被更新为当前 head
        }
    }
    
    bool pop(int& value) {
        Node* old_head = head.load();
        
        while (old_head && !head.compare_exchange_weak(old_head, old_head->next)) {
            // old_head 会被更新为当前 head
        }
        
        if (old_head) {
            value = old_head->data;
            delete old_head;
            return true;
        }
        return false;
    }
};
```

---

### 5. 内存序 (memory_order)

内存序控制原子操作的同步和排序行为，是高性能并发编程的关键。

#### memory_order_relaxed

最宽松的内存序，只保证原子性，不保证顺序。

```cpp
#include <iostream>
#include <thread>
#include <atomic>

std::atomic<int> x{0}, y{0};
std::atomic<int> r1{0}, r2{0};

void thread1() {
    x.store(1, std::memory_order_relaxed);
    r1.store(y.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void thread2() {
    y.store(1, std::memory_order_relaxed);
    r2.store(x.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

// 可能出现 r1 == 0 && r2 == 0
// 因为 relaxed 不保证 store 和 load 的顺序
```

**适用场景**：计数器、统计信息等不需要同步其他数据的情况。

#### memory_order_acquire / memory_order_release

acquire-release 语义：release 操作之前的所有写入对 acquire 该原子变量的线程可见。

```cpp
#include <iostream>
#include <thread>
#include <atomic>

std::atomic<bool> ready{false};
int data = 0;

void producer() {
    data = 42;  // 1. 写入数据
    ready.store(true, std::memory_order_release);  // 2. release: 保证 1 在 2 之前完成
}

void consumer() {
    // 3. acquire: 等待 ready 变为 true
    while (!ready.load(std::memory_order_acquire)) {
        // 自旋等待
    }
    // 4. 保证能看到 data = 42
    std::cout << "data = " << data << std::endl;
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);
    
    t1.join();
    t2.join();
    return 0;
}
```

**适用场景**：发布-订阅模式、一次性初始化、消息传递。

#### memory_order_acq_rel

同时具有 acquire 和 release 语义，用于 read-modify-write 操作。

```cpp
#include <iostream>
#include <thread>
#include <atomic>

std::atomic<int> shared_value{0};

void read_modify_write() {
    int old = shared_value.load(std::memory_order_acquire);
    int new_val;
    
    do {
        new_val = old + 1;
        // acq_rel: 读取时 acquire，写入时 release
    } while (!shared_value.compare_exchange_weak(
        old, new_val, 
        std::memory_order_acq_rel, 
        std::memory_order_acquire
    ));
}
```

**适用场景**：需要同时读取和修改原子变量，并保证同步。

#### memory_order_seq_cst

顺序一致性，最强的内存序，也是默认的内存序。

```cpp
#include <iostream>
#include <thread>
#include <atomic>

std::atomic<int> x{0}, y{0};
std::atomic<int> r1{0}, r2{0};

void thread1() {
    x.store(1, std::memory_order_seq_cst);
    r1.store(y.load(std::memory_order_seq_cst), std::memory_order_seq_cst);
}

void thread2() {
    y.store(1, std::memory_order_seq_cst);
    r2.store(x.load(std::memory_order_seq_cst), std::memory_order_seq_cst);
}

// 不可能出现 r1 == 0 && r2 == 0
// 因为 seq_cst 保证全局顺序
```

**适用场景**：需要最强保证的情况，或者不确定该用哪种内存序时。

#### 内存序对比

| 内存序 | 原子性 | 顺序保证 | 性能 | 使用场景 |
|--------|--------|----------|------|----------|
| relaxed | 有 | 无 | 最高 | 独立计数器 |
| acquire | 有 | 读屏障 | 高 | 读取同步数据 |
| release | 有 | 写屏障 | 高 | 发布数据 |
| acq_rel | 有 | 读写屏障 | 中 | read-modify-write |
| seq_cst | 有 | 全局顺序 | 最低 | 需要最强保证 |

---

### 6. std::atomic_flag

`std::atomic_flag` 是最简单的原子类型，保证无锁实现。

#### 接口说明

| 方法 | 说明 |
|------|------|
| `test_and_set()` | 设置为 true，返回旧值 |
| `clear()` | 设置为 false |

#### 实现自旋锁

```cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    
public:
    void lock() {
        // 自旋等待：test_and_set 返回 true 表示锁被占用
        while (flag.test_and_set(std::memory_order_acquire)) {
            // 自旋等待，可以加入 pause 指令优化
        }
    }
    
    void unlock() {
        flag.clear(std::memory_order_release);
    }
};

SpinLock spin_lock;
int shared_data = 0;

void increment_with_spinlock(int n) {
    for (int i = 0; i < n; ++i) {
        spin_lock.lock();
        ++shared_data;
        spin_lock.unlock();
    }
}

int main() {
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(increment_with_spinlock, 10000);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "shared_data = " << shared_data << std::endl;
    return 0;
}
```

#### 自旋锁 vs 互斥锁

| 特性 | 自旋锁 | 互斥锁 |
|------|--------|--------|
| 等待方式 | 忙等待（CPU 空转） | 阻塞（让出 CPU） |
| 适用场景 | 临界区很短 | 临界区较长 |
| 性能 | 低竞争时快 | 高竞争时好 |
| 实现 | 简单 | 复杂 |

---

## Part 3: 实际示例

### 1. 单例模式

#### 双重检查锁定 (DCLP)

```cpp
#include <iostream>
#include <thread>
#include <mutex>

class Singleton {
private:
    static Singleton* instance;
    static std::mutex mtx;
    
    Singleton() { std::cout << "Singleton 构造" << std::endl; }
    
public:
    static Singleton* getInstance() {
        if (instance == nullptr) {  // 第一次检查
            std::lock_guard<std::mutex> lock(mtx);
            if (instance == nullptr) {  // 第二次检查
                instance = new Singleton();
            }
        }
        return instance;
    }
};

Singleton* Singleton::instance = nullptr;
std::mutex Singleton::mtx;
```

#### C++11 静态局部变量（推荐）

```cpp
#include <iostream>
#include <thread>

class Singleton {
private:
    Singleton() { std::cout << "Singleton 构造" << std::endl; }
    
public:
    static Singleton& getInstance() {
        // C++11 保证静态局部变量的初始化是线程安全的
        static Singleton instance;
        return instance;
    }
    
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
};
```

#### atomic 实现的单例

```cpp
#include <iostream>
#include <thread>
#include <atomic>

class Singleton {
private:
    static std::atomic<Singleton*> instance;
    static std::mutex mtx;
    
    Singleton() { std::cout << "Singleton 构造" << std::endl; }
    
public:
    static Singleton* getInstance() {
        Singleton* tmp = instance.load(std::memory_order_acquire);
        
        if (tmp == nullptr) {
            std::lock_guard<std::mutex> lock(mtx);
            tmp = instance.load(std::memory_order_relaxed);
            
            if (tmp == nullptr) {
                tmp = new Singleton();
                instance.store(tmp, std::memory_order_release);
            }
        }
        return tmp;
    }
};

std::atomic<Singleton*> Singleton::instance{nullptr};
std::mutex Singleton::mtx;
```

---

### 2. 引用计数

使用 atomic 实现线程安全的引用计数。

```cpp
#include <iostream>
#include <thread>
#include <atomic>

template<typename T>
class SharedPtr {
private:
    T* ptr;
    std::atomic<int>* ref_count;
    
public:
    explicit SharedPtr(T* p = nullptr) : ptr(p), ref_count(nullptr) {
        if (ptr) {
            ref_count = new std::atomic<int>(1);
        }
    }
    
    SharedPtr(const SharedPtr& other) : ptr(other.ptr), ref_count(other.ref_count) {
        if (ref_count) {
            ref_count->fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    SharedPtr& operator=(const SharedPtr& other) {
        if (this != &other) {
            release();
            ptr = other.ptr;
            ref_count = other.ref_count;
            if (ref_count) {
                ref_count->fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }
    
    ~SharedPtr() {
        release();
    }
    
    T* get() const { return ptr; }
    T& operator*() const { return *ptr; }
    T* operator->() const { return ptr; }
    
    int use_count() const {
        return ref_count ? ref_count->load(std::memory_order_relaxed) : 0;
    }
    
private:
    void release() {
        if (ref_count) {
            // fetch_sub 返回旧值，如果旧值为 1，说明这是最后一个引用
            if (ref_count->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete ptr;
                delete ref_count;
            }
        }
    }
};

// 测试
int main() {
    SharedPtr<int> p1(new int(42));
    std::cout << "use_count = " << p1.use_count() << std::endl;  // 1
    
    {
        SharedPtr<int> p2 = p1;
        std::cout << "use_count = " << p1.use_count() << std::endl;  // 2
    }
    
    std::cout << "use_count = " << p1.use_count() << std::endl;  // 1
    return 0;
}
```

---

### 3. 生产者-消费者队列

使用 mutex + condition_variable 实现线程安全的队列。

```cpp
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>

template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_not_empty_;
    std::condition_variable cond_not_full_;
    size_t capacity_;
    bool closed_ = false;
    
public:
    explicit ThreadSafeQueue(size_t capacity = 0) : capacity_(capacity) {}
    
    // 阻塞式入队
    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 如果有容量限制，等待有空间
        if (capacity_ > 0) {
            cond_not_full_.wait(lock, [this] {
                return queue_.size() < capacity_ || closed_;
            });
        }
        
        if (closed_) return false;
        
        queue_.push(item);
        cond_not_empty_.notify_one();
        return true;
    }
    
    // 阻塞式出队
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        cond_not_empty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });
        
        if (queue_.empty()) return std::nullopt;
        
        T item = queue_.front();
        queue_.pop();
        cond_not_full_.notify_one();
        return item;
    }
    
    // 非阻塞式出队
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.empty()) return std::nullopt;
        
        T item = queue_.front();
        queue_.pop();
        cond_not_full_.notify_one();
        return item;
    }
    
    // 关闭队列
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cond_not_empty_.notify_all();
        cond_not_full_.notify_all();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
};

// 测试
int main() {
    ThreadSafeQueue<int> queue(10);
    
    // 生产者
    std::thread producer([&queue] {
        for (int i = 0; i < 20; ++i) {
            queue.push(i);
            std::cout << "生产: " << i << std::endl;
        }
        queue.close();
    });
    
    // 消费者
    std::thread consumer([&queue] {
        while (true) {
            auto item = queue.pop();
            if (!item) break;
            std::cout << "消费: " << *item << std::endl;
        }
    });
    
    producer.join();
    consumer.join();
    
    return 0;
}
```

---

### 4. 自旋锁

使用 atomic_flag 实现高效的自旋锁。

```cpp
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

class SpinLock {
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    
public:
    void lock() {
        // 指数退避的自旋锁
        int spin_count = 0;
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // 指数退避：逐渐增加等待时间
            if (spin_count < 10) {
                // 短暂自旋
                for (int i = 0; i < (1 << spin_count); ++i) {
                    // CPU pause 指令（x86）
                    #if defined(__x86_64__) || defined(_M_X64)
                        __asm__ __volatile__("pause");
                    #endif
                }
                ++spin_count;
            } else {
                // 让出 CPU 时间片
                std::this_thread::yield();
            }
        }
    }
    
    void unlock() {
        flag_.clear(std::memory_order_release);
    }
    
    // 尝试加锁
    bool try_lock() {
        return !flag_.test_and_set(std::memory_order_acquire);
    }
};

// 测试
SpinLock spin_lock;
int counter = 0;

void increment(int n) {
    for (int i = 0; i < n; ++i) {
        spin_lock.lock();
        ++counter;
        spin_lock.unlock();
    }
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(increment, 100000);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "counter = " << counter << std::endl;
    std::cout << "耗时: " << duration.count() << "ms" << std::endl;
    
    return 0;
}
```

---

## 总结

### mutex 选择指南

| 场景 | 推荐 |
|------|------|
| 简单临界区 | `lock_guard<mutex>` |
| 需要手动解锁 | `unique_lock<mutex>` |
| 条件变量 | `unique_lock<mutex>` |
| 多个锁 | `std::lock()` 或 `scoped_lock` (C++17) |
| 读多写少 | `shared_mutex` (C++17) |

### atomic 选择指南

| 场景 | 推荐 |
|------|------|
| 简单计数 | `atomic<int>` + `fetch_add` |
| 标志位 | `atomic<bool>` |
| 无锁数据结构 | CAS 操作 |
| 高性能 | 选择合适的 memory_order |

### 内存序选择指南

| 场景 | 推荐 |
|------|------|
| 不确定 | `memory_order_seq_cst` (默认) |
| 独立计数器 | `memory_order_relaxed` |
| 发布-订阅 | `memory_order_release` + `memory_order_acquire` |
| read-modify-write | `memory_order_acq_rel` |

---

## 参考资料

- [cppreference - mutex](https://en.cppreference.com/w/cpp/header/mutex)
- [cppreference - atomic](https://en.cppreference.com/w/cpp/header/atomic)
- [C++ Concurrency in Action](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
