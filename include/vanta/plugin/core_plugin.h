#pragma once

#include <functional>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vanta/plugin/extension_context.h"

namespace vanta {

class CoreExtension {
public:
    virtual ~CoreExtension() = default;

    virtual void activate(ExtensionContext& context) = 0;
    virtual void deactivate();
};

using CoreExtensionFactory = std::function<std::unique_ptr<CoreExtension>()>;

class CorePluginRegistry {
public:
    void add(std::string entry, CoreExtensionFactory factory);
    std::unique_ptr<CoreExtension> create(const std::string& entry) const;
    std::vector<std::string> entries() const;

private:
    std::map<std::string, CoreExtensionFactory> factories_;
};

struct CorePluginDependencies {
    std::filesystem::path clicePath;
    std::filesystem::path workspaceRoot;
};

CorePluginRegistry createDefaultCorePluginRegistry(CorePluginDependencies dependencies = {});

}
