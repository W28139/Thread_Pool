#include "threadpool.h"
#include <iostream>

// --- Thread 类实现 ---

// 静态成员初始化
std::atomic_int Thread::generateId_{0};

Thread::Thread(ThreadFunc func)
    : func_(std::move(func)), 
      threadId_(generateId_++) 
{}

Thread::~Thread()
{
    if(thread_.joinable())
    {
        thread_.join();
    }
}
void Thread::start() {
    thread_ = std::thread(func_,threadId_);
}

int Thread::getId() const { 
    return threadId_; 
}

// --- ThreadPool 类实现 -----------------------------------------------------------

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
    
    // 此时所有线程都已经从 ThreadFunc 的循环中跳出，处于可 join 状态
    // 清空 map 会触发 unique_ptr<Thread> 的析构，进而触发 Thread::~Thread() 里的 join()
    threads_.clear(); 
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

void ThreadPool::cleanFinishThreads()
{
    std::lock_guard<std::mutex>exitlock(exitMtx_);
    for (int tid : exitedThreadIds_) {
        // 在这里 erase，会触发 Thread::~Thread() 执行 join()
        // 因为 ThreadFunc 已经执行完毕返回了，所以这里的 join 会立即成功，不会阻塞
        threads_.erase(tid); 
    }
    exitedThreadIds_.clear();
}
void ThreadPool::createThread() 
{
    // 在创建新线程前，先清理一下旧的“僵尸”线程对象
    cleanFinishThreads();

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
            while (taskQue_.empty()) 
            {
                if (!isPoolRunning_) 
                {
                    goto EXIT_HANDLER;
                }

                if (poolMode_ == PoolMode::MODE_CACHED) 
                {
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                        auto now = std::chrono::high_resolution_clock::now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) {
                            goto EXIT_HANDLER;
                        }
                    }
                } 
                else 
                {
                    notEmpty_.wait(lock, [&]() { return !taskQue_.empty() || !isPoolRunning_; });
                }
            }

            // 2. 取任务
            idleThreadSize_--;
            task = std::move(taskQue_.front());
            taskQue_.pop();
            taskSize_--;

            notFull_.notify_one();
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
        curThreadSize_--;
        idleThreadSize_--;
        //std::cout << "Thread TID: " << threadid << " exiting. Remaining: " << curThreadSize_ << std::endl;
        {
            std::lock_guard<std::mutex> exitLock(exitMtx_);
            exitedThreadIds_.push_back(threadid);
        }
        exitCond_.notify_all(); 
    }
}