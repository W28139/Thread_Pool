#pragma once
#include<vector>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<functional>
#include<condition_variable>
#include<iostream>
#include<functional>
#include<thread>
#include<chrono>

// 任务抽象基类
class Task
{
public:
	// 用户可以自定义任意类型，从Task继承，重写run方法，实现自定义任务处理
	virtual void run() = 0;
};

// 线程池支持的两种模式
enum class PoolMode
{
	MODE_FIXED,	// 固定数量的线程
	MODE_CACHED,	// 线程数量可动态增长
};

// 线程类
class Thread
{
private:
	
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void()>;
	// 线程构造
	Thread(ThreadFunc func);
	// 线程析构
	~Thread();
	// 启动线程
	void start();

private:
	ThreadFunc func_;
};

// 线程池类
class ThreadPool
{
private:
	std::vector<std::unique_ptr<Thread>> threads_;	// 线程列表
	size_t initThreadSize_;		// 初始的线程数量

	// 由于传入对象生命周期不确定，因此要用智能指针
	std::queue<std::shared_ptr<Task>> taskQue_;	// 任务队列
	std::atomic_int taskSize_;	// 表示任务的数量，用原子变量，保证统一
	int taskQueMaxThreshHold_;	// 任务队列数量上限的阈值
	
	std::mutex taskQueMtx_;		// 保证任务队列的线程安全
	std::condition_variable notFull_;	// 表示任务队列不满
	std::condition_variable notEmpty_;	// 表示任务队列不空
	PoolMode poolMode_;			// 当前线程池的工作模式
	

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	// 定义线程函数  线程池的所有线程从任务队列里面消费任务
	void ThreadFunc();

public:
	ThreadPool();
	~ThreadPool();

	// 设置线程池的工作模式
	void setMode(PoolMode mode);
	
	// 设置初始线程数量
	void setInitThreadSize(int size);
	
	// 设置task任务队列上限的阈值
	void setTaskQueMaxThreshHold(int threshhold);

	// 给线程池提交任务
	void submitTask(std::shared_ptr<Task>sp);

	// 开始线程池
	void start(int initThreadSize = 4);
};

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
public:
	void run(){ // 线程代码}
};

pool.submitTask(std::make_shared<MyTask>());

*/