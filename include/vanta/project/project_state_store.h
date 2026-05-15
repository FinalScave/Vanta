#pragma once

#include <filesystem>
#include <string>

#include "vanta/project/component.h"

namespace vanta {

class ProjectStateStore {
public:
    bool load(const std::filesystem::path& path, ProjectState* state, std::string* errorMessage = nullptr) const;
    bool save(const std::filesystem::path& path, const ProjectState& state, std::string* errorMessage = nullptr) const;
};

Json toJson(const ProjectState& state);
ProjectState projectStateFromJson(const Json& json);

}
