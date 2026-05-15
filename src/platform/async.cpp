#include "vanta/platform/async.h"

#include <algorithm>

namespace vanta {

CancellationToken::CancellationToken()
    : cancelled_(std::make_shared<std::atomic_bool>(false)) {}

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> cancelled)
    : cancelled_(std::move(cancelled)) {}

bool CancellationToken::cancelled() const {
    return cancelled_ != nullptr && cancelled_->load();
}

CancellationSource::CancellationSource()
    : cancelled_(std::make_shared<std::atomic_bool>(false)) {}

CancellationToken CancellationSource::token() const {
    return CancellationToken(cancelled_);
}

void CancellationSource::cancel() {
    cancelled_->store(true);
}

ProgressReporter::ProgressReporter(Sink sink)
    : sink_(std::move(sink)) {}

void ProgressReporter::report(ProgressEvent event) const {
    if (sink_) {
        sink_(event);
    }
}

AsyncRuntime::AsyncRuntime(std::size_t workerCount) {
    start(workerCount);
}

AsyncRuntime::~AsyncRuntime() {
    stop();
}

void AsyncRuntime::start(std::size_t workerCount) {
    if (!workers_.empty()) {
        return;
    }
    stopping_ = false;
    const std::size_t count = workerCount == 0 ? std::max<std::size_t>(1, std::thread::hardware_concurrency()) : workerCount;
    for (std::size_t i = 0; i < count; ++i) {
        workers_.emplace_back([this] {
            workerLoop();
        });
    }
}

void AsyncRuntime::stop() {
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        stopping_ = true;
    }
    workerCondition_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void AsyncRuntime::postWorker(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        workerQueue_.push(std::move(task));
    }
    workerCondition_.notify_one();
}

void AsyncRuntime::postMain(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(mainMutex_);
    mainQueue_.push(std::move(task));
}

std::size_t AsyncRuntime::drainMain() {
    std::size_t count = 0;
    while (true) {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lock(mainMutex_);
            if (mainQueue_.empty()) {
                return count;
            }
            task = std::move(mainQueue_.front());
            mainQueue_.pop();
        }
        task();
        ++count;
    }
}

void AsyncRuntime::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(workerMutex_);
            workerCondition_.wait(lock, [this] {
                return stopping_ || !workerQueue_.empty();
            });
            if (stopping_ && workerQueue_.empty()) {
                return;
            }
            task = std::move(workerQueue_.front());
            workerQueue_.pop();
        }
        task();
    }
}

}
