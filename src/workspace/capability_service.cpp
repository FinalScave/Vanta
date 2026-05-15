#include "vanta/workspace/capability_service.h"

#include <utility>

namespace vanta {

void CapabilityRegistry::set(Capability capability) {
    if (capability.id.empty()) {
        return;
    }
    if (!capability.data.isObject()) {
        capability.data = Json::object();
    }
    const std::string id = capability.id;
    capabilities_[id] = std::move(capability);
    publish(capabilities_.at(id));
}

bool CapabilityRegistry::remove(const std::string& id) {
    return capabilities_.erase(id) > 0;
}

std::optional<Capability> CapabilityRegistry::capability(const std::string& id) const {
    auto it = capabilities_.find(id);
    return it == capabilities_.end() ? std::nullopt : std::optional<Capability>(it->second);
}

std::vector<Capability> CapabilityRegistry::capabilities() const {
    std::vector<Capability> values;
    for (const auto& [id, capability] : capabilities_) {
        (void)id;
        values.push_back(capability);
    }
    return values;
}

bool CapabilityRegistry::available(const std::string& id) const {
    auto value = capability(id);
    return value && value->status == CapabilityStatus::Available;
}

void CapabilityRegistry::clear() {
    capabilities_.clear();
}

std::uint64_t CapabilityRegistry::onDidChangeCapability(EventBus<CapabilityChangeEvent>::Listener listener) {
    return onDidChange_.subscribe(std::move(listener));
}

void CapabilityRegistry::removeCapabilityListener(std::uint64_t listenerId) {
    onDidChange_.unsubscribe(listenerId);
}

void CapabilityRegistry::publish(const Capability& capability) {
    onDidChange_.publish({.capability = capability});
}

std::string toString(CapabilityStatus status) {
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

Json toJson(const Capability& capability) {
    return Json::object({
        {"id", Json(capability.id)},
        {"title", Json(capability.title)},
        {"providerId", Json(capability.providerId)},
        {"status", Json(toString(capability.status))},
        {"message", Json(capability.message)},
        {"data", capability.data},
    });
}

Json toJson(const std::vector<Capability>& capabilities) {
    Json::Array values;
    for (const Capability& capability : capabilities) {
        values.push_back(toJson(capability));
    }
    return Json::array(std::move(values));
}

}
