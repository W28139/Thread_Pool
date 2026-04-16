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

	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	for(int i = 0;i<initThreadSize_;i++)
	{
		// 创建thread线程对象的时候，把线程函数给到thread线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc,this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId,std::move(ptr));
	}

	for(auto &pair : threads_)
	{
		pair.second->start();
		idleThreadSize_++;
	}
}

Result ThreadPool::submitTask(std::shared_ptr<Task>task)
{
	std::unique_lock<std::mutex>lock(taskQueMtx_);
	bool wait_success = 0;
	wait_success = notFull_.wait_for(lock,std::chrono::seconds(1),
		[&]() { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; });
	
	if(!wait_success)
	{
		std::cerr<<"task queue is full submit task fail."<<std::endl;
		return Result(task,false);
	}
		
	taskQue_.emplace(task);
	taskSize_++;

	notEmpty_.notify_all();

	if(poolMode_ == PoolMode::MODE_CACHED && taskSize_>idleThreadSize_ && curThreadSize_<threadSizeThreshHold_)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::ThreadFunc,this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId,std::move(ptr));
		threads_[threadId]->start();	// 启动线程
		curThreadSize_++;		// 当前线程添加一个
		idleThreadSize_++;		// 空闲线程添加一个
	}

	return Result(task,true);
}

void ThreadPool::ThreadFunc(int threadid)	// 线程函数返回,相应的线程也就结束了
{
	auto lastTime = std::chrono::high_resolution_clock::now();	// 记录线程初始的时间
	while(isPoolRunning_)
	{
		std::shared_ptr<Task>task;
		{
			std::unique_lock<std::mutex>lock(taskQueMtx_);

			std::cout<<"tid:"<<std::this_thread::get_id()<<"尝试获取任务"<<std::endl;
			
			if(poolMode_ == PoolMode::MODE_CACHED)
			{
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
			
			idleThreadSize_--;
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			if(taskQue_.size()>0)
			{
				notEmpty_.notify_all();
			}

			notFull_.notify_all();
		}

		// 当前线程负责执行这个任务
		if(task!=nullptr)
		{
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

void Result::setVal(Any any)
{
	// 存储task的返回值
	this -> any_ = std::move(any);
	sem_.post();	// 已经获取的任务的返回值，增加信号量资源
}

