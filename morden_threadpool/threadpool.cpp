#include "threadpool.h"
#include <iostream>

// --- Thread 类实现 ---

// 静态成员初始化
std::atomic_int Thread::generateId_{0};

Thread::Thread(ThreadFunc func)
    : func_(std::move(func)), 
      threadId_(generateId_++) 
{}

void Thread::start() {
    std::thread t(func_, threadId_);
    t.detach(); 
}

int Thread::getId() const { 
    return threadId_; 
}

// --- ThreadPool 类实现 ---

ThreadPool::ThreadPool()
    : initThreadSize_(4),
      taskSize_(0),
      curThreadSize_(0),
      idleThreadSize_(0),
      taskQueMaxThreshold_(TASK_MAX_THRESHOLD),
      threadSizeThreshold_(THREAD_MAX_THRESHOLD),
      poolMode_(PoolMode::MODE_FIXED),
      isPoolRunning_(false) 
{}

ThreadPool::~ThreadPool() {
    isPoolRunning_ = false;

    // 唤醒所有等待任务的线程
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();
    
    // 等待所有线程真正退出
    exitCond_.wait(lock, [&]() { return curThreadSize_ == 0; });
}

void ThreadPool::setMode(PoolMode mode) {
    if (isPoolRunning_) return;
    poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshold(int threshold) {
    if (isPoolRunning_) return;
    taskQueMaxThreshold_ = threshold;
}

void ThreadPool::setThreadSizeThreshold(int threshold) {
    if (isPoolRunning_) return;
    if (poolMode_ == PoolMode::MODE_CACHED) {
        threadSizeThreshold_ = threshold;
    }
}

void ThreadPool::start(int initThreadSize) {
    isPoolRunning_ = true;
    initThreadSize_ = initThreadSize;

    std::unique_lock<std::mutex> lock(taskQueMtx_);
    for (int i = 0; i < initThreadSize_; i++) {
        createThread();
    }
}

void ThreadPool::createThread() {
    auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
    int tid = ptr->getId();
    threads_.emplace(tid, std::move(ptr));
    threads_[tid]->start();
    curThreadSize_++;
    idleThreadSize_++;
}

void ThreadPool::ThreadFunc(int threadid) {
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            // 1. 等待任务或退出信号
            while (taskQue_.empty()) {
                if (!isPoolRunning_) {
                    goto EXIT_HANDLER;
                }

                if (poolMode_ == PoolMode::MODE_CACHED) {
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                        auto now = std::chrono::high_resolution_clock::now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) {
                            goto EXIT_HANDLER;
                        }
                    }
                } 
                else {
                    notEmpty_.wait(lock, [&]() { return !taskQue_.empty() || !isPoolRunning_; });
                }
            }

            // 2. 取任务
            idleThreadSize_--;
            task = std::move(taskQue_.front());
            taskQue_.pop();
            taskSize_--;

            notFull_.notify_all();
        } 

        // 3. 锁外执行任务
        if (task) {
            task();
        }

        lastTime = std::chrono::high_resolution_clock::now();
        idleThreadSize_++;
    }

// 执行 goto EXIT_HANDLER后的代码
EXIT_HANDLER:
    {
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        threads_.erase(threadid); 
        curThreadSize_--;
        idleThreadSize_--;
        std::cout << "Thread TID: " << threadid << " exiting. Remaining: " << curThreadSize_ << std::endl;
        exitCond_.notify_all(); 
    }
}