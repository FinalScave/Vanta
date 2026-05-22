#include "vanta/core/cancellation.h"

#include <utility>

namespace vanta {

CancellationToken::CancellationToken()
    : cancelled_(std::make_shared<std::atomic_bool>(false)) {}

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> cancelled)
    : cancelled_(std::move(cancelled)) {}

bool CancellationToken::Cancelled() const {
    return cancelled_ != nullptr && cancelled_->load();
}

CancellationSource::CancellationSource()
    : cancelled_(std::make_shared<std::atomic_bool>(false)) {}

CancellationToken CancellationSource::Token() const {
    return CancellationToken(cancelled_);
}

void CancellationSource::Cancel() {
    cancelled_->store(true);
}

}
