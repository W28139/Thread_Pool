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
#include<unordered_map>

// Any类型：可以接受任意数据的类型
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator = (Any&&) = default;

	// 这个构造函数可以让Any类型接收任意其它的数据
	template<typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data))
	{}

	// 这个方法能把Any对象里面存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		// 我们怎么从base_指针里，找到它所指向的Derive_对象，然后从他里面取出data成员变量
		// 基类指针——> 派生类指针  RTTI
		Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get());
		if(pd==nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}

private:
	// 基类类型
	class Base
	{
	public:
		virtual ~Base() {};
	};

	// 派生类类型
	template<typename T>
	class Derive:public Base
	{
	public:
		Derive(T data):data_(data)
		{}

		T data_;
	};

private:
	// 定义一个基类的指针
	std::unique_ptr<Base> base_;
};

// 实现一个信号量
class Semaphore
{
private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;

public:
	Semaphore(int limit = 0)
	: resLimit_(limit)
	{}

	~Semaphore() = default;

	// 获取一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		// 等待信号量有资源，如果没有资源的话，会阻塞当前进程
		cond_.wait(lock,[&]()->bool {return resLimit_ > 0; } );
		resLimit_--;
	}	
	
	// 增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex>lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
};

// Task类型的前置声明
class Task;
// 实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
private:
	Any any_;		// 存储任务的返回值
	Semaphore sem_;	// 线程通信信号量
	std::shared_ptr<Task>task_;	// 指向对应获取返回值的任务对象
	std::atomic_bool isValid_;	// 返回值是否有效（是否成功提交上）
public:
	Result(std::shared_ptr<Task> task ,bool isValid = true);
	~Result() = default;

	// setVal 方法：获取任务执行完的返回值 任务线程 -> Result
	void setVal(Any any);
	
	// get 方法： 用户调用这个方法获取task的返回值 Result -> 用户
	Any get();
	
};

// 任务抽象基类
class Task
{
private:
	Result* result_;	// Result的生命周期长于Task的
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	// 用户可以自定义任意类型，从Task继承，重写run方法，实现自定义任务处理
	virtual Any run() = 0;
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
	using ThreadFunc = std::function<void(int)>;
	// 线程构造
	Thread(ThreadFunc func);
	// 线程析构
	~Thread();
	// 启动线程
	void start();

	// 获取线程ID
	int getId() const;

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;	// 保存线程ID
};

// 线程池类
class ThreadPool
{
private:
	std::atomic_int curThreadSize_; 				// 记录当前线程池里面线程的总数量
	// std::vector<std::unique_ptr<Thread>> threads_;	// 线程列表
	std::unordered_map<int,std::unique_ptr<Thread>>threads_;	// 线程列表
	size_t initThreadSize_;		// 初始的线程数量
	std::atomic_int idleThreadSize_;	// 记录空闲线程的数量
	int threadSizeThreshHold_;	// 线程数量的上限阈值

	// 由于传入对象生命周期不确定，因此要用智能指针
	std::queue<std::shared_ptr<Task>> taskQue_;	// 任务队列
	std::atomic_int taskSize_;	// 表示任务的数量，用原子变量，保证统一
	int taskQueMaxThreshHold_;	// 任务队列数量上限的阈值

	std::mutex taskQueMtx_;		// 保证任务队列的线程安全
	std::condition_variable notFull_;	// 表示任务队列不满
	std::condition_variable notEmpty_;	// 表示任务队列不空
	std::condition_variable exitCond_;

	PoolMode poolMode_;			// 当前线程池的工作模式
	std::atomic_bool isPoolRunning_;	// 表示线程池是否在运行

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数  线程池的所有线程从任务队列里面消费任务
	void ThreadFunc(int threadid);

	// 检查pool的运行状态
	bool checkRunningState() const;

public:
	ThreadPool();
	~ThreadPool();

	// 设置线程池的工作模式
	void setMode(PoolMode mode);
	
	// 设置初始线程数量
	void setInitThreadSize(int size);
	
	// 设置task任务队列上限的阈值
	void setTaskQueMaxThreshHold(int threshhold);

	// 设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold);

	// 给线程池提交任务
	Result submitTask(std::shared_ptr<Task>sp);

	// 开始线程池,默认是本设备CPU核心数量
	void start(int initThreadSize = std::thread::hardware_concurrency());
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