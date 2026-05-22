#pragma once

#include <filesystem>
#include <string>

#include "vanta/project/component.h"

namespace vanta {

class ProjectStateStore {
public:
    bool Load(const std::filesystem::path& path, ProjectState* state, std::string* error_message = nullptr) const;
    bool Save(const std::filesystem::path& path, const ProjectState& state, std::string* error_message = nullptr) const;
};

}
