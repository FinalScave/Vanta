#pragma once

#include <filesystem>
#include <string>

namespace vanta {

struct GitDiff {
    int exit_code = -1;
    std::string text;
};

class GitService {
public:
    static constexpr const char* kServiceId = "vanta.git";

    virtual ~GitService() = default;

    virtual GitDiff Diff() const = 0;
};

}
