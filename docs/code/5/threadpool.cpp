#include"threadpool.h"

const int TASK_MAX_THRESHHOLD = 4;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10;  // 单位秒

ThreadPool::ThreadPool()
	:initThreadSize_(4),
	taskSize_(0),
	curThreadSize_(0),
	taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD),
	threadSizeThreshHold_(THREAD_MAX_THRESHHOLD),
	poolMode_(PoolMode::MODE_FIXED),
	isPoolRunning_(false),
	idleThreadSize_(0)
{}

ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	std::unique_lock<std::mutex>lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock,[&]()->bool { return threads_.size() == 0;});
}

void ThreadPool::setMode(PoolMode mode)
{
	if(checkRunningState())
	{
		return;
	}
	poolMode_ = mode;
}

void ThreadPool::setInitThreadSize(int size)
{
	initThreadSize_ = size;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if(checkRunningState())
	{
		return;
	}
	taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if(checkRunningState())
	{
		return;
	}
	if(poolMode_ == PoolMode::MODE_CACHED)
	{
	threadSizeThreshHold_ = threshhold;
	}
}

void ThreadPool::start(int initThreadSize)
{
	isPoolRunning_ = true;

	// 记录初始线程的个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// 创建线程对象
	for(int i = 0;i<initThreadSize_;i++)
	{
		// 创建thread线程对象的时候，把线程函数给到thread线程对象

		// threads_.emplace_back(new Thread(std::bind(&ThreadPool::ThreadFunc,this)));
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc,this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId,std::move(ptr));
	}

	// 启动所有线程
	for(auto &pair : threads_)
	{
		pair.second->start();
		idleThreadSize_++;
	}
}

Result ThreadPool::submitTask(std::shared_ptr<Task>task)
{
	// 获取锁
	std::unique_lock<std::mutex>lock(taskQueMtx_);
	bool wait_success = 0;
	wait_success = notFull_.wait_for(lock,std::chrono::seconds(1),
		[&]() { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; });
	
	if(!wait_success)
	{
		std::cerr<<"task queue is full submit task fail."<<std::endl;
		// 两种方式返回方式
		// 不行，task线程执行完后，task对象被销毁，外界无法调用，无法.get()到结果，因为用户可能在执行完很久之后才调用
		// return task->getResult() 
		return Result(task,false);
	}
		
	// 如果有空余，把任务放到任务队列中
	taskQue_.emplace(task);
	taskSize_++;

	// 由于新放任务，因此肯定不空，在notEmpty_上进行通知,分配线程执行任务
	notEmpty_.notify_all();

	// cahced模式,任务处理比较紧急,小而快,需根据任务数量和空闲线程的数量,判断是否需要创建新的线程
	if(poolMode_ == PoolMode::MODE_CACHED && taskSize_>idleThreadSize_ && curThreadSize_<threadSizeThreshHold_)
	{
		// 创建新线程
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc,this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId,std::move(ptr));
		threads_[threadId]->start();	// 启动线程
		curThreadSize_++;		// 当前线程添加一个
		idleThreadSize_++;		// 空闲线程添加一个
	}

	// 返回任务的Result对象
	return Result(task,true);
}

void ThreadPool::ThreadFunc(int threadid)	// 线程函数返回,相应的线程也就结束了
{
	auto lastTime = std::chrono::high_resolution_clock::now();	// 记录线程初始的时间
	while(isPoolRunning_)
	{
		std::shared_ptr<Task>task;
		// 先获取锁
		{
			std::unique_lock<std::mutex>lock(taskQueMtx_);
			// 等待notEmpty条件

			std::cout<<"tid:"<<std::this_thread::get_id()<<"尝试获取任务"<<std::endl;
			
			// cached模式下,有可能已经创建了很多线程,但是空闲时间超过60s,应把多余线程销毁
			// 当前时间 - 上一次线程执行的时间 > 60
			if(poolMode_ == PoolMode::MODE_CACHED)
			{
				// 每一秒钟返回一次 怎么区分:超时返回? 还是有任务待执行返回?
				while(taskQue_.size()==0 && isPoolRunning_)
				{
					if(!isPoolRunning_)
					{
						threads_.erase(threadid);
						exitCond_.notify_one();
						std::cout<<"thread id:"<<std::this_thread::get_id()<<"exit."<<std::endl;
						return;
					}

					if(std::cv_status::timeout == notEmpty_.wait_for(lock,std::chrono::seconds(1)))
					{
						// 看一看是否是线程池结束而被唤醒
						if(!isPoolRunning_)
						{
							threads_.erase(threadid);
							exitCond_.notify_one();
							std::cout<<"thread id:"<<std::this_thread::get_id()<<"exit."<<std::endl;
							return;
						}
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now-lastTime);
						// 超时，销毁线程
						if(dur.count()>=THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
						{
							// 开始回收当前线程
							// 把线程对象从线程容器中删除  如何匹配:threadFunc 与 thread对象??
							// 借助线程ID,找到线程,删除
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							exitCond_.notify_one();
							std::cout<<"thread id:"<<std::this_thread::get_id()<<"exit."<<std::endl;
							return;
						}
					}
				}
				
			}
			// 固定线程数量执行的程序
			else
			{
				notEmpty_.wait(lock,[&]()->bool { return taskQue_.size()>0 || !isPoolRunning_;});

				// 看一看是否是线程池结束而被唤醒
				if(!isPoolRunning_)
				{
					threads_.erase(threadid);
					exitCond_.notify_one();
					std::cout<<"thread id:"<<std::this_thread::get_id()<<"exit."<<std::endl;
					return;
				}
			}
			
			std::cout<<"tid:"<<std::this_thread::get_id()<<"获取任务成功"<<std::endl;
			
			// 从任务队列中取一个任务出来
			idleThreadSize_--;
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
			// task->run();	// 执行任务；把任务的返回值 setVal 方法给到 Result
			task->exec();
		}
		lastTime = std::chrono::high_resolution_clock::now();	// 更新线程执行完任务的时间
		idleThreadSize_++;
	}
	
	// 如果退出循环，那说明线程池已经要销毁，因此要结束线程
	std::cout<<"thread id:"<<std::this_thread::get_id()<<"exit."<<std::endl;
	{
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		threads_.erase(threadid);
		exitCond_.notify_one();
	}
	return;
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
//  task方法实现
Task::Task()
	: result_(nullptr)
{}

void Task::exec()
{
	if(result_!=nullptr)
	{
	result_->setVal(run());	// 这里发生多态调用
	}
}

void Task::setResult(Result* res)
{
	result_ = res;
}


///////////////////////////////////////////////////////////////////////////////////////////////
// 线程方法实现

int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func)
	:func_(func),
	threadId_(generateId_++)
{}

Thread::~Thread(){}

void Thread::start()
{
	// 创建一个线程，执行线程函数
	std::thread t(func_,threadId_);	// C++11来说，线程对象t  线程函数func_
	t.detach();				// 设置分离线程（保证线程函数不会销毁，分离线程函数与线程对象）
}

int Thread::getId() const
{
	return threadId_;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////
// Result 方法的实现 
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid),
	  task_(task)
{
	task_->setResult(this);
}

// 用户调用
Any Result::get()
{
	if(!isValid_)
	{
		return Any();
	}

	sem_.wait();	// task任务如果没有执行完，这里会阻塞用户的线程
	return std::move(any_);
}

// 
void Result::setVal(Any any)
{
	// 存储task的返回值
	this -> any_ = std::move(any);
	sem_.post();	// 已经获取的任务的返回值，增加信号量资源
}

