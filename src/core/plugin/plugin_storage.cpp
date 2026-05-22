#include "vanta/plugin/plugin_storage.h"

#include <fstream>
#include <sstream>
#include <utility>

#include "vanta/core/json_codec.h"

namespace vanta {

PluginStorageService::PluginStorageService(std::filesystem::path root)
    : root_(std::move(root)) {}

void PluginStorageService::SetRoot(std::filesystem::path root) {
    root_ = std::move(root);
}

Result<void> PluginStorageService::Write(std::string plugin_id, std::string key, Value value) const {
    const std::filesystem::path path = PathFor(plugin_id, key);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return Result<void>::Failure("storage.directory", "Failed to create plugin storage directory");
    }

    std::ofstream output(path);
    if (!output) {
        return Result<void>::Failure("storage.write", "Failed to write plugin storage");
    }
    output << ValueToJsonText(value);
    return Result<void>::Success();
}

Result<Value> PluginStorageService::Read(const std::string& plugin_id, const std::string& key) const {
    const std::filesystem::path path = PathFor(plugin_id, key);
    std::ifstream input(path);
    if (!input) {
        return Result<Value>::Failure("storage.read", "Failed to read plugin storage");
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return ValueFromJsonText(stream.str());
}

std::filesystem::path PluginStorageService::PathFor(const std::string& plugin_id, const std::string& key) const {
    return root_ / plugin_id / (key + ".json");
}

}
