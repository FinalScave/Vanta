#pragma once

#include <functional>

namespace vanta {

class RegistrationHandle {
public:
    RegistrationHandle() = default;
    explicit RegistrationHandle(std::function<void()> unregister);
    RegistrationHandle(const RegistrationHandle&) = delete;
    RegistrationHandle& operator=(const RegistrationHandle&) = delete;
    RegistrationHandle(RegistrationHandle&& other) noexcept;
    RegistrationHandle& operator=(RegistrationHandle&& other) noexcept;
    ~RegistrationHandle();

    void unregister() noexcept;
    bool registered() const noexcept;

private:
    std::function<void()> unregister_;
    bool registered_ = false;
};

}
