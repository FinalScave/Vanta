#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vanta/execution/run_configuration.h"

namespace vanta::internal {

class RunConfigurationServiceImpl final : public RunConfigurationService {
public:
    RegistrationHandle RegisterProvider(std::unique_ptr<RunConfigurationProvider> provider) override;
    void RemoveProvider(const std::string& provider_id) override;
    RunConfigurationProvider* Provider(const std::string& provider_id) const override;
    std::vector<std::string> ProviderIds() const override;

    RunConfiguration Create(
        WorkspaceContext& context,
        const std::string& provider_id,
        const VirtualFile& focus_file = VirtualFile(),
        const std::string& name_hint = "") const override;
    std::vector<RunConfiguration> Discover(WorkspaceContext& context, const VirtualFile& focus_file) const override;
    RunResult Run(
        WorkspaceContext& context,
        const RunConfiguration& configuration,
        const std::string& target_id = "") const override;
    RunResult RunSaved(WorkspaceContext& context, const std::string& configuration_id, const std::string& target_id = "") const override;

private:
    std::map<std::string, std::unique_ptr<RunConfigurationProvider>> providers_;
};

}
