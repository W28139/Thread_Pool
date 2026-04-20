这是一个为你准备的 GitHub `README.md` 文档模板。它涵盖了项目简介、核心特性、安装指南、接口说明以及快速上手示例。

---

# CppThreadPool

一个基于现代 C++ (C++11/14/17) 实现的轻量级、高性能线程池。支持任务异步提交、结果获取（Future机制）、动态扩容与缩容。

## 🚀 核心特性

- **泛型任务支持**：支持任何可调用对象（函数、Lambda 表达式、Bind 对象等）。
- **Future 机制**：提交任务后返回 `std::future`，方便在需要时阻塞或非阻塞地获取执行结果。
- **两种运行模式**：
  - `FIXED`：固定线程数量，适用于负载稳定的场景。
  - `CACHED`：根据任务量动态增加线程，并在空闲时自动回收，适用于突发流量场景。
- **资源限流**：支持设置任务队列上限和线程数上限，防止内存溢出或 CPU 过载。
- **安全析构**：在对象销毁时确保所有已分配任务执行完毕或线程安全退出。

## 📦 快速开始

### 1. 引入项目
将 `threadpool.h` 和 `threadpool.cpp` 添加到你的项目中。

### 2. 编译要求
- 支持 C++11 或更高版本的编译器 (如 g++ 4.8+, clang, MSVC 2015+)。
- 链接线程库（在 Linux 下需使用 `-pthread`）。

```bash
g++ threadpool.cpp your_main.cpp -o main -pthread
```

## 🛠 接口说明

### `ThreadPool` 类主要方法

| 方法 | 说明 |
| :--- | :--- |
| `setMode(PoolMode mode)` | 设置模式：`MODE_FIXED` 或 `MODE_CACHED`。 |
| `setTaskQueMaxThreshold(int)` | 设置任务队列最大容量（默认 1024）。 |
| `setThreadSizeThreshold(int)` | 设置 `CACHED` 模式下线程池的最大扩容上限。 |
| `start(int initSize)` | 启动线程池，设置初始核心线程数。 |
| `submitTask(Func&& func, Args&&... args)` | **核心接口**：提交任务，返回 `std::future<ReturnType>`。 |

---

## 💡 代码示例

### 1. 基础用法：获取返回值
```cpp
#include "threadpool.h"
#include <iostream>

int sum(int a, int b) { return a + b; }

int main() {
    ThreadPool pool;
    pool.start(4); // 开启4个核心线程

    // 提交任务
    auto result = pool.submitTask(sum, 10, 20);

    // 获取结果 (阻塞直到计算完成)
    std::cout << "Result: " << result.get() << std::endl;

    return 0;
}
```

### 2. Lambda 表达式与复杂类型
```cpp
auto res = pool.submitTask([](int limit) {
    int s = 0;
    for(int i=0; i<limit; i++) s += i;
    return s;
}, 100);

std::cout << "Sum Result: " << res.get() << std::endl;
```

### 3. CACHED 模式（动态扩容）
```cpp
ThreadPool pool;
pool.setMode(PoolMode::MODE_CACHED);
pool.setThreadSizeThreshold(10); // 最高扩容到10个线程
pool.start(2); // 初始2个线程

// 连续提交大量耗时任务，线程池将自动创建新线程处理
for(int i=0; i<20; i++) {
    pool.submitTask([](){ 
        std::this_thread::sleep_for(std::chrono::seconds(1)); 
    });
}
```

---

## ⚙️ 运行逻辑

1.  **任务提交**：当调用 `submitTask` 时，如果任务队列未满，任务被放入队列并通知空闲线程。
2.  **队列溢出**：若任务队列达到 `taskQueMaxThreshold`，提交操作将阻塞（最多 1 秒），若依然满载则返回一个包含默认值的空 Future。
3.  **动态扩容 (Cached)**：在 `CACHED` 模式下，若任务积压且当前线程数未达到 `threadSizeThreshold`，池体会自动创建新线程。
4.  **资源回收 (Cached)**：当线程空闲超过 10 秒且总线程数多于核心线程数时，多余的线程会自动退出。

## 📝 实现细节
- **线程通信**：使用 `std::condition_variable` 和 `std::mutex` 实现生产者-消费者模型。
- **任务封装**：通过 `std::packaged_task` 和 `std::shared_ptr` 实现对不同签名函数的统一管理。
- **生命周期**：利用 `std::atomic_bool` 标记运行状态，结合析构函数中的 `notify_all` 实现线程的平滑退出。

## 📄 开源协议
本项目遵循 MIT License 协议。