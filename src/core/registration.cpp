#include "vanta/core/registration.h"

#include <utility>

namespace vanta {

RegistrationHandle::RegistrationHandle(std::function<void()> unregister)
    : unregister_(std::move(unregister)), registered_(true) {}

RegistrationHandle::RegistrationHandle(RegistrationHandle&& other) noexcept {
    *this = std::move(other);
}

RegistrationHandle& RegistrationHandle::operator=(RegistrationHandle&& other) noexcept {
    if (this != &other) {
        unregister();
        unregister_ = std::move(other.unregister_);
        registered_ = other.registered_;
        other.registered_ = false;
    }
    return *this;
}

RegistrationHandle::~RegistrationHandle() {
    unregister();
}

void RegistrationHandle::unregister() noexcept {
    if (!registered_) {
        return;
    }
    registered_ = false;
    if (unregister_) {
        unregister_();
    }
}

bool RegistrationHandle::registered() const noexcept {
    return registered_;
}

}
