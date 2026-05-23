#include "core_plugin_factories.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mornox/execution/run_configuration.h"
#include "mornox/platform/executable.h"
#include "mornox/project/project.h"
#include "mornox/project/project_template.h"
#include "mornox/workspace/workspace_context.h"

namespace mornox::builtin {
namespace {

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

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string NormalizedExtension(const VirtualFile& file) {
    std::string extension = Lowercase(file.Extension());
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(extension.begin());
    }
    return extension;
}

bool IsPythonFile(const VirtualFile& file) {
    return NormalizedExtension(file) == "py";
}

std::string SanitizeId(std::string value) {
    for (char& character : value) {
        const bool allowed = std::isalnum(static_cast<unsigned char>(character)) ||
            character == '-' || character == '_' || character == '.';
        if (!allowed) {
            character = '_';
        }
    }
    return value.empty() ? "file" : value;
}

ValidationResult Missing(const std::string& message) {
    return {
        .ok = false,
        .messages = {message},
    };
}

std::filesystem::path PythonExecutable() {
#if defined(_WIN32)
    const std::vector<std::string> candidates = {"python", "py"};
    return FindFirstExecutableOnPath(candidates).value_or(std::filesystem::path("python"));
#else
    const std::vector<std::string> candidates = {
        "/opt/local/bin/python3.11",
        "/opt/homebrew/bin/python3",
        "/usr/local/bin/python3",
        "python3",
        "python",
    };
    return FindFirstExecutableOnPath(candidates).value_or(std::filesystem::path("python3"));
#endif
}

class PythonScriptRunConfigurationProvider final : public RunConfigurationProvider {
public:
    std::string Id() const override {
        return "python.script";
    }

    std::string Title() const override {
        return "Python Script";
    }

    struct Data final : public RunConfigurationData {
        std::filesystem::path file;
        std::vector<std::string> arguments;
        std::filesystem::path working_directory;

        std::unique_ptr<RunConfigurationData> Clone() const override {
            return std::make_unique<Data>(*this);
        }
    };

    RunConfiguration Create(
        WorkspaceContext& context,
        const VirtualFile& focus_file,
        const std::string& name_hint) const override {
        auto data = std::make_unique<Data>();
        data->working_directory = context.CurrentWorkspace().Info().root_path;
        if (focus_file.Valid()) {
            if (const auto local_path = focus_file.LocalPath()) {
                data->file = *local_path;
                data->working_directory = local_path->parent_path();
            }
        }

        RunConfiguration configuration;
        configuration.id = name_hint.empty() ? "python.script" : "python." + SanitizeId(name_hint);
        configuration.name = name_hint.empty() ? Title() : name_hint;
        configuration.provider_id = Id();
        configuration.target_id = "local.default";
        configuration.data = std::move(data);
        return configuration;
    }

    std::vector<RunConfiguration> Discover(WorkspaceContext& context, const VirtualFile& focus_file) const override {
        VirtualFile file = focus_file;
        if (!file.Valid()) {
            if (const auto* single_file = context.RequireProject().Model().Attachment<SingleFileModel>(SingleFileModel::kAttachmentId)) {
                file = single_file->file;
            }
        }
        if (!file.Valid() || !IsPythonFile(file)) {
            return {};
        }
        const auto local_path = file.LocalPath();
        if (!local_path) {
            return {};
        }
        RunConfiguration configuration = Create(context, file, "Run " + file.DisplayName());
        configuration.id = "temp.python." + SanitizeId(file.ToUri().ToString());
        configuration.temporary = true;
        return {std::move(configuration)};
    }

    std::unique_ptr<RunConfigurationData> LoadData(const Value& value) const override {
        auto data = std::make_unique<Data>();
        if (!value.IsObject()) {
            return data;
        }
        if (auto file = value.StringValue("file")) {
            data->file = *file;
        }
        if (value.Contains("arguments")) {
            data->arguments = StringsFromValue(value["arguments"]);
        }
        if (auto working_directory = value.StringValue("workingDirectory")) {
            data->working_directory = *working_directory;
        }
        return data;
    }

    Value SaveData(const RunConfigurationData& data_value) const override {
        const auto* data = dynamic_cast<const Data*>(&data_value);
        if (data == nullptr) {
            return Value::ObjectValue();
        }
        return Value::ObjectValue({
            {"file", Value(data->file.string())},
            {"arguments", StringsProjection(data->arguments)},
            {"workingDirectory", Value(data->working_directory.string())},
        });
    }

    std::vector<RunConfigurationField> Fields(WorkspaceContext& context, const RunConfiguration& configuration) const override {
        (void)context;
        (void)configuration;
        return {
            {.id = "file", .title = "File", .kind = "file", .default_value = Value(""), .required = true},
            {.id = "arguments", .title = "Arguments", .kind = "stringArray", .default_value = Value::ArrayValue()},
            {.id = "workingDirectory", .title = "Working Directory", .kind = "path", .default_value = Value("")},
        };
    }

    Value GetFieldValue(const RunConfigurationData& data_value, std::string_view field_id) const override {
        const auto* data = dynamic_cast<const Data*>(&data_value);
        if (data == nullptr) {
            return nullptr;
        }
        if (field_id == "file") {
            return Value(data->file.string());
        }
        if (field_id == "arguments") {
            return StringsProjection(data->arguments);
        }
        if (field_id == "workingDirectory") {
            return Value(data->working_directory.string());
        }
        return nullptr;
    }

    bool SetFieldValue(RunConfigurationData& data_value, std::string_view field_id, const Value& value) const override {
        auto* data = dynamic_cast<Data*>(&data_value);
        if (data == nullptr) {
            return false;
        }
        if (field_id == "file" && value.IsString()) {
            data->file = value.AsString();
            return true;
        }
        if (field_id == "arguments" && value.IsArray()) {
            data->arguments = StringsFromValue(value);
            return true;
        }
        if (field_id == "workingDirectory" && value.IsString()) {
            data->working_directory = value.AsString();
            return true;
        }
        return false;
    }

    ValidationResult Validate(WorkspaceContext& context, const RunConfiguration& configuration) const override {
        const Data* data = dynamic_cast<const Data*>(configuration.data.get());
        if (data == nullptr) {
            return Missing("Run configuration data is invalid");
        }
        const VirtualFile file = context.CurrentWorkspace().File(data->file);
        if (!file.Valid() || !file.Exists()) {
            return Missing("File does not exist");
        }
        if (!IsPythonFile(file)) {
            return Missing("File is not a Python script");
        }
        return {};
    }

    RunResult Run(RunExecutionContext& context, const RunConfiguration& configuration) const override {
        const Data* data = dynamic_cast<const Data*>(configuration.data.get());
        if (data == nullptr) {
            return {.exit_code = -1, .output = "Run configuration data is invalid\n", .job_id = context.job_id};
        }
        const VirtualFile file = context.workspace.CurrentWorkspace().File(data->file);
        const auto local_path = file.LocalPath();
        if (!local_path) {
            return {.exit_code = -1, .output = "File is not backed by a local path\n", .job_id = context.job_id};
        }
        std::filesystem::path working_directory = data->working_directory;
        if (working_directory.empty()) {
            working_directory = local_path->parent_path();
        }

        std::vector<std::string> arguments;
        arguments.push_back(local_path->string());
        for (const std::string& argument : data->arguments) {
            arguments.push_back(argument);
        }
        const std::filesystem::path python = PythonExecutable();
        const ExecutionResult execution = context.workspace.Execution().Execute(context.workspace, {
            .executable = python.string(),
            .arguments = arguments,
            .working_directory = working_directory,
            .job_id = context.job_id,
        }, context.target);
        return {
            .exit_code = execution.exit_code,
            .output = execution.output,
            .job_id = context.job_id,
        };
    }
};

class PythonCoreExtension final : public CoreExtension {
public:
    void Activate(ExtensionContext& context) override {
        WorkspaceContext& workspace = context.Context();
        context.Track(workspace.RunConfigurations().RegisterProvider(std::make_unique<PythonScriptRunConfigurationProvider>()));
        context.Track(workspace.ProjectTemplates().RegisterCategory({.id = "python", .title = "Python", .sort_order = 20}));
        context.Track(workspace.ProjectTemplates().RegisterTemplate({
            .id = "python.script",
            .category_id = "python",
            .title = "Python Script",
            .description = "A single Python entry point.",
            .files = {
                {.relative_path = "main.py", .contents = "def main():\n    print(\"Hello from Mornox\")\n\nif __name__ == \"__main__\":\n    main()\n"},
            },
            .language_id = "python",
        }));
        context.Log().Info("Activated Python core plugin");
    }
};

}

std::unique_ptr<CoreExtension> CreatePythonCoreExtension() {
    return std::make_unique<PythonCoreExtension>();
}

}
