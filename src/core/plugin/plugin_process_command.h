#pragma once

#include <filesystem>

#include "mornox/platform/process.h"
#include "mornox/plugin/plugin_manager.h"

namespace mornox::internal {

CommandSpec ResolvePluginProcessCommand(const PluginManifest& manifest, const std::filesystem::path& workspace_root);

}
