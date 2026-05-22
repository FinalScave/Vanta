#include "execution/run_configuration_service_impl.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>

#include "internal/projection.h"
#include "vanta/project/project.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace_runtime.h"

namespace vanta {
namespace {

ValidationResult Missing(const std::string& message) {
  return {
      .ok = false,
      .messages = {message},
  };
}

std::vector<std::string> StringsFromValue(const Value& value) {
  std::vector<std::string> values;
  if (!value.IsArray()) {
    return values;
  }
  for (const Value& item : value.AsArray()) {
    if (item.IsString()) {
      values.push_back(item.AsString());
    }
  }
  return values;
}

Value StringsProjection(const std::vector<std::string>& values) {
  Value::Array array;
  for (const std::string& value : values) {
    array.push_back(Value(value));
  }
  return Value::ArrayValue(std::move(array));
}

std::string SanitizeId(std::string value) {
  for (char& character : value) {
    const bool allowed = std::isalnum(static_cast<unsigned char>(character)) ||
                         character == '-' || character == '_' || character == '.';
    if (!allowed) {
      character = '_';
    }
  }
  return value.empty() ? "run" : value;
}

const CustomCommandRunConfigurationData* CustomCommandData(const RunConfiguration& configuration) {
  return dynamic_cast<const CustomCommandRunConfigurationData*>(configuration.data.get());
}

Value CustomCommandDataProjection(const CustomCommandRunConfigurationData& data) {
  return Value::ObjectValue({
      {"executable", Value(data.executable)},
      {"arguments", StringsProjection(data.arguments)},
      {"workingDirectory", Value(data.working_directory.string())},
  });
}

Value RunConfigurationState(const RunConfiguration& configuration, const RunConfigurationProvider* provider) {
  return Value::ObjectValue({
      {"id", Value(configuration.id)},
      {"name", Value(configuration.name)},
      {"providerId", Value(configuration.provider_id)},
      {"targetId", Value(configuration.target_id)},
      {"data", provider == nullptr || configuration.data == nullptr ? Value::ObjectValue()
                                                                     : provider->SaveData(*configuration.data)},
      {"temporary", Value(configuration.temporary)},
  });
}

class CustomCommandRunConfigurationProvider final : public RunConfigurationProvider {
public:
  std::string Id() const override { return "custom.command"; }

  std::string Title() const override { return "Custom Command"; }

  RunConfiguration Create(
      WorkspaceContext& context,
      const VirtualFile& focus_file,
      const std::string& name_hint) const override {
    (void)focus_file;
    auto data = std::make_unique<CustomCommandRunConfigurationData>();
    data->working_directory = context.CurrentWorkspace().Info().root_path;

    RunConfiguration configuration;
    configuration.id = name_hint.empty() ? "custom.command" : "custom." + SanitizeId(name_hint);
    configuration.name = name_hint.empty() ? Title() : name_hint;
    configuration.provider_id = Id();
    configuration.target_id = "local.default";
    configuration.data = std::move(data);
    return configuration;
  }

  std::unique_ptr<RunConfigurationData> LoadData(const Value& value) const override {
    return CustomCommandRunConfigurationData::FromValue(value);
  }

  Value SaveData(const RunConfigurationData& data) const override {
    const auto* custom_data = dynamic_cast<const CustomCommandRunConfigurationData*>(&data);
    return custom_data == nullptr ? Value::ObjectValue() : CustomCommandDataProjection(*custom_data);
  }

  std::vector<RunConfigurationField> Fields(WorkspaceContext& context, const RunConfiguration& configuration) const override {
    (void)context;
    (void)configuration;
    return {
        {.id = "executable",
         .title = "Executable",
         .kind = "string",
         .default_value = Value(""),
         .required = true},
        {.id = "arguments",
         .title = "Arguments",
         .kind = "stringArray",
         .default_value = Value::ArrayValue(),
         .required = false},
        {.id = "workingDirectory",
         .title = "Working Directory",
         .kind = "path",
         .default_value = Value(""),
         .required = false},
    };
  }

  Value GetFieldValue(const RunConfigurationData& data, std::string_view field_id) const override {
    const auto* custom_data = dynamic_cast<const CustomCommandRunConfigurationData*>(&data);
    if (custom_data == nullptr) {
      return nullptr;
    }
    if (field_id == "executable") {
      return Value(custom_data->executable);
    }
    if (field_id == "arguments") {
      return StringsProjection(custom_data->arguments);
    }
    if (field_id == "workingDirectory") {
      return Value(custom_data->working_directory.string());
    }
    return nullptr;
  }

  bool SetFieldValue(RunConfigurationData& data, std::string_view field_id, const Value& value) const override {
    auto* custom_data = dynamic_cast<CustomCommandRunConfigurationData*>(&data);
    if (custom_data == nullptr) {
      return false;
    }
    if (field_id == "executable" && value.IsString()) {
      custom_data->executable = value.AsString();
      return true;
    }
    if (field_id == "arguments" && value.IsArray()) {
      custom_data->arguments = StringsFromValue(value);
      return true;
    }
    if (field_id == "workingDirectory" && value.IsString()) {
      custom_data->working_directory = value.AsString();
      return true;
    }
    return false;
  }

  ValidationResult Validate(WorkspaceContext& context, const RunConfiguration& configuration) const override {
    (void)context;
    const CustomCommandRunConfigurationData* data = CustomCommandData(configuration);
    if (data == nullptr || data->executable.empty()) {
      return Missing("Executable is required");
    }
    return {};
  }

  RunResult Run(RunExecutionContext& context, const RunConfiguration& configuration) const override {
    const CustomCommandRunConfigurationData* data = CustomCommandData(configuration);
    if (data == nullptr) {
      return {.exit_code = -1, .output = "Run configuration data is invalid\n", .job_id = context.job_id};
    }
    std::filesystem::path working_directory = data->working_directory;
    if (working_directory.empty()) {
      working_directory = context.workspace.CurrentWorkspace().Info().root_path;
    }

    const ExecutionResult execution = context.workspace.Execution().Execute(
        context.workspace,
        {
            .executable = data->executable,
            .arguments = data->arguments,
            .working_directory = working_directory,
            .job_id = context.job_id,
        },
        context.target);
    return {
        .exit_code = execution.exit_code,
        .output = execution.output,
        .diagnostics = {},
        .job_id = context.job_id,
    };
  }
};

std::optional<RunConfiguration> ConfigurationFromValue(const Value& item, RunConfigurationProvider* provider) {
  if (!item.IsObject()) {
    return std::nullopt;
  }
  RunConfiguration configuration;
  configuration.id = item.StringValue("id").value_or("");
  configuration.name = item.StringValue("name").value_or(configuration.id);
  configuration.provider_id = item.StringValue("providerId").value_or("");
  configuration.target_id = item.StringValue("targetId").value_or("");
  if (provider != nullptr) {
    configuration.data = provider->LoadData(item.Contains("data") ? item["data"] : Value::ObjectValue());
  }
  configuration.temporary = item.BoolValue("temporary").value_or(configuration.temporary);
  if (configuration.id.empty() || configuration.provider_id.empty()) {
    return std::nullopt;
  }
  if (configuration.data == nullptr) {
    return std::nullopt;
  }
  return configuration;
}

} // namespace

std::string RunConfigurationProvider::Category() const {
  return {};
}

std::vector<RunConfiguration> RunConfigurationProvider::Discover(
    WorkspaceContext& context,
    const VirtualFile& focus_file) const {
  (void)context;
  (void)focus_file;
  return {};
}

std::vector<RunConfigurationField> RunConfigurationProvider::Fields(
    WorkspaceContext& context,
    const RunConfiguration& configuration) const {
  (void)context;
  (void)configuration;
  return {};
}

Value RunConfigurationProvider::GetFieldValue(
    const RunConfigurationData& data,
    std::string_view field_id) const {
  (void)data;
  (void)field_id;
  return nullptr;
}

bool RunConfigurationProvider::SetFieldValue(
    RunConfigurationData& data,
    std::string_view field_id,
    const Value& value) const {
  (void)data;
  (void)field_id;
  (void)value;
  return false;
}

std::unique_ptr<RunConfigurationData> CustomCommandRunConfigurationData::Clone() const {
  return std::make_unique<CustomCommandRunConfigurationData>(*this);
}

std::unique_ptr<RunConfigurationData> CustomCommandRunConfigurationData::FromValue(const Value& value) {
  auto data = std::make_unique<CustomCommandRunConfigurationData>();
  if (!value.IsObject()) {
    return data;
  }
  data->executable = value.StringValue("executable").value_or("");
  if (value.Contains("arguments")) {
    data->arguments = StringsFromValue(value["arguments"]);
  }
  if (auto working_directory = value.StringValue("workingDirectory")) {
    data->working_directory = *working_directory;
  }
  return data;
}

RunConfiguration::RunConfiguration(const RunConfiguration& other)
    : id(other.id),
      name(other.name),
      provider_id(other.provider_id),
      target_id(other.target_id),
      data(other.data == nullptr ? nullptr : other.data->Clone()),
      temporary(other.temporary) {}

RunConfiguration& RunConfiguration::operator=(const RunConfiguration& other) {
  if (this == &other) {
    return *this;
  }
  id = other.id;
  name = other.name;
  provider_id = other.provider_id;
  target_id = other.target_id;
  data = other.data == nullptr ? nullptr : other.data->Clone();
  temporary = other.temporary;
  return *this;
}

RegistrationHandle internal::RunConfigurationServiceImpl::RegisterProvider(
    std::unique_ptr<RunConfigurationProvider> provider) {
  if (provider == nullptr || provider->Id().empty()) {
    return {};
  }
  const std::string id = provider->Id();
  providers_[id] = std::move(provider);
  return RegistrationHandle([this, id] {
    RemoveProvider(id);
  });
}

void internal::RunConfigurationServiceImpl::RemoveProvider(const std::string& provider_id) {
  providers_.erase(provider_id);
}

RunConfigurationProvider*
internal::RunConfigurationServiceImpl::Provider(const std::string& provider_id) const {
  auto it = providers_.find(provider_id);
  return it == providers_.end() ? nullptr : it->second.get();
}

std::vector<std::string> internal::RunConfigurationServiceImpl::ProviderIds() const {
  std::vector<std::string> ids;
  for (const auto& [id, provider] : providers_) {
    (void)provider;
    ids.push_back(id);
  }
  return ids;
}

RunConfiguration internal::RunConfigurationServiceImpl::Create(
    WorkspaceContext& context,
    const std::string& provider_id,
    const VirtualFile& focus_file,
    const std::string& name_hint) const {
  RunConfigurationProvider* provider = Provider(provider_id);
  if (provider == nullptr) {
    return {};
  }
  RunConfiguration configuration = provider->Create(context, focus_file, name_hint);
  if (configuration.provider_id.empty()) {
    configuration.provider_id = provider_id;
  }
  if (configuration.id.empty()) {
    configuration.id = provider_id;
  }
  if (configuration.name.empty()) {
    configuration.name = provider->Title();
  }
  return configuration;
}

std::vector<RunConfiguration>
internal::RunConfigurationServiceImpl::Discover(WorkspaceContext& context, const VirtualFile& focus_file) const {
  std::vector<RunConfiguration> values;
  for (const auto& [id, provider] : providers_) {
    std::vector<RunConfiguration> discovered = provider->Discover(context, focus_file);
    for (RunConfiguration& configuration : discovered) {
      if (configuration.provider_id.empty()) {
        configuration.provider_id = id;
      }
      values.push_back(std::move(configuration));
    }
  }
  return values;
}

RunResult internal::RunConfigurationServiceImpl::Run(
    WorkspaceContext& context,
    const RunConfiguration& configuration,
    const std::string& target_id) const {
  RunConfigurationProvider* provider = Provider(configuration.provider_id);
  if (provider == nullptr) {
    return {.exit_code = -1, .output = "Run configuration provider not found\n"};
  }
  if (configuration.data == nullptr) {
    return {.exit_code = -1, .output = "Run configuration data is invalid\n"};
  }

  const ValidationResult validation = provider->Validate(context, configuration);
  if (!validation.ok) {
    std::ostringstream output;
    for (const std::string& message : validation.messages) {
      output << message << '\n';
    }
    return {.exit_code = -1, .output = output.str()};
  }

  std::vector<ExecutionTarget> available_targets = context.Execution().Targets(context);
  const std::string resolved_target_id = target_id.empty() ? configuration.target_id : target_id;
  auto target_it = available_targets.begin();
  if (!resolved_target_id.empty()) {
    target_it = std::find_if(available_targets.begin(), available_targets.end(),
                             [&](const ExecutionTarget& target) {
                               return target.id == resolved_target_id;
                             });
  }
  if (target_it == available_targets.end()) {
    return {.exit_code = -1, .output = "Run target not found\n"};
  }

  auto result_promise = std::make_shared<std::promise<RunResult>>();
  std::future<RunResult> result_future = result_promise->get_future();
  const ExecutionTarget target = *target_it;
  JobHandle handle = context.Jobs().Submit(
      {
          .kind = JobKind::Run,
          .title = configuration.name.empty() ? configuration.id : configuration.name,
      },
      [&context, provider, configuration, target, result_promise](JobContext& job) mutable {
        try {
          job.Report(0.1, "Run started");
          RunExecutionContext execution_context{
              .workspace = context,
              .job_id = job.Id(),
              .target = target,
          };
          RunResult result = provider->Run(execution_context, configuration);
          result.job_id = job.Id();
          if (!context.Jobs().IsTerminal(job.Id()) && !result.output.empty()) {
            job.AppendOutput(result.output);
          }
          result_promise->set_value(result);
          return JobResult{
              .success = result.exit_code == 0,
              .payload = internal::RunResultProjection(result),
          };
        } catch (const std::exception& error) {
          RunResult result{
              .exit_code = -1,
              .output = std::string(error.what()) + "\n",
              .job_id = job.Id(),
          };
          result_promise->set_value(result);
          return JobResult{
              .success = false,
              .message = error.what(),
              .payload = internal::RunResultProjection(result),
          };
        } catch (...) {
          RunResult result{
              .exit_code = -1,
              .output = "Run failed\n",
              .job_id = job.Id(),
          };
          result_promise->set_value(result);
          return JobResult{
              .success = false,
              .message = "Run failed",
              .payload = internal::RunResultProjection(result),
          };
        }
      });
  RunResult result = result_future.get();
  handle.Wait();
  return result;
}

RunResult internal::RunConfigurationServiceImpl::RunSaved(
    WorkspaceContext& context,
    const std::string& configuration_id,
    const std::string& target_id) const {
  const Project* project = context.CurrentProject();
  const ProjectRunConfigurations* configurations =
      project == nullptr ? nullptr : project->GetComponent<ProjectRunConfigurations>(ProjectRunConfigurations::kComponentId);
  const auto configuration_value =
      configurations == nullptr ? std::optional<RunConfiguration>() : configurations->Configuration(configuration_id);
  if (!configuration_value) {
    return {.exit_code = -1, .output = "Run configuration not found\n"};
  }
  return Run(context, *configuration_value, target_id);
}

std::string ProjectRunConfigurations::Id() const { return kComponentId; }

void ProjectRunConfigurations::OnAttach(WorkspaceContext& context) {
  context_ = &context;
}

void ProjectRunConfigurations::RestoreState(const Value& state) {
  configurations_.clear();
  if (!state.IsObject() || !state.Contains("configurations") ||
      !state["configurations"].IsArray()) {
    return;
  }
  for (const Value& item : state["configurations"].AsArray()) {
    const std::string provider_id = item.IsObject() ? item.StringValue("providerId").value_or("") : "";
    RunConfigurationProvider* provider = context_ == nullptr ? nullptr : context_->RunConfigurations().Provider(provider_id);
    if (auto configuration = ConfigurationFromValue(item, provider)) {
      configuration->temporary = false;
      AddConfiguration(std::move(*configuration));
    }
  }
}

Value ProjectRunConfigurations::SaveState() const {
  Value::Array configurations;
  for (const RunConfiguration& configuration : this->Configurations(false)) {
    RunConfigurationProvider* provider =
        context_ == nullptr ? nullptr : context_->RunConfigurations().Provider(configuration.provider_id);
    configurations.push_back(RunConfigurationState(configuration, provider));
  }
  return Value::ObjectValue({
      {"schemaVersion", Value(static_cast<std::int64_t>(1))},
      {"configurations", Value::ArrayValue(std::move(configurations))},
  });
}

void ProjectRunConfigurations::AddConfiguration(RunConfiguration configuration) {
  if (configuration.id.empty() || configuration.provider_id.empty()) {
    return;
  }
  if (configuration.data == nullptr && context_ != nullptr) {
    if (RunConfigurationProvider* provider = context_->RunConfigurations().Provider(configuration.provider_id)) {
      RunConfiguration default_configuration = provider->Create(*context_, {}, configuration.name);
      configuration.data = std::move(default_configuration.data);
    }
  }
  if (configuration.data == nullptr) {
    return;
  }
  configurations_[configuration.id] = std::move(configuration);
}

RegistrationHandle ProjectRunConfigurations::RegisterConfiguration(RunConfiguration configuration) {
  if (configuration.id.empty()) {
    return {};
  }
  const std::string id = configuration.id;
  AddConfiguration(std::move(configuration));
  return RegistrationHandle([this, id] {
    RemoveConfiguration(id);
  });
}

bool ProjectRunConfigurations::RemoveConfiguration(const std::string& configuration_id) {
  return configurations_.erase(configuration_id) > 0;
}

std::optional<RunConfiguration>
ProjectRunConfigurations::Configuration(const std::string& configuration_id) const {
  auto it = configurations_.find(configuration_id);
  return it == configurations_.end() ? std::nullopt : std::optional<RunConfiguration>(it->second);
}

std::vector<RunConfiguration>
ProjectRunConfigurations::Configurations(bool include_temporary) const {
  std::vector<RunConfiguration> values;
  for (const auto& [id, configuration] : configurations_) {
    (void)id;
    if (include_temporary || !configuration.temporary) {
      values.push_back(configuration);
    }
  }
  return values;
}

void ProjectRunConfigurations::SetConfigurations(std::vector<RunConfiguration> configurations) {
  configurations_.clear();
  for (RunConfiguration& configuration : configurations) {
    AddConfiguration(std::move(configuration));
  }
}

void RegisterDefaultRunConfigurations(RunConfigurationService& catalog) {
  catalog.RegisterProvider(std::make_unique<CustomCommandRunConfigurationProvider>());
}

} // namespace vanta
