#include "ide/ui_service_impl.h"

#include <algorithm>

namespace vanta::internal {
namespace {

template <typename Provider>
RegistrationHandle RegisterProvider(
    std::map<std::string, Provider*>& providers,
    Provider* provider,
    void (UiServiceImpl::*remove)(const std::string&),
    UiServiceImpl* service) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string id = provider->Id();
    providers[id] = provider;
    return RegistrationHandle([service, remove, id] {
        (service->*remove)(id);
    });
}

template <typename Provider>
std::vector<std::string> ProviderIds(const std::map<std::string, Provider*>& providers) {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : providers) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

template <typename Descriptor>
void SortDescriptors(std::vector<Descriptor>& descriptors) {
    std::sort(descriptors.begin(), descriptors.end(), [](const Descriptor& left, const Descriptor& right) {
        if (left.sort_order != right.sort_order) {
            return left.sort_order < right.sort_order;
        }
        return left.id < right.id;
    });
}

}

RegistrationHandle UiServiceImpl::RegisterPanelProvider(UiPanelProvider* provider) {
    return RegisterProvider(panel_providers_, provider, &UiServiceImpl::RemovePanelProvider, this);
}

RegistrationHandle UiServiceImpl::RegisterActionProvider(UiActionProvider* provider) {
    return RegisterProvider(action_providers_, provider, &UiServiceImpl::RemoveActionProvider, this);
}

RegistrationHandle UiServiceImpl::RegisterSettingsPageProvider(UiSettingsPageProvider* provider) {
    return RegisterProvider(settings_page_providers_, provider, &UiServiceImpl::RemoveSettingsPageProvider, this);
}

std::vector<std::string> UiServiceImpl::PanelProviderIds() const {
    return ProviderIds(panel_providers_);
}

std::vector<std::string> UiServiceImpl::ActionProviderIds() const {
    return ProviderIds(action_providers_);
}

std::vector<std::string> UiServiceImpl::SettingsPageProviderIds() const {
    return ProviderIds(settings_page_providers_);
}

std::vector<UiPanelDescriptor> UiServiceImpl::Panels(WorkspaceContext& context) const {
    std::vector<UiPanelDescriptor> descriptors;
    for (const auto& [id, provider] : panel_providers_) {
        (void)id;
        std::vector<UiPanelDescriptor> provided = provider->Panels(context);
        descriptors.insert(descriptors.end(), provided.begin(), provided.end());
    }
    SortDescriptors(descriptors);
    return descriptors;
}

std::vector<UiActionDescriptor> UiServiceImpl::Actions(WorkspaceContext& context) const {
    std::vector<UiActionDescriptor> descriptors;
    for (const auto& [id, provider] : action_providers_) {
        (void)id;
        std::vector<UiActionDescriptor> provided = provider->Actions(context);
        descriptors.insert(descriptors.end(), provided.begin(), provided.end());
    }
    SortDescriptors(descriptors);
    return descriptors;
}

std::vector<UiSettingsPageDescriptor> UiServiceImpl::SettingsPages(WorkspaceContext& context) const {
    std::vector<UiSettingsPageDescriptor> descriptors;
    for (const auto& [id, provider] : settings_page_providers_) {
        (void)id;
        std::vector<UiSettingsPageDescriptor> provided = provider->SettingsPages(context);
        descriptors.insert(descriptors.end(), provided.begin(), provided.end());
    }
    SortDescriptors(descriptors);
    return descriptors;
}

void UiServiceImpl::RemovePanelProvider(const std::string& provider_id) {
    panel_providers_.erase(provider_id);
}

void UiServiceImpl::RemoveActionProvider(const std::string& provider_id) {
    action_providers_.erase(provider_id);
}

void UiServiceImpl::RemoveSettingsPageProvider(const std::string& provider_id) {
    settings_page_providers_.erase(provider_id);
}

}
