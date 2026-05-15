#pragma once

#include <filesystem>
#include <string>

namespace vanta {

struct GitDiff {
    int exitCode = -1;
    std::string text;
};

class GitClient {
public:
    GitDiff diff(const std::filesystem::path& workspaceRoot) const;
};

}
