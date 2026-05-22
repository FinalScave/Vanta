#pragma once

#include <map>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/core/value.h"
#include "vanta/workspace/diagnostic_service.h"
#include "vanta/execution/execution_service.h"
#include "vanta/project/component.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class WorkspaceContext;
struct ExecutionTarget;

struct RunConfigurationField {
    std::string id;
    std::string title;
    std::string kind;
    Value default_value;
    std::vector<Value> options;
    bool required = false;
};

struct ValidationResult {
    bool ok = true;
    std::vector<std::string> messages;
};

class RunConfigurationData {
public:
    virtual ~RunConfigurationData() = default;

    virtual std::unique_ptr<RunConfigurationData> Clone() const = 0;
};

struct RunConfiguration {
    RunConfiguration() = default;
    RunConfiguration(const RunConfiguration& other);
    RunConfiguration& operator=(const RunConfiguration& other);
    RunConfiguration(RunConfiguration&&) noexcept = default;
    RunConfiguration& operator=(RunConfiguration&&) noexcept = default;
    ~RunConfiguration() = default;

    std::string id;
    std::string name;
    std::string provider_id;
    std::string target_id;
    std::unique_ptr<RunConfigurationData> data;
    bool temporary = false;
};

class CustomCommandRunConfigurationData final : public RunConfigurationData {
public:
    std::string executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;

    std::unique_ptr<RunConfigurationData> Clone() const override;
    static std::unique_ptr<RunConfigurationData> FromValue(const Value& value);
};

struct RunResult {
    int exit_code = -1;
    std::string output;
    std::vector<Diagnostic> diagnostics;
    JobId job_id = 0;
};

struct RunExecutionContext {
    WorkspaceContext& workspace;
    JobId job_id = 0;
    ExecutionTarget target;
};

class RunConfigurationProvider {
public:
    virtual ~RunConfigurationProvider() = default;

    virtual std::string Id() const = 0;
    virtual std::string Title() const = 0;
    virtual std::string Category() const;
    virtual RunConfiguration Create(
        WorkspaceContext& context,
        const VirtualFile& focus_file = VirtualFile(),
        const std::string& name_hint = "") const = 0;
    virtual std::vector<RunConfiguration> Discover(WorkspaceContext& context, const VirtualFile& focus_file) const;
    virtual std::unique_ptr<RunConfigurationData> LoadData(const Value& value) const = 0;
    virtual Value SaveData(const RunConfigurationData& data) const = 0;
    virtual std::vector<RunConfigurationField> Fields(WorkspaceContext& context, const RunConfiguration& configuration) const;
    virtual Value GetFieldValue(const RunConfigurationData& data, std::string_view field_id) const;
    virtual bool SetFieldValue(RunConfigurationData& data, std::string_view field_id, const Value& value) const;
    virtual ValidationResult Validate(WorkspaceContext& context, const RunConfiguration& configuration) const = 0;
    virtual RunResult Run(RunExecutionContext& context, const RunConfiguration& configuration) const = 0;
};

class RunConfigurationService {
public:
    static constexpr const char* kServiceId = "vanta.runConfigurations";

    virtual ~RunConfigurationService() = default;

    virtual RegistrationHandle RegisterProvider(std::unique_ptr<RunConfigurationProvider> provider) = 0;
    virtual void RemoveProvider(const std::string& provider_id) = 0;
    virtual RunConfigurationProvider* Provider(const std::string& provider_id) const = 0;
    virtual std::vector<std::string> ProviderIds() const = 0;

    virtual RunConfiguration Create(
        WorkspaceContext& context,
        const std::string& provider_id,
        const VirtualFile& focus_file = VirtualFile(),
        const std::string& name_hint = "") const = 0;
    virtual std::vector<RunConfiguration> Discover(WorkspaceContext& context, const VirtualFile& focus_file) const = 0;
    virtual RunResult Run(
        WorkspaceContext& context,
        const RunConfiguration& configuration,
        const std::string& target_id = "") const = 0;
    virtual RunResult RunSaved(WorkspaceContext& context, const std::string& configuration_id, const std::string& target_id = "") const = 0;
};

class ProjectRunConfigurations final : public Component {
public:
    static constexpr const char* kComponentId = "vanta.project.runConfigurations";

    std::string Id() const override;
    void OnAttach(WorkspaceContext& context) override;
    void RestoreState(const Value& state) override;
    Value SaveState() const override;

    void AddConfiguration(RunConfiguration configuration);
    RegistrationHandle RegisterConfiguration(RunConfiguration configuration);
    bool RemoveConfiguration(const std::string& configuration_id);
    std::optional<RunConfiguration> Configuration(const std::string& configuration_id) const;
    std::vector<RunConfiguration> Configurations(bool include_temporary = false) const;
    void SetConfigurations(std::vector<RunConfiguration> configurations);

private:
    WorkspaceContext* context_ = nullptr;
    std::map<std::string, RunConfiguration> configurations_;
};

void RegisterDefaultRunConfigurations(RunConfigurationService& catalog);

}
