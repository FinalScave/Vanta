#pragma once

#include "vanta/execution/build_service.h"

namespace vanta {

class CMakeBuildProvider final : public BuildProvider {
public:
    std::string id() const override;
    BuildEnvironment detect(const std::filesystem::path& workspaceRoot) const override;
    BuildPlan plan(const std::filesystem::path& workspaceRoot, const BuildTask& task) const override;
};

}
