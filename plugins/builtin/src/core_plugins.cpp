#include "vanta/plugin/core_plugin.h"

#include <algorithm>
#include <utility>

#include "core_plugin_factories.h"

namespace vanta {

void CoreExtension::Deactivate() {}

void CorePluginRegistry::Add(std::string entry, CoreExtensionFactory factory) {
    factories_[std::move(entry)] = std::move(factory);
}

std::unique_ptr<CoreExtension> CorePluginRegistry::Create(const std::string& entry) const {
    auto it = factories_.find(entry);
    if (it == factories_.end()) {
        return nullptr;
    }
    return it->second();
}

std::vector<std::string> CorePluginRegistry::Entries() const {
    std::vector<std::string> result;
    for (const auto& [entry, factory] : factories_) {
        (void)factory;
        result.push_back(entry);
    }
    std::sort(result.begin(), result.end());
    return result;
}

CorePluginRegistry CreateDefaultCorePluginRegistry(CorePluginDependencies dependencies) {
    CorePluginRegistry registry;
    registry.Add("builtin:languages", [] {
        return builtin::CreateLanguagesCoreExtension();
    });
    registry.Add("builtin:cpp", [] {
        return builtin::CreateCppCoreExtension();
    });
    registry.Add("builtin:python", [] {
        return builtin::CreatePythonCoreExtension();
    });
    registry.Add("builtin:cmake", [] {
        return builtin::CreateCMakeCoreExtension();
    });
    registry.Add("builtin:git", [] {
        return builtin::CreateGitCoreExtension();
    });
    registry.Add("builtin:clice", [dependencies] {
        return builtin::CreateCliceCoreExtension(dependencies);
    });
    return registry;
}

}
