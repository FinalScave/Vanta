#pragma once

#include <map>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/core/value.h"

namespace vanta {

class WorkspaceContext;

struct UiLocations {
    static constexpr const char* kLeftToolWindow = "leftToolWindow";
    static constexpr const char* kRightToolWindow = "rightToolWindow";
    static constexpr const char* kBottomToolWindow = "bottomToolWindow";
    static constexpr const char* kMenu = "menu";
    static constexpr const char* kToolbar = "toolbar";
    static constexpr const char* kStatusBar = "statusBar";
    static constexpr const char* kSettings = "settings";
};

struct UiPanelDescriptor {
    std::string id;
    std::string title;
    std::string icon;
    std::string location = UiLocations::kBottomToolWindow;
    std::string command_id;
    int sort_order = 0;
    bool closable = true;
    Value metadata;
};

struct UiActionDescriptor {
    std::string id;
    std::string title;
    std::string icon;
    std::string location = UiLocations::kToolbar;
    std::string command_id;
    int sort_order = 0;
    Value metadata;
};

struct UiSettingsPageDescriptor {
    std::string id;
    std::string title;
    std::string parent_id;
    std::string command_id;
    int sort_order = 0;
    Value metadata;
};

class UiPanelProvider {
public:
    virtual ~UiPanelProvider() = default;

    virtual std::string Id() const = 0;
    virtual std::vector<UiPanelDescriptor> Panels(WorkspaceContext& context) const = 0;
};

class UiActionProvider {
public:
    virtual ~UiActionProvider() = default;

    virtual std::string Id() const = 0;
    virtual std::vector<UiActionDescriptor> Actions(WorkspaceContext& context) const = 0;
};

class UiSettingsPageProvider {
public:
    virtual ~UiSettingsPageProvider() = default;

    virtual std::string Id() const = 0;
    virtual std::vector<UiSettingsPageDescriptor> SettingsPages(WorkspaceContext& context) const = 0;
};

class UiService {
public:
    static constexpr const char* kServiceId = "vanta.ide.ui";

    virtual ~UiService() = default;

    virtual RegistrationHandle RegisterPanelProvider(UiPanelProvider* provider) = 0;
    virtual RegistrationHandle RegisterActionProvider(UiActionProvider* provider) = 0;
    virtual RegistrationHandle RegisterSettingsPageProvider(UiSettingsPageProvider* provider) = 0;

    virtual std::vector<std::string> PanelProviderIds() const = 0;
    virtual std::vector<std::string> ActionProviderIds() const = 0;
    virtual std::vector<std::string> SettingsPageProviderIds() const = 0;

    virtual std::vector<UiPanelDescriptor> Panels(WorkspaceContext& context) const = 0;
    virtual std::vector<UiActionDescriptor> Actions(WorkspaceContext& context) const = 0;
    virtual std::vector<UiSettingsPageDescriptor> SettingsPages(WorkspaceContext& context) const = 0;
};

}
