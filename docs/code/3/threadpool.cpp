#include"threadpool.h"

const int TASK_MAX_THRESHHOLD = 4;

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
	// 获取锁
	std::unique_lock<std::mutex>lock(taskQueMtx_);
	// 线程的通信 等待任务队列有空余,两种书写方式

	// while(taskQue_.size()==taskQueMaxThreshHold_)
	// {
	// 	notFull_.wait(lock);
	// }
	
	// 用户提交任务，最长不能阻塞超过1s，否则判定任务提交失败，返回
	// wait			:常规使用,一直等，等待条件满足为止
	// wait_for		:增加时间参数，有等待的时间限制，倒计时等待
	// wait_until	:增加世间参数，等到具体的时间节点
	// notFull_.wait(lock,[&]() { return taskQue_.size() < taskQueMaxThreshHold_; })
	bool wait_success = 0;
	wait_success = notFull_.wait_for(lock,std::chrono::seconds(1),
		[&]() { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; });
	
	if(!wait_success)
	{
		std::cerr<<"task queue is full submit task fail."<<std::endl;
		return;
	}
		
	// 如果有空余，把任务放到任务队列中
	taskQue_.emplace(sp);
	taskSize_++;

	// 由于新放任务，因此肯定不空，在notEmpty_上进行通知,分配线程执行任务
	notEmpty_.notify_all();
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
		threads_.emplace_back(std::move(ptr));
	}

	// 启动所有线程
	for(int i = 0;i<initThreadSize_;i++)
	{
		threads_[i]->start();	// 需要执行线程函数
	}
}

void ThreadPool::ThreadFunc()
{
	for(;;)
	{
		std::shared_ptr<Task>task;
		// 先获取锁
		{
			std::unique_lock<std::mutex>lock(taskQueMtx_);
			// 等待notEmpty条件

			std::cout<<"tid:"<<std::this_thread::get_id()<<"尝试获取任务"<<std::endl;
			
			notEmpty_.wait(lock,[&]()->bool { return taskQue_.size()>0 ; });
			
			std::cout<<"tid:"<<std::this_thread::get_id()<<"获取任务成功"<<std::endl;

			// 从任务队列中取一个任务出来
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果依然有剩余任务，继续通知其它的线程执行任务
			if(taskQue_.size()>0)
			{
				notEmpty_.notify_all();
			}

			// 任务拿完以后，进行通知,通知可以继续提交生产任务
			notFull_.notify_all();
		}
		// 释放锁，拿到任务就可以了。原理：局部创建对象lock出了作用域{}后会销毁，锁会释放

		// 当前线程负责执行这个任务
		if(task!=nullptr)
		{
			task->run();
		}
	}
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


