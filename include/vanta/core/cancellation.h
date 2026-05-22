#pragma once

#include <atomic>
#include <memory>

namespace vanta {

class CancellationToken {
public:
    CancellationToken();
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> cancelled);

    bool Cancelled() const;

private:
    std::shared_ptr<std::atomic_bool> cancelled_;
};

class CancellationSource {
public:
    CancellationSource();

    CancellationToken Token() const;
    void Cancel();

private:
    std::shared_ptr<std::atomic_bool> cancelled_;
};

}
