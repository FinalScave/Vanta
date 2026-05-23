#include "plugin/plugin_process_command.h"

#include <vector>

#include "mornox/platform/executable.h"

namespace mornox::internal {
namespace {

std::vector<std::string> PythonInterpreterCandidates() {
#if defined(_WIN32)
    return {"python", "py"};
#else
    return {
        "/opt/local/bin/python3.11",
        "/opt/homebrew/bin/python3",
        "/usr/local/bin/python3",
        "python3",
        "python",
    };
#endif
}

}

CommandSpec ResolvePluginProcessCommand(const PluginManifest& manifest, const std::filesystem::path& workspace_root) {
    const std::filesystem::path entry = manifest.extension.location / manifest.entry;
    CommandSpec command{
        .executable = entry.string(),
        .arguments = {},
        .working_directory = workspace_root,
    };

    if (entry.extension() == ".py") {
        if (auto python = FindFirstExecutableOnPath(PythonInterpreterCandidates())) {
            command.executable = python->string();
            command.arguments = {entry.string()};
        }
    }
    return command;
}

}
