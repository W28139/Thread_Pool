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
	{
		std::unique_lock<std::mutex>lock(taskQueMtx_);
		notEmpty_.notify_all();
	}
	//exitCond_.wait(lock,[&]()->bool { return threads_.size() == 0;});

	// join 会阻塞在这里，直到子线程把 taskQue_ 里的活全干完并退出 ThreadFunc
	for(auto &pair:threads_)
	{
		pair.second->join();
	}
	threads_.clear();
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

void ThreadPool::ThreadFunc(int threadid)
{
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    while (true) 
    {
        std::shared_ptr<Task> task;
        {
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            std::cout << "tid:" << std::this_thread::get_id() << " 尝试获取任务" << std::endl;

            // 1. 等待任务或者退出信号
            while (taskQue_.empty())
            {
                // 如果线程池已停止运行，准备退出
                if (!isPoolRunning_)
                {
                    goto EXIT_HANDLER; 
                }

                if (poolMode_ == PoolMode::MODE_CACHED)
                {
                    // CACHED 模式：支持超时回收
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock::now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        
                        // 超时且当前线程数超过初始值，则销毁线程
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
                        {
                            goto EXIT_HANDLER;
                        }
                    }
                }
                else
                {
                    // FIXED 模式：无限期等待任务
                    notEmpty_.wait(lock, [&]() { return !taskQue_.empty() || !isPoolRunning_; });
                }
            }

            // 2. 成功获取任务
            std::cout << "tid:" << std::this_thread::get_id() << " 获取任务成功" << std::endl;
            
            idleThreadSize_--; // 准备执行，空闲线程减1
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

            // 如果队列中还有任务，继续通知其他线程
            if (!taskQue_.empty())
            {
                notEmpty_.notify_all();
            }
            notFull_.notify_all(); 
        } // 自动释放锁

        // 3. 执行任务（在锁之外执行，提高并发度）
        if (task != nullptr)
        {
            task->exec();
        }

        // 4. 任务完成，更新状态
        lastTime = std::chrono::high_resolution_clock::now();
        idleThreadSize_++; 
    }

EXIT_HANDLER:
    // 线程退出时的收尾逻辑
    // 注意：必须先打印，再从 map 中删除，最后通知析构函数
    std::cout << "thread id:" << std::this_thread::get_id() << " exit." << std::endl;

	curThreadSize_--;
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
	t_ = std::thread(func_,threadId_);	// C++11来说，线程对象t  线程函数func_
	// t.detach();				// 设置分离线程（保证线程函数不会销毁，分离线程函数与线程对象）
}

void Thread::join()
{
	if(t_.joinable())
	{
		t_.join();
	}
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

