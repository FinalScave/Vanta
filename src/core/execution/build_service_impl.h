#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vanta/execution/build_service.h"

namespace vanta::internal {

class BuildServiceImpl final : public BuildService {
public:
    RegistrationHandle RegisterProvider(std::unique_ptr<BuildProvider> provider) override;
    void RemoveProvider(const std::string& provider_id) override;
    std::vector<std::string> BuildProviderIds() const override;
    BuildEnvironment Detect(WorkspaceContext& context, const ProjectModel& project) const override;
    BuildHandle Start(
        WorkspaceContext& context,
        const BuildRequest& request,
        ExecutionEventCallback on_event = {}) const override;
    BuildResult Run(
        WorkspaceContext& context,
        const BuildRequest& request,
        ExecutionEventCallback on_event = {}) const override;

private:
    const BuildProvider* ChooseProvider(WorkspaceContext& context, const BuildRequest& request) const;

    std::map<std::string, std::unique_ptr<BuildProvider>> providers_;
};

}
