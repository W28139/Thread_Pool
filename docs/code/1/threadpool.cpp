#include"threadpool.h"

const int TASK_MAX_THRESHHOLD = 1024;

ThreadPool::ThreadPool()
	:initThreadSize_(4),
	taskSize_(0),
	taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD),
	poolMode_(PoolMode::MODE_FIXED)
{}

ThreadPool::~ThreadPool()
{}

void ThreadPool::setMode(PoolMode mode)
{
	poolMode_ = mode;
}

void ThreadPool::setInitThreadSize(int size)
{
	initThreadSize_ = size;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::submitTask(std::shared_ptr<Task>sp)
{
	// 稍后完成
}

void ThreadPool::start(int initThreadSize)
{
	// 记录初始线程的个数
	initThreadSize_ = initThreadSize;

	// 创建线程对象
	for(int i = 0;i<initThreadSize_;i++)
	{
		// 创建thread线程对象的时候，把线程函数给到thread线程对象
		// threads_.emplace_back(new Thread(std::bind(&ThreadPool::ThreadFunc,this)));
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc,this));
		threads_.emplace_back(ptr);
	}

	// 启动所有线程
	for(int i = 0;i<initThreadSize_;i++)
	{
		threads_[i]->start();	// 需要执行线程函数
	}
}

void ThreadPool::ThreadFunc()
{
	std::cout<<"begin threadFunc"<<std::endl;
	std::cout<<std::this_thread::get_id()<<std::endl;
	std::cout<<"end threadFunc"<<std::endl;
}


//////////////////// 线程方法实现
Thread::Thread(ThreadFunc func)
	:func_(func)
{}

Thread::~Thread(){}

void Thread::start()
{
	// 创建一个线程，执行线程函数
	std::thread t(func_);	// C++11来说，线程对象t  线程函数func_
	t.detach();				// 设置分离线程（保证线程函数不会销毁，分离线程函数与线程对象）
}


