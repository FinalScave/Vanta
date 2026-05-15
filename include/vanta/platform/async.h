#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace vanta {

class CancellationToken {
public:
    CancellationToken();
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> cancelled);

    bool cancelled() const;

private:
    std::shared_ptr<std::atomic_bool> cancelled_;
};

class CancellationSource {
public:
    CancellationSource();

    CancellationToken token() const;
    void cancel();

private:
    std::shared_ptr<std::atomic_bool> cancelled_;
};

struct ProgressEvent {
    std::uint64_t jobId = 0;
    std::string title;
    std::string message;
    double fraction = -1.0;
};

class ProgressReporter {
public:
    using Sink = std::function<void(const ProgressEvent&)>;

    explicit ProgressReporter(Sink sink = {});

    void report(ProgressEvent event) const;

private:
    Sink sink_;
};

class AsyncRuntime {
public:
    explicit AsyncRuntime(std::size_t workerCount = 0);
    AsyncRuntime(const AsyncRuntime&) = delete;
    AsyncRuntime& operator=(const AsyncRuntime&) = delete;
    ~AsyncRuntime();

    void start(std::size_t workerCount = 0);
    void stop();
    void postWorker(std::function<void()> task);
    void postMain(std::function<void()> task);
    std::size_t drainMain();

private:
    void workerLoop();

    bool stopping_ = false;
    std::mutex workerMutex_;
    std::condition_variable workerCondition_;
    std::queue<std::function<void()>> workerQueue_;
    std::vector<std::thread> workers_;

    std::mutex mainMutex_;
    std::queue<std::function<void()>> mainQueue_;
};

template <typename Event>
class EventBus {
public:
    using Listener = std::function<void(const Event&)>;

    std::uint64_t subscribe(Listener listener) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint64_t id = nextId_++;
        listeners_[id] = std::move(listener);
        return id;
    }

    void unsubscribe(std::uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_.erase(id);
    }

    void publish(const Event& event) const {
        std::vector<Listener> listeners;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, listener] : listeners_) {
                (void)id;
                listeners.push_back(listener);
            }
        }
        for (const Listener& listener : listeners) {
            listener(event);
        }
    }

private:
    mutable std::mutex mutex_;
    mutable std::map<std::uint64_t, Listener> listeners_;
    std::uint64_t nextId_ = 1;
};

}
