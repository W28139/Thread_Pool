#include <iostream>
#include <chrono>
#include <vector>
#include <future>
#include <numeric>
#include "threadpool.h"

// 一个简单的计算密集型任务：计算向量元素的平方和
long long heavyTask(int n) {
    std::vector<int> vec(n);
    std::iota(vec.begin(), vec.end(), 1); // 填充 1 到 n
    long long sum = 0;
    for (int i : vec) {
        sum += (long long)i * i;
    }
    return sum;
}

void stressTest(int threadCount, int taskCount, int taskComplexity, PoolMode mode) {
    ThreadPool pool;
    pool.setMode(mode);
    pool.setThreadSizeThreshold(20); // Cached模式上限
    pool.start(threadCount);

    std::cout << "Starting Stress Test..." << std::endl;
    std::cout << "Mode: " << (mode == PoolMode::MODE_FIXED ? "FIXED" : "CACHED") << std::endl;
    std::cout << "Threads: " << threadCount << ", Tasks: " << taskCount << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::future<long long>> results;
    results.reserve(taskCount);

    // 1. 压测提交任务的性能
    for (int i = 0; i < taskCount; ++i) {
        results.push_back(pool.submitTask(heavyTask, taskComplexity));
    }

    // 2. 压测任务执行的并行度
    long long totalSum = 0;
    for (auto& res : results) {
        totalSum += res.get(); // 获取结果
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Test Completed!" << std::endl;
    std::cout << "Time taken: " << elapsed.count() << " seconds" << std::endl;
    std::cout << "Throughput: " << (taskCount / elapsed.count()) << " tasks/sec" << std::endl;
    std::cout << "------------------------------------------" << std::endl;
}

int main() {
    // 场景 A：小任务、超高并发（测试锁竞争）
    // 10万个小任务，验证线程池在频繁存取任务时的锁性能
    stressTest(4, 100000, 100, PoolMode::MODE_FIXED);

    // 场景 B：大计算量任务（测试 CPU 并行效率）
    // 2000个中等计算量任务，观察 CPU 是否跑满
    stressTest(std::thread::hardware_concurrency(), 2000, 100000, PoolMode::MODE_FIXED);

    // 场景 C：Cached 模式下的动态伸缩压测
    // 模拟任务突发峰值，观察线程创建和销毁是否正常，是否有死锁
    stressTest(2, 5000, 50000, PoolMode::MODE_CACHED);

    return 0;
}