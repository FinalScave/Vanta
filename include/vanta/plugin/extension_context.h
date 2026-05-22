#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vanta/core/localization.h"
#include "vanta/core/registration.h"
#include "vanta/core/value.h"
#include "vanta/plugin/plugin_storage.h"
#include "vanta/workspace/workspace.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta {

struct ExtensionInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string publisher;
    std::filesystem::path location;
};

class Logger {
public:
    virtual ~Logger() = default;
    virtual void Info(const std::string& message) = 0;
    virtual void Warn(const std::string& message) = 0;
    virtual void Error(const std::string& message) = 0;
};

class ExtensionContext {
public:
    virtual ~ExtensionContext() = default;

    virtual const ExtensionInfo& Extension() const = 0;
    virtual const WorkspaceInfo& Workspace() const = 0;
    virtual Logger& Log() = 0;
    virtual WorkspaceContext& Context() = 0;
    void* GetService(std::string_view service_id) {
        return Context().GetService(service_id);
    }
    template <typename T>
    T* GetService() {
        return Context().GetService<T>();
    }
    virtual Localizer LocalizerValue() const = 0;
    virtual PluginStorageService& Storage() = 0;
    virtual void Track(RegistrationHandle registration) = 0;
};

}
