
# CppThreadPool

一个基于现代 C++ (C++11/14/17) 实现的高性能线程池。支持任务异步提交、结果获取（Future机制）、动态扩容与缩容。

## 核心特性

- **泛型任务支持**：支持任何可调用对象。
- **Future 机制**：提交任务后返回 `std::future`，方便在需要时阻塞或非阻塞地获取执行结果。
- **两种运行模式**：
  - `FIXED`：固定线程数量，适用于负载稳定的场景。
  - `CACHED`：根据任务量动态增加线程，并在空闲时自动回收，适用于突发流量场景。


```bash
g++ threadpool.cpp your_main.cpp -o main -pthread
```

## 接口说明

### `ThreadPool` 类主要方法

| 方法 | 说明 |
| :--- | :--- |
| `setMode(PoolMode mode)` | 设置模式：`MODE_FIXED` 或 `MODE_CACHED`。 |
| `setTaskQueMaxThreshold(int)` | 设置任务队列最大容量（默认 1024）。 |
| `setThreadSizeThreshold(int)` | 设置 `CACHED` 模式下线程池的最大扩容上限。 |
| `start(int initSize)` | 启动线程池，设置初始核心线程数。 |
| `submitTask(Func&& func, Args&&... args)` | **核心接口**：提交任务，返回 `std::future<ReturnType>`。 |

---

## 压测结果(stress_test.cpp)
Starting Stress Test...
Mode: FIXED
Threads: 4, Tasks: 100000
Test Completed!
Time taken: 0.438749 seconds
Throughput: 227921 tasks/sec
------------------------------------------
Starting Stress Test...
Mode: FIXED
Threads: 28, Tasks: 2000
Test Completed!
Time taken: 0.0206524 seconds
Throughput: 96841 tasks/sec
------------------------------------------
Starting Stress Test...
Mode: CACHED
Threads: 2, Tasks: 5000
Test Completed!
Time taken: 0.0771733 seconds
Throughput: 64789.2 tasks/sec
------------------------------------------