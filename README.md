# Custom ThreadPool - 基于 C++11 的高性能线程池

## 1. 项目简介

本项目是一个基于 C++11 编写的轻量级、高性能线程池。它采用了生产消费者模型，支持任务的异步提交与结果获取，并针对高并发场景设计了两种工作模式（固定模式与动态扩容模式）。

本项目不仅提供了最终的线程池实现，还完整记录了从零开始的开发演进过程。文件组织结构如下：

### 1. 开发演进 (docs/code/1-5)
包含项目从 1 到 5 阶段的递进式代码实现：
*   **阶段 1-2**：搭建基础框架，实现任务提交与固定数量线程的消费。
*   **阶段 3-4**：引入 `Any` 类型与 `Result` 机制，解决跨线程获取返回值的问题。
*   **阶段 5**：实现 `Cached` 模式，支持根据任务负载动态扩缩容。

### 2. 核心笔记 (docs/note)
与代码演进对应的详细技术文档：
*   **基础知识**：多线程编程的核心概念。
*   **实现细节**：逐步解析任务队列流控、信号量同步、原子操作、线程回收等核心模块的设计思路与原理。

### 3. 成品代码 (threadpool/)
*   项目最终的生产版本代码。
*   包含完整的 `threadpool.h`、`threadpool.cpp` 源码及单元测试 `test.cpp`。

---

## 2. 核心功能
*   **异步提交任务**：用户可以自定义任务类并提交给线程池异步执行。
*   **灵活的返回值获取**：通过自定义的 `Any` 类型和 `Result` 类，支持获取任务执行后的任意类型返回值。
*   **两种工作模式**：
    *   `MODE_FIXED` (固定模式)：线程数量固定，适用于负载稳定的场景。
    *   `MODE_CACHED` (缓存模式)：根据任务负载动态创建/销毁线程，提高资源利用率。
*   **任务队列流控**：支持设置任务队列上限，防止因任务过多导致内存溢出。
*   **资源自动回收**：完备的线程池销毁机制，确保所有任务处理完毕或线程安全退出。

## 3. 线程池特点
1.  **类型擦除**：利用自实现的 `Any` 类，打破了强类型限制，使得线程池可以处理返回任意类型的任务。
2.  **自定义 Result 机制**：类似于 `std::future`，用户调用 `result.get()` 时，若任务未完成会通过信号量（Semaphore）自动阻塞等待。
3.  **动态扩缩容**：在 `Cached` 模式下，当任务积压且有空闲配额时，线程池会自动增加线程；当线程空闲超过一定时间且超过初始线程数时，会自动回收。
4.  **线程安全**：综合运用 `std::mutex`、`std::condition_variable` 以及 `std::atomic` 原子变量，确保在多线程环境下任务分发和状态切换的准确性。

## 4. 主要接口说明

### ThreadPool 类
| 接口名称 | 功能描述 |
| :--- | :--- |
| `setMode(PoolMode mode)` | 设置线程池工作模式（Fixed / Cached） |
| `setInitThreadSize(int size)` | 设置初始线程数量 |
| `setTaskQueMaxThreshHold(int thresh)` | 设置任务队列的最大阈值（流控） |
| `setThreadSizeThreshHold(int thresh)` | 设置 Cached 模式下线程总数上限 |
| `start(int initSize)` | 启动线程池，开启工作线程 |
| `submitTask(shared_ptr<Task> sp)` | 提交任务，返回一个 `Result` 对象 |

### Task 类 (抽象基类)
*   **用户需继承此类**，并重写 `virtual Any run()` 方法来实现具体的业务逻辑。

### Result 类
*   **`get()`**：调用此方法将阻塞等待任务执行完成，并获取 `Any` 类型的返回值。

---

## 5. 使用示例

```cpp
#include "threadpool.h"
#include <iostream>

// 1. 自定义任务类
class MyTask : public Task {
public:
    MyTask(int a, int b) : a_(a), b_(b) {}

    // 2. 实现业务逻辑
    Any run() override {
        std::cout << "Thread " << std::this_thread::get_id() << " is working..." << std::endl;
        return a_ + b_; // 返回一个 int 结果
    }
private:
    int a_, b_;
};

int main() {
    // 3. 初始化线程池
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.start(4); // 初始 4 个线程

    // 4. 提交任务
    Result res = pool.submitTask(std::make_shared<MyTask>(10, 20));

    // 5. 获取结果 (会阻塞等待任务完成)
    int sum = res.get().cast_<int>();
    std::cout << "Task Result: " << sum << std::endl;

    return 0;
}
```

## 6. 技术栈
*   **语言标准**：C++11/14
*   **多线程协作**：`std::thread`, `std::mutex`, `std::condition_variable`, `std::unique_lock`
*   **设计模式**：生产者-消费者模型、单例模式思想（可选）、面向对象接口设计
*   **内存管理**：`std::unique_ptr`, `std::shared_ptr`, `std::move` 语义

## 7. 注意事项
*   任务返回值 `Any` 获取时，需调用 `cast_<T>()` 转换为实际类型，若类型不匹配会抛出异常。
*   在 `Cached` 模式下，线程的回收间隔可通过 `THREAD_MAX_IDLE_TIME` 常量进行调整。