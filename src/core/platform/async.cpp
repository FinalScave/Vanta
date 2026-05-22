#include "vanta/platform/async.h"

#include <algorithm>
#include <utility>

namespace vanta {

AsyncRuntime::AsyncRuntime(std::size_t worker_count) {
    Start(worker_count);
}

AsyncRuntime::~AsyncRuntime() {
    Stop();
}

void AsyncRuntime::Start(std::size_t worker_count) {
    if (!workers_.empty()) {
        return;
    }
    stopping_ = false;
    const std::size_t count = worker_count == 0 ? std::max<std::size_t>(1, std::thread::hardware_concurrency()) : worker_count;
    for (std::size_t i = 0; i < count; ++i) {
        workers_.emplace_back([this] {
            WorkerLoop();
        });
    }
}

void AsyncRuntime::Stop() {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        stopping_ = true;
    }
    worker_condition_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void AsyncRuntime::PostWorker(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        worker_queue_.push(std::move(task));
    }
    worker_condition_.notify_one();
}

void AsyncRuntime::PostMain(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(main_mutex_);
    main_queue_.push(std::move(task));
}

std::size_t AsyncRuntime::DrainMain() {
    std::size_t count = 0;
    while (true) {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lock(main_mutex_);
            if (main_queue_.empty()) {
                return count;
            }
            task = std::move(main_queue_.front());
            main_queue_.pop();
        }
        task();
        ++count;
    }
}

void AsyncRuntime::WorkerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(worker_mutex_);
            worker_condition_.wait(lock, [this] {
                return stopping_ || !worker_queue_.empty();
            });
            if (stopping_ && worker_queue_.empty()) {
                return;
            }
            task = std::move(worker_queue_.front());
            worker_queue_.pop();
        }
        task();
    }
}

}
