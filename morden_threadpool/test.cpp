#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include "threadpool.h"

using namespace std;

// 示例函数1：计算和
int sum(int a, int b) {
    std::this_thread::sleep_for(std::chrono::seconds(1)); // 模拟耗时操作
    return a + b;
}

// 示例函数2：字符串处理
string concat(string a, string b) {
    return a + " " + b;
}

int main() {
    /* 
    测试1: FIXED 模式测试
    */
    cout << "--- Testing FIXED Mode ---" << endl;
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_FIXED);
        pool.start(4); // 启动4个核心线程

        // 提交计算任务
        future<int> res1 = pool.submitTask(sum, 10, 20);
        future<int> res2 = pool.submitTask(sum, 100, 200);
        future<int> res3 = pool.submitTask([](int i) { return i * i; }, 10);

        // 提交字符串任务
        future<string> res4 = pool.submitTask(concat, "Hello", "ThreadPool");

        // 获取结果 (get() 会阻塞直到任务完成)
        cout << "Sum1 result: " << res1.get() << endl;
        cout << "Sum2 result: " << res2.get() << endl;
        cout << "Lambda result: " << res3.get() << endl;
        cout << "String result: " << res4.get() << endl;
    } 
    // pool 离开作用域，触发析构函数，等待所有线程安全退出
    cout << "FIXED Mode pool destroyed safely." << endl << endl;


    /* 
    测试2: CACHED 模式扩容与回收测试
    */
    cout << "--- Testing CACHED Mode (Dynamic Expansion) ---" << endl;
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.setThreadSizeThreshold(10); // 最大扩容到10个线程
        pool.setTaskQueMaxThreshold(100);
        pool.start(2); // 初始2个线程

        // 同时提交大量耗时任务，观察是否会创建新线程
        vector<future<int>> results;
        for (int i = 0; i < 15; i++) {
            results.emplace_back(pool.submitTask([](int id) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                return id;
            }, i));
        }

        cout << "All tasks submitted. Waiting for results..." << endl;
        for (auto& res : results) {
            res.get(); // 阻塞等待所有任务完成
        }

        cout << "Tasks finished. Waiting for idle threads to exit (Timeout is set to 10s)..." << endl;
        // 睡眠一段时间，观察控制台是否打印 "Thread TID: ... exiting"
        std::this_thread::sleep_for(std::chrono::seconds(15));
    }
    cout << "CACHED Mode pool destroyed." << endl << endl;


    /* 
    测试3: 任务队列满载测试
    */
    cout << "--- Testing Task Queue Threshold ---" << endl;
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_FIXED);
        pool.setTaskQueMaxThreshold(2); // 极小的队列，模拟溢出
        pool.start(1);

        // 提交一个长时间任务，占用掉唯一的线程
        pool.submitTask([]() { std::this_thread::sleep_for(std::chrono::seconds(5)); });

        // 连续提交任务填满队列
        pool.submitTask([]() { return 1; });
        pool.submitTask([]() { return 2; });

        // 第四个任务应该会因为队列满而失败（打印错误信息并返回空结果）
        auto fail_res = pool.submitTask([]() { return 404; });
        
        // 此时提交的任务会立刻返回默认值（int为0）
        cout << "Failed task result (default): " << fail_res.get() << endl;
    }

    cout << "\nAll tests completed." << endl;
    return 0;
}

/*
g++ threadpool.cpp test.cpp -o pool_test -pthread
./pool_test
*/