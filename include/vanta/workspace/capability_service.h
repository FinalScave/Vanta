#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/platform/async.h"
#include "vanta/platform/json.h"

namespace vanta {

enum class CapabilityStatus {
    Initializing,
    Available,
    Degraded,
    Unavailable,
};

struct Capability {
    std::string id;
    std::string title;
    std::string providerId;
    CapabilityStatus status = CapabilityStatus::Unavailable;
    std::string message;
    Json data;
};

struct CapabilityChangeEvent {
    Capability capability;
};

class CapabilityRegistry {
public:
    void set(Capability capability);
    bool remove(const std::string& id);
    std::optional<Capability> capability(const std::string& id) const;
    std::vector<Capability> capabilities() const;
    bool available(const std::string& id) const;
    void clear();
    std::uint64_t onDidChangeCapability(EventBus<CapabilityChangeEvent>::Listener listener);
    void removeCapabilityListener(std::uint64_t listenerId);

private:
    void publish(const Capability& capability);

    std::map<std::string, Capability> capabilities_;
    EventBus<CapabilityChangeEvent> onDidChange_;
};

std::string toString(CapabilityStatus status);
Json toJson(const Capability& capability);
Json toJson(const std::vector<Capability>& capabilities);

}
