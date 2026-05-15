#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/workspace/diagnostic_service.h"
#include "vanta/execution/execution_service.h"
#include "vanta/platform/async.h"
#include "vanta/platform/json.h"
#include "vanta/project/component.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class WorkspaceContext;
struct ExecutionTarget;

struct ConfigurationField {
    std::string id;
    std::string title;
    std::string type;
    Json defaultValue;
    bool required = false;
};

struct ValidationResult {
    bool ok = true;
    std::vector<std::string> messages;
};

struct RunConfiguration {
    std::string id;
    std::string name;
    std::string typeId;
    std::string targetId;
    Json data;
    bool temporary = false;
};

struct RunResult {
    int exitCode = -1;
    std::string output;
    std::vector<Diagnostic> diagnostics;
    JobId jobId = 0;
};

struct RunExecutionContext {
    WorkspaceContext& workspace;
    JobId jobId = 0;
    ExecutionTarget target;
};

class RunConfigurationType {
public:
    virtual ~RunConfigurationType() = default;

    virtual std::string id() const = 0;
    virtual std::string title() const = 0;
    virtual Json defaultData(WorkspaceContext& context) const = 0;
    virtual std::vector<ConfigurationField> fields() const = 0;
    virtual ValidationResult validate(WorkspaceContext& context, const RunConfiguration& configuration) const = 0;
    virtual RunResult run(RunExecutionContext& context, const RunConfiguration& configuration) const = 0;
};

class RunConfigurationProducer {
public:
    virtual ~RunConfigurationProducer() = default;

    virtual std::string id() const = 0;
    virtual std::vector<RunConfiguration> produce(WorkspaceContext& context, const VirtualFile& focusFile) const = 0;
};

class RunConfigurationService {
public:
    void addType(std::unique_ptr<RunConfigurationType> type);
    void removeType(const std::string& typeId);
    RunConfigurationType* type(const std::string& typeId) const;
    std::vector<std::string> typeIds() const;

    void addProducer(std::unique_ptr<RunConfigurationProducer> producer);
    void removeProducer(const std::string& producerId);
    std::vector<std::string> producerIds() const;

    void addConfiguration(RunConfiguration configuration);
    bool removeConfiguration(const std::string& configurationId);
    std::optional<RunConfiguration> configuration(const std::string& configurationId) const;
    std::vector<RunConfiguration> configurations(bool includeTemporary = false) const;
    void setConfigurations(std::vector<RunConfiguration> configurations);

    std::vector<RunConfiguration> produce(WorkspaceContext& context, const VirtualFile& focusFile) const;
    RunResult run(WorkspaceContext& context, const std::string& configurationId, const std::string& targetId = "") const;

private:
    std::map<std::string, std::unique_ptr<RunConfigurationType>> types_;
    std::map<std::string, std::unique_ptr<RunConfigurationProducer>> producers_;
    std::map<std::string, RunConfiguration> configurations_;
};

class RunConfigurationComponent final : public Component {
public:
    static constexpr const char* componentId = "vanta.run.configurations";

    std::string id() const override;
    void onAttach(WorkspaceContext& context) override;
    void restoreState(const Json& state) override;
    Json saveState() const override;
    void onDetach() override;

private:
    RunConfigurationService* service_ = nullptr;
    std::vector<RunConfiguration> restoredConfigurations_;
};

void registerDefaultRunConfigurationProviders(RunConfigurationService& service);
Json toJson(const ConfigurationField& field);
Json toJson(const RunConfiguration& configuration);
Json toJson(const RunResult& result);

}
