#include "vanta/execution/run_configuration.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <future>
#include <memory>
#include <sstream>
#include <utility>

#include "vanta/execution/problem_matcher.h"
#include "vanta/project/project.h"
#include "vanta/project/project_manager.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace_runtime.h"

namespace vanta {
namespace {

std::vector<std::string> stringArrayFromJson(const Json &object,
                                             const std::string &key) {
  std::vector<std::string> values;
  if (!object.isObject() || !object.contains(key) || !object[key].isArray()) {
    return values;
  }
  for (const Json &item : object[key].asArray()) {
    if (item.isString()) {
      values.push_back(item.asString());
    }
  }
  return values;
}

std::optional<std::string> optionalString(const Json &object,
                                          const std::string &key) {
  if (!object.isObject()) {
    return std::nullopt;
  }
  return object.stringValue(key);
}

std::string dataString(const RunConfiguration &configuration,
                       const std::string &key,
                       const std::string &fallback = {}) {
  return optionalString(configuration.data, key).value_or(fallback);
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

std::string normalizedExtension(const VirtualFile &file) {
  std::string extension = lowercase(file.extension());
  if (!extension.empty() && extension.front() == '.') {
    extension.erase(extension.begin());
  }
  return extension;
}

bool isCppFile(const VirtualFile &file) {
  const std::string extension = normalizedExtension(file);
  return extension == "cpp" || extension == "cxx" || extension == "cc" ||
         extension == "c";
}

bool isPythonFile(const VirtualFile &file) {
  return normalizedExtension(file) == "py";
}

std::string defaultStandard(const VirtualFile &file) {
  return normalizedExtension(file) == "c" ? "c17" : "c++20";
}

std::string sanitizeId(std::string value) {
  for (char &character : value) {
    const bool allowed = std::isalnum(static_cast<unsigned char>(character)) ||
                         character == '-' || character == '_' ||
                         character == '.';
    if (!allowed) {
      character = '_';
    }
  }
  return value.empty() ? "file" : value;
}

ValidationResult missing(const std::string &message) {
  return {
      .ok = false,
      .messages = {message},
  };
}

Json diagnosticToJson(const Diagnostic &diagnostic) {
  return Json::object({
      {"file", Json(diagnostic.location.file.toUri().string())},
      {"line", Json(static_cast<std::int64_t>(diagnostic.location.line))},
      {"column", Json(static_cast<std::int64_t>(diagnostic.location.column))},
      {"severity", Json(toString(diagnostic.severity))},
      {"source", Json(diagnostic.source)},
      {"message", Json(diagnostic.message)},
  });
}

class CustomCommandRunConfigurationType final : public RunConfigurationType {
public:
  std::string id() const override { return "custom.command"; }

  std::string title() const override { return "Custom Command"; }

  Json defaultData(WorkspaceContext &context) const override {
    return Json::object({
        {"executable", Json("")},
        {"arguments", Json::array()},
        {"workingDirectory",
         Json(context.workspace().info().rootPath.string())},
    });
  }

  std::vector<ConfigurationField> fields() const override {
    return {
        {.id = "executable",
         .title = "Executable",
         .type = "string",
         .defaultValue = Json(""),
         .required = true},
        {.id = "arguments",
         .title = "Arguments",
         .type = "stringArray",
         .defaultValue = Json::array(),
         .required = false},
        {.id = "workingDirectory",
         .title = "Working Directory",
         .type = "path",
         .defaultValue = Json(""),
         .required = false},
    };
  }

  ValidationResult
  validate(WorkspaceContext &context,
           const RunConfiguration &configuration) const override {
    (void)context;
    if (dataString(configuration, "executable").empty()) {
      return missing("Executable is required");
    }
    return {};
  }

  RunResult run(RunExecutionContext &context,
                const RunConfiguration &configuration) const override {
    std::filesystem::path workingDirectory =
        dataString(configuration, "workingDirectory");
    if (workingDirectory.empty()) {
      workingDirectory = context.workspace.workspace().info().rootPath;
    }

    const ExecutionResult execution = context.workspace.execution().execute(
        {
            .executable = dataString(configuration, "executable"),
            .arguments = stringArrayFromJson(configuration.data, "arguments"),
            .workingDirectory = workingDirectory,
            .jobId = context.jobId,
        },
        context.target);
    return {
        .exitCode = execution.exitCode,
        .output = execution.output,
        .diagnostics = {},
        .jobId = context.jobId,
    };
  }
};

class CppSingleFileRunConfigurationType final : public RunConfigurationType {
public:
  std::string id() const override { return "cpp.singleFile"; }

  std::string title() const override { return "C++ Single File"; }

  Json defaultData(WorkspaceContext &context) const override {
    return Json::object({
        {"file", Json("")},
        {"standard", Json("c++20")},
        {"arguments", Json::array()},
        {"workingDirectory",
         Json(context.workspace().info().rootPath.string())},
    });
  }

  std::vector<ConfigurationField> fields() const override {
    return {
        {.id = "file",
         .title = "File",
         .type = "file",
         .defaultValue = Json(""),
         .required = true},
        {.id = "standard",
         .title = "Standard",
         .type = "string",
         .defaultValue = Json("c++20"),
         .required = false},
        {.id = "arguments",
         .title = "Arguments",
         .type = "stringArray",
         .defaultValue = Json::array(),
         .required = false},
        {.id = "workingDirectory",
         .title = "Working Directory",
         .type = "path",
         .defaultValue = Json(""),
         .required = false},
    };
  }

  ValidationResult
  validate(WorkspaceContext &context,
           const RunConfiguration &configuration) const override {
    const VirtualFile file =
        context.workspace().file(dataString(configuration, "file"));
    if (!file.valid() || !file.exists()) {
      return missing("File does not exist");
    }
    if (!isCppFile(file)) {
      return missing("File is not a C or C++ source file");
    }
    return {};
  }

  RunResult run(RunExecutionContext &context,
                const RunConfiguration &configuration) const override {
    const VirtualFile file =
        context.workspace.workspace().file(dataString(configuration, "file"));
    const auto localPath = file.localPath();
    if (!localPath) {
      return {.exitCode = -1,
              .output = "File is not backed by a local path\n",
              .jobId = context.jobId};
    }

    std::filesystem::path workingDirectory =
        dataString(configuration, "workingDirectory");
    if (workingDirectory.empty()) {
      workingDirectory = localPath->parent_path();
    }

    std::error_code error;
    const std::filesystem::path runDirectory =
        context.workspace.workspace().info().rootPath / ".vanta" / "run";
    std::filesystem::create_directories(runDirectory, error);
    if (error) {
      return {.exitCode = -1,
              .output = "Could not create run directory\n",
              .jobId = context.jobId};
    }

    std::filesystem::path executablePath =
        runDirectory / sanitizeId(localPath->stem().string());
#ifdef _WIN32
    executablePath += ".exe";
#endif

    std::vector<std::string> compileArguments;
    compileArguments.push_back(
        "-std=" + dataString(configuration, "standard", defaultStandard(file)));
    compileArguments.push_back(localPath->string());
    compileArguments.push_back("-o");
    compileArguments.push_back(executablePath.string());
    const ExecutionResult compile = context.workspace.execution().execute(
        {
            .executable = normalizedExtension(file) == "c" ? "cc" : "c++",
            .arguments = compileArguments,
            .workingDirectory = workingDirectory,
            .jobId = context.jobId,
        },
        context.target);

    RunResult result;
    result.exitCode = compile.exitCode;
    result.output = compile.output;
    result.jobId = context.jobId;
    const ProblemMatcher matcher;
    const DiagnosticResolver resolver;
    result.diagnostics =
        resolver.resolve(matcher.matchCompilerOutput(result.output),
                         context.workspace.workspace(), workingDirectory);
    if (compile.exitCode != 0) {
      return result;
    }

    const ExecutionResult run = context.workspace.execution().execute(
        {
            .executable = executablePath.string(),
            .arguments = stringArrayFromJson(configuration.data, "arguments"),
            .workingDirectory = workingDirectory,
            .jobId = context.jobId,
        },
        context.target);
    result.exitCode = run.exitCode;
    result.output += run.output;
    return result;
  }
};

class PythonScriptRunConfigurationType final : public RunConfigurationType {
public:
  std::string id() const override { return "python.script"; }

  std::string title() const override { return "Python Script"; }

  Json defaultData(WorkspaceContext &context) const override {
    return Json::object({
        {"file", Json("")},
        {"arguments", Json::array()},
        {"workingDirectory",
         Json(context.workspace().info().rootPath.string())},
    });
  }

  std::vector<ConfigurationField> fields() const override {
    return {
        {.id = "file",
         .title = "File",
         .type = "file",
         .defaultValue = Json(""),
         .required = true},
        {.id = "arguments",
         .title = "Arguments",
         .type = "stringArray",
         .defaultValue = Json::array(),
         .required = false},
        {.id = "workingDirectory",
         .title = "Working Directory",
         .type = "path",
         .defaultValue = Json(""),
         .required = false},
    };
  }

  ValidationResult
  validate(WorkspaceContext &context,
           const RunConfiguration &configuration) const override {
    const VirtualFile file =
        context.workspace().file(dataString(configuration, "file"));
    if (!file.valid() || !file.exists()) {
      return missing("File does not exist");
    }
    if (!isPythonFile(file)) {
      return missing("File is not a Python script");
    }
    return {};
  }

  RunResult run(RunExecutionContext &context,
                const RunConfiguration &configuration) const override {
    const VirtualFile file =
        context.workspace.workspace().file(dataString(configuration, "file"));
    const auto localPath = file.localPath();
    if (!localPath) {
      return {.exitCode = -1,
              .output = "File is not backed by a local path\n",
              .jobId = context.jobId};
    }
    std::filesystem::path workingDirectory =
        dataString(configuration, "workingDirectory");
    if (workingDirectory.empty()) {
      workingDirectory = localPath->parent_path();
    }

    std::vector<std::string> arguments;
    arguments.push_back(localPath->string());
    for (const std::string &argument :
         stringArrayFromJson(configuration.data, "arguments")) {
      arguments.push_back(argument);
    }
    const ExecutionResult execution = context.workspace.execution().execute(
        {
            .executable = "python3",
            .arguments = arguments,
            .workingDirectory = workingDirectory,
            .jobId = context.jobId,
        },
        context.target);
    return {
        .exitCode = execution.exitCode,
        .output = execution.output,
        .diagnostics = {},
        .jobId = context.jobId,
    };
  }
};

class SingleFileRunConfigurationProducer final
    : public RunConfigurationProducer {
public:
  std::string id() const override { return "vanta.singleFileRunProducer"; }

  std::vector<RunConfiguration>
  produce(WorkspaceContext &context,
          const VirtualFile &focusFile) const override {
    VirtualFile file = focusFile;
    if (!file.valid()) {
      if (const auto *singleFile =
              context.requireProject().model().attachment<SingleFileModel>(
                  SingleFileModel::attachmentId)) {
        file = singleFile->file;
      }
    }
    if (!file.valid()) {
      return {};
    }
    const auto localPath = file.localPath();
    if (!localPath) {
      return {};
    }

    std::vector<RunConfiguration> configurations;
    if (isCppFile(file)) {
      configurations.push_back({
          .id = "temp.cpp." + sanitizeId(file.toUri().string()),
          .name = "Run " + file.displayName(),
          .typeId = "cpp.singleFile",
          .targetId = "local.default",
          .data = Json::object({
              {"file", Json(localPath->string())},
              {"standard", Json(defaultStandard(file))},
              {"arguments", Json::array()},
              {"workingDirectory", Json(localPath->parent_path().string())},
          }),
          .temporary = true,
      });
    } else if (isPythonFile(file)) {
      configurations.push_back({
          .id = "temp.python." + sanitizeId(file.toUri().string()),
          .name = "Run " + file.displayName(),
          .typeId = "python.script",
          .targetId = "local.default",
          .data = Json::object({
              {"file", Json(localPath->string())},
              {"arguments", Json::array()},
              {"workingDirectory", Json(localPath->parent_path().string())},
          }),
          .temporary = true,
      });
    }
    return configurations;
  }
};

std::optional<RunConfiguration> configurationFromJson(const Json &item) {
  if (!item.isObject()) {
    return std::nullopt;
  }
  RunConfiguration configuration;
  configuration.id = item.stringValue("id").value_or("");
  configuration.name = item.stringValue("name").value_or(configuration.id);
  configuration.typeId = item.stringValue("typeId").value_or("");
  configuration.targetId = item.stringValue("targetId").value_or("");
  if (item.contains("data") && item["data"].isObject()) {
    configuration.data = item["data"];
  } else {
    configuration.data = Json::object();
  }
  if (item.contains("temporary") && item["temporary"].isBool()) {
    configuration.temporary = item["temporary"].asBool();
  }
  if (configuration.id.empty() || configuration.typeId.empty()) {
    return std::nullopt;
  }
  return configuration;
}

} // namespace

void RunConfigurationService::addType(
    std::unique_ptr<RunConfigurationType> type) {
  if (type == nullptr || type->id().empty()) {
    return;
  }
  types_[type->id()] = std::move(type);
}

void RunConfigurationService::removeType(const std::string &typeId) {
  types_.erase(typeId);
}

RunConfigurationType *
RunConfigurationService::type(const std::string &typeId) const {
  auto it = types_.find(typeId);
  return it == types_.end() ? nullptr : it->second.get();
}

std::vector<std::string> RunConfigurationService::typeIds() const {
  std::vector<std::string> ids;
  for (const auto &[id, type] : types_) {
    (void)type;
    ids.push_back(id);
  }
  return ids;
}

void RunConfigurationService::addProducer(
    std::unique_ptr<RunConfigurationProducer> producer) {
  if (producer == nullptr || producer->id().empty()) {
    return;
  }
  producers_[producer->id()] = std::move(producer);
}

void RunConfigurationService::removeProducer(const std::string &producerId) {
  producers_.erase(producerId);
}

std::vector<std::string> RunConfigurationService::producerIds() const {
  std::vector<std::string> ids;
  for (const auto &[id, producer] : producers_) {
    (void)producer;
    ids.push_back(id);
  }
  return ids;
}

void RunConfigurationService::addConfiguration(RunConfiguration configuration) {
  if (configuration.id.empty() || configuration.typeId.empty()) {
    return;
  }
  configurations_[configuration.id] = std::move(configuration);
}

bool RunConfigurationService::removeConfiguration(
    const std::string &configurationId) {
  return configurations_.erase(configurationId) > 0;
}

std::optional<RunConfiguration> RunConfigurationService::configuration(
    const std::string &configurationId) const {
  auto it = configurations_.find(configurationId);
  return it == configurations_.end()
             ? std::nullopt
             : std::optional<RunConfiguration>(it->second);
}

std::vector<RunConfiguration>
RunConfigurationService::configurations(bool includeTemporary) const {
  std::vector<RunConfiguration> values;
  for (const auto &[id, configuration] : configurations_) {
    (void)id;
    if (includeTemporary || !configuration.temporary) {
      values.push_back(configuration);
    }
  }
  return values;
}

void RunConfigurationService::setConfigurations(
    std::vector<RunConfiguration> configurations) {
  configurations_.clear();
  for (RunConfiguration &configuration : configurations) {
    addConfiguration(std::move(configuration));
  }
}

std::vector<RunConfiguration>
RunConfigurationService::produce(WorkspaceContext &context,
                                 const VirtualFile &focusFile) const {
  std::vector<RunConfiguration> values;
  for (const auto &[id, producer] : producers_) {
    (void)id;
    std::vector<RunConfiguration> produced =
        producer->produce(context, focusFile);
    values.insert(values.end(), produced.begin(), produced.end());
  }
  return values;
}

RunResult RunConfigurationService::run(WorkspaceContext &context,
                                       const std::string &configurationId,
                                       const std::string &targetId) const {
  const auto configurationValue = configuration(configurationId);
  if (!configurationValue) {
    return {.exitCode = -1, .output = "Run configuration not found\n"};
  }
  const RunConfiguration &configuration = *configurationValue;
  RunConfigurationType *typeValue = type(configuration.typeId);
  if (typeValue == nullptr) {
    return {.exitCode = -1, .output = "Run configuration type not found\n"};
  }

  const ValidationResult validation =
      typeValue->validate(context, configuration);
  if (!validation.ok) {
    std::ostringstream output;
    for (const std::string &message : validation.messages) {
      output << message << '\n';
    }
    return {.exitCode = -1, .output = output.str()};
  }

  std::vector<ExecutionTarget> availableTargets = context.execution().targets();
  const std::string resolvedTargetId =
      targetId.empty() ? configuration.targetId : targetId;
  auto targetIt = availableTargets.begin();
  if (!resolvedTargetId.empty()) {
    targetIt = std::find_if(availableTargets.begin(), availableTargets.end(),
                            [&](const ExecutionTarget &target) {
                              return target.id == resolvedTargetId;
                            });
  }
  if (targetIt == availableTargets.end()) {
    return {.exitCode = -1, .output = "Run target not found\n"};
  }

  auto resultPromise = std::make_shared<std::promise<RunResult>>();
  std::future<RunResult> resultFuture = resultPromise->get_future();
  const ExecutionTarget target = *targetIt;
  JobHandle handle = context.jobs().submit(
      context.runtime()->async(),
      {
          .kind = JobKind::Run,
          .title = configuration.name.empty() ? configuration.id
                                              : configuration.name,
      },
      [&context, typeValue, configuration, target,
       resultPromise](JobContext &job) mutable {
        try {
          job.report(0.1, "Run started");
          RunExecutionContext executionContext{
              .workspace = context,
              .jobId = job.id(),
              .target = target,
          };
          RunResult result = typeValue->run(executionContext, configuration);
          result.jobId = job.id();
          if (!context.jobs().isTerminal(job.id()) && !result.output.empty()) {
            job.appendOutput(result.output);
          }
          resultPromise->set_value(result);
          return JobResult{
              .success = result.exitCode == 0,
              .data = toJson(result),
          };
        } catch (const std::exception &error) {
          RunResult result{
              .exitCode = -1,
              .output = std::string(error.what()) + "\n",
              .jobId = job.id(),
          };
          resultPromise->set_value(result);
          return JobResult{
              .success = false,
              .message = error.what(),
              .data = toJson(result),
          };
        } catch (...) {
          RunResult result{
              .exitCode = -1,
              .output = "Run failed\n",
              .jobId = job.id(),
          };
          resultPromise->set_value(result);
          return JobResult{
              .success = false,
              .message = "Run failed",
              .data = toJson(result),
          };
        }
      });
  RunResult result = resultFuture.get();
  handle.wait();
  return result;
}

std::string RunConfigurationComponent::id() const { return componentId; }

void RunConfigurationComponent::onAttach(WorkspaceContext &context) {
  service_ = &context.runConfigurations();
  if (!restoredConfigurations_.empty()) {
    service_->setConfigurations(restoredConfigurations_);
  }
}

void RunConfigurationComponent::restoreState(const Json &state) {
  restoredConfigurations_.clear();
  if (!state.isObject() || !state.contains("configurations") ||
      !state["configurations"].isArray()) {
    return;
  }
  for (const Json &item : state["configurations"].asArray()) {
    if (auto configuration = configurationFromJson(item)) {
      configuration->temporary = false;
      restoredConfigurations_.push_back(std::move(*configuration));
    }
  }
  if (service_ != nullptr) {
    service_->setConfigurations(restoredConfigurations_);
  }
}

Json RunConfigurationComponent::saveState() const {
  Json::Array configurations;
  if (service_ != nullptr) {
    for (const RunConfiguration &configuration :
         service_->configurations(false)) {
      configurations.push_back(toJson(configuration));
    }
  }
  return Json::object({
      {"schemaVersion", Json(static_cast<std::int64_t>(1))},
      {"configurations", Json::array(std::move(configurations))},
  });
}

void RunConfigurationComponent::onDetach() { service_ = nullptr; }

void registerDefaultRunConfigurationProviders(
    RunConfigurationService &service) {
  service.addType(std::make_unique<CustomCommandRunConfigurationType>());
  service.addType(std::make_unique<CppSingleFileRunConfigurationType>());
  service.addType(std::make_unique<PythonScriptRunConfigurationType>());
  service.addProducer(std::make_unique<SingleFileRunConfigurationProducer>());
}

Json toJson(const ConfigurationField &field) {
  return Json::object({
      {"id", Json(field.id)},
      {"title", Json(field.title)},
      {"type", Json(field.type)},
      {"defaultValue", field.defaultValue},
      {"required", Json(field.required)},
  });
}

Json toJson(const RunConfiguration &configuration) {
  return Json::object({
      {"id", Json(configuration.id)},
      {"name", Json(configuration.name)},
      {"typeId", Json(configuration.typeId)},
      {"targetId", Json(configuration.targetId)},
      {"data", configuration.data},
      {"temporary", Json(configuration.temporary)},
  });
}

Json toJson(const RunResult &result) {
  Json::Array diagnostics;
  for (const Diagnostic &diagnostic : result.diagnostics) {
    diagnostics.push_back(diagnosticToJson(diagnostic));
  }
  return Json::object({
      {"exitCode", Json(static_cast<std::int64_t>(result.exitCode))},
      {"output", Json(result.output)},
      {"diagnostics", Json::array(std::move(diagnostics))},
      {"jobId", Json(static_cast<std::int64_t>(result.jobId))},
  });
}

} // namespace vanta
