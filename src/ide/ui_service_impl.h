#pragma once

#include <map>
#include <string>

#include "vanta/ide/ui_service.h"

namespace vanta::internal {

class UiServiceImpl final : public UiService {
public:
    RegistrationHandle RegisterPanelProvider(UiPanelProvider* provider) override;
    RegistrationHandle RegisterActionProvider(UiActionProvider* provider) override;
    RegistrationHandle RegisterSettingsPageProvider(UiSettingsPageProvider* provider) override;

    std::vector<std::string> PanelProviderIds() const override;
    std::vector<std::string> ActionProviderIds() const override;
    std::vector<std::string> SettingsPageProviderIds() const override;

    std::vector<UiPanelDescriptor> Panels(WorkspaceContext& context) const override;
    std::vector<UiActionDescriptor> Actions(WorkspaceContext& context) const override;
    std::vector<UiSettingsPageDescriptor> SettingsPages(WorkspaceContext& context) const override;

private:
    void RemovePanelProvider(const std::string& provider_id);
    void RemoveActionProvider(const std::string& provider_id);
    void RemoveSettingsPageProvider(const std::string& provider_id);

    std::map<std::string, UiPanelProvider*> panel_providers_;
    std::map<std::string, UiActionProvider*> action_providers_;
    std::map<std::string, UiSettingsPageProvider*> settings_page_providers_;
};

}
