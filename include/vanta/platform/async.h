#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace vanta {

class AsyncRuntime {
public:
    explicit AsyncRuntime(std::size_t worker_count = 0);
    AsyncRuntime(const AsyncRuntime&) = delete;
    AsyncRuntime& operator=(const AsyncRuntime&) = delete;
    ~AsyncRuntime();

    void Start(std::size_t worker_count = 0);
    void Stop();
    void PostWorker(std::function<void()> task);
    void PostMain(std::function<void()> task);
    std::size_t DrainMain();

private:
    void WorkerLoop();

    bool stopping_ = false;
    std::mutex worker_mutex_;
    std::condition_variable worker_condition_;
    std::queue<std::function<void()>> worker_queue_;
    std::vector<std::thread> workers_;

    std::mutex main_mutex_;
    std::queue<std::function<void()>> main_queue_;
};

}
