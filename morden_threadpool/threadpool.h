#pragma once
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <future>

// 常量定义
const int TASK_MAX_THRESHOLD = 1024;
const int THREAD_MAX_THRESHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10; // 秒

// 线程池支持的模式
enum class PoolMode {
    MODE_FIXED,   // 固定数量线程
    MODE_CACHED,  // 动态增长线程
};

// 线程类声明
class Thread {
public:
    using ThreadFunc = std::function<void(int)>;
    Thread(ThreadFunc func);
    ~Thread() = default;
    void start();
    int getId() const;

private:
    ThreadFunc func_;
    static std::atomic_int generateId_;
    int threadId_;
};

// 线程池类声明
class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();

    // 设置模式
    void setMode(PoolMode mode);
    // 设置任务队列阈值
    void setTaskQueMaxThreshold(int threshold);
    // 设置Cached模式下线程上限
    void setThreadSizeThreshold(int threshold);

    // 开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency());

    // 提交任务：模板函数必须定义在头文件中
    template<typename Func, typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
        using RType = decltype(func(args...));
        
        auto task = std::make_shared<std::packaged_task<RType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );
        // 获取该任务的 future对象,一个任务只能获取一次,利用该对象调用结果
        std::future<RType> result = task->get_future();

        std::unique_lock<std::mutex> lock(taskQueMtx_);
        // 等待队列不满（最多等待1秒）
        if (!notFull_.wait_for(lock, std::chrono::seconds(1), 
            [&]() { return taskQue_.size() < (size_t)taskQueMaxThreshold_; })) {
            
            // 提交失败逻辑
            auto emptyTask = std::make_shared<std::packaged_task<RType()>>([]() { return RType(); });
            (*emptyTask)();
            return emptyTask->get_future();
        }

        // 入队封装好的闭包
        taskQue_.emplace([task]() { (*task)(); });
        taskSize_++;

        notEmpty_.notify_all();

        // Cached模式扩容判断
        if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshold_) {
            createThread();
        }

        return result;
    }

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    void ThreadFunc(int threadid);
    void createThread();

private:
    // 线程与任务相关
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;
    int initThreadSize_;
    int threadSizeThreshold_;
    std::atomic_int curThreadSize_;
    std::atomic_int idleThreadSize_;

    using Task = std::function<void()>;
    std::queue<Task> taskQue_;
    std::atomic_int taskSize_;
    int taskQueMaxThreshold_;

    // 同步与状态
    std::mutex taskQueMtx_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::condition_variable exitCond_;

    PoolMode poolMode_;
    std::atomic_bool isPoolRunning_;
};