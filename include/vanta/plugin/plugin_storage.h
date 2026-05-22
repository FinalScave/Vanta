#pragma once

#include <filesystem>
#include <string>

#include "vanta/core/result.h"
#include "vanta/core/value.h"

namespace vanta {

class PluginStorageService {
public:
    static constexpr const char* kServiceId = "vanta.pluginStorage";

    explicit PluginStorageService(std::filesystem::path root = {});

    void SetRoot(std::filesystem::path root);
    Result<void> Write(std::string plugin_id, std::string key, Value value) const;
    Result<Value> Read(const std::string& plugin_id, const std::string& key) const;

private:
    std::filesystem::path PathFor(const std::string& plugin_id, const std::string& key) const;

    std::filesystem::path root_;
};

}
