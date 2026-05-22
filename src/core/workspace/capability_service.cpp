#include "vanta/workspace/capability_service.h"

#include <utility>

namespace vanta {

void CapabilityRegistry::Set(Capability capability) {
    if (capability.id.empty()) {
        return;
    }
    const std::string id = capability.id;
    capabilities_[id] = std::move(capability);
    Publish(capabilities_.at(id));
}

bool CapabilityRegistry::Remove(const std::string& id) {
    return capabilities_.erase(id) > 0;
}

std::optional<Capability> CapabilityRegistry::Get(const std::string& id) const {
    auto it = capabilities_.find(id);
    return it == capabilities_.end() ? std::nullopt : std::optional<Capability>(it->second);
}

std::vector<Capability> CapabilityRegistry::Capabilities() const {
    std::vector<Capability> values;
    for (const auto& [id, capability] : capabilities_) {
        (void)id;
        values.push_back(capability);
    }
    return values;
}

bool CapabilityRegistry::Available(const std::string& id) const {
    auto value = Get(id);
    return value && value->status == CapabilityStatus::Available;
}

void CapabilityRegistry::Clear() {
    capabilities_.clear();
}

std::uint64_t CapabilityRegistry::OnDidChangeCapability(EventBus<CapabilityChangeEvent>::Listener listener) {
    return on_did_change_.Subscribe(std::move(listener));
}

void CapabilityRegistry::RemoveCapabilityListener(std::uint64_t listener_id) {
    on_did_change_.Unsubscribe(listener_id);
}

void CapabilityRegistry::Publish(const Capability& capability) {
    on_did_change_.Publish({.capability = capability});
}

std::string ToString(CapabilityStatus status) {
    switch (status) {
    case CapabilityStatus::Initializing:
        return "initializing";
    case CapabilityStatus::Available:
        return "available";
    case CapabilityStatus::Degraded:
        return "degraded";
    case CapabilityStatus::Unavailable:
        return "unavailable";
    }
    return "unavailable";
}

}
