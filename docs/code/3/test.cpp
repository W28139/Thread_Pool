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
    ThreadPool pool;
    pool.start(4);

    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    std::this_thread::sleep_for(std::chrono::seconds(10));
}
