#include"threadpool.h"

class MyTask : public Task
{
public:
    MyTask(int begin, int end)
    : begin_(begin),
      end_(end)
    {}

    Any run()
    {
        std::cout<<"begin : "<<std::this_thread::get_id()<<std::endl;
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
        int sum = 0;
        for(int i=begin_;i<=end_;i++)
        {
            sum+=i;
        }

        std::cout<<"end : "<<std::this_thread::get_id()<<std::endl;

        return sum;
    }

private:
    int begin_;
    int end_;
};

int main()
{
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.start(4);

        Result res1 = pool.submitTask(std::make_shared<MyTask>(1,100));
        int sum1 = res1.get().cast_<int>();
        std::cout<<sum1<<std::endl;
    }
    // Result res2 = pool.submitTask(std::make_shared<MyTask>(101,200));
    //Result res3 = pool.submitTask(std::make_shared<MyTask>(201,300));
    //Result res4 = pool.submitTask(std::make_shared<MyTask>(301,400));

    // int sum2 = res2.get().cast_<int>();
    //int sum3 = res3.get().cast_<int>();
   // int sum4 = res4.get().cast_<int>();

   // std::cout<<sum1+sum2+sum3+sum4<<std::endl;

    //std::this_thread::sleep_for(std::chrono::seconds(10));
    getchar();
}

// Master - Slave线程模型
// Master线程用来分解任务，然后分配给各个Salve线程分配任务
// 等待各个Slave线程执行完任务，返回结果
// Master线程合并各个任务结果，输出