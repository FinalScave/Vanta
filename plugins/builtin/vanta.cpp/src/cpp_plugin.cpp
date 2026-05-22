#include "core_plugin_factories.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cpp_index.h"
#include "vanta/execution/problem_matcher.h"
#include "vanta/execution/run_configuration.h"
#include "vanta/project/project.h"
#include "vanta/project/project_template.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta::builtin {
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

bool IsCppFile(const VirtualFile& file) {
    const std::string extension = NormalizedExtension(file);
    return extension == "cpp" || extension == "cxx" || extension == "cc" || extension == "c";
}

std::string DefaultStandard(const VirtualFile& file) {
    return NormalizedExtension(file) == "c" ? "c17" : "c++20";
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

class CppSingleFileRunConfigurationProvider final : public RunConfigurationProvider {
public:
    std::string Id() const override {
        return "cpp.singleFile";
    }

    std::string Title() const override {
        return "C++ Single File";
    }

    struct Data final : public RunConfigurationData {
        std::filesystem::path file;
        std::string standard = "c++20";
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
        data->standard = "c++20";
        if (focus_file.Valid()) {
            if (const auto local_path = focus_file.LocalPath()) {
                data->file = *local_path;
                data->standard = DefaultStandard(focus_file);
                data->working_directory = local_path->parent_path();
            }
        }

        RunConfiguration configuration;
        configuration.id = name_hint.empty() ? "cpp.singleFile" : "cpp." + SanitizeId(name_hint);
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
        if (!file.Valid() || !IsCppFile(file)) {
            return {};
        }
        const auto local_path = file.LocalPath();
        if (!local_path) {
            return {};
        }
        RunConfiguration configuration = Create(context, file, "Run " + file.DisplayName());
        configuration.id = "temp.cpp." + SanitizeId(file.ToUri().ToString());
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
        data->standard = value.StringValue("standard").value_or("c++20");
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
            {"standard", Value(data->standard)},
            {"arguments", StringsProjection(data->arguments)},
            {"workingDirectory", Value(data->working_directory.string())},
        });
    }

    std::vector<RunConfigurationField> Fields(WorkspaceContext& context, const RunConfiguration& configuration) const override {
        (void)context;
        (void)configuration;
        return {
            {.id = "file", .title = "File", .kind = "file", .default_value = Value(""), .required = true},
            {.id = "standard", .title = "Standard", .kind = "string", .default_value = Value("c++20")},
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
        if (field_id == "standard") {
            return Value(data->standard);
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
        if (field_id == "standard" && value.IsString()) {
            data->standard = value.AsString();
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
        if (!IsCppFile(file)) {
            return Missing("File is not a C or C++ source file");
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

        std::error_code error;
        const std::filesystem::path run_directory = context.workspace.CurrentWorkspace().Info().root_path / ".vanta" / "run";
        std::filesystem::create_directories(run_directory, error);
        if (error) {
            return {.exit_code = -1, .output = "Could not create run directory\n", .job_id = context.job_id};
        }

        std::filesystem::path executable_path = run_directory / SanitizeId(local_path->stem().string());
#ifdef _WIN32
        executable_path += ".exe";
#endif

        std::vector<std::string> compile_arguments;
        compile_arguments.push_back("-std=" + (data->standard.empty() ? DefaultStandard(file) : data->standard));
        compile_arguments.push_back(local_path->string());
        compile_arguments.push_back("-o");
        compile_arguments.push_back(executable_path.string());
        const ExecutionResult compile = context.workspace.Execution().Execute(context.workspace, {
            .executable = NormalizedExtension(file) == "c" ? "cc" : "c++",
            .arguments = compile_arguments,
            .working_directory = working_directory,
            .job_id = context.job_id,
        }, context.target);

        RunResult result;
        result.exit_code = compile.exit_code;
        result.output = compile.output;
        result.job_id = context.job_id;
        const ProblemMatcher matcher;
        const DiagnosticResolver resolver;
        result.diagnostics = resolver.Resolve(
            matcher.MatchCompilerOutput(result.output),
            context.workspace.CurrentWorkspace(),
            working_directory);
        if (compile.exit_code != 0) {
            return result;
        }

        const ExecutionResult run = context.workspace.Execution().Execute(context.workspace, {
            .executable = executable_path.string(),
            .arguments = data->arguments,
            .working_directory = working_directory,
            .job_id = context.job_id,
        }, context.target);
        result.exit_code = run.exit_code;
        result.output += run.output;
        return result;
    }
};

class CppCoreExtension final : public CoreExtension {
public:
    void Activate(ExtensionContext& context) override {
        WorkspaceContext& workspace = context.Context();
        context.Track(workspace.RunConfigurations().RegisterProvider(std::make_unique<CppSingleFileRunConfigurationProvider>()));
        context.Track(workspace.Indexes().RegisterProvider(CreateCppCompilationDatabaseIndexProvider()));
        context.Track(workspace.ProjectTemplates().RegisterCategory({.id = "cpp", .title = "C++", .sort_order = 10}));
        context.Track(workspace.ProjectTemplates().RegisterTemplate({
            .id = "cpp.console.cmake",
            .category_id = "cpp",
            .title = "C++ Console",
            .description = "A minimal CMake console project.",
            .files = {
                {.relative_path = "CMakeLists.txt", .contents = "cmake_minimum_required(VERSION 3.20)\nproject(VantaApp LANGUAGES CXX)\n\nset(CMAKE_CXX_STANDARD 20)\nadd_executable(vanta_app src/main.cpp)\n"},
                {.relative_path = "src/main.cpp", .contents = "#include <iostream>\n\nint main() {\n    std::cout << \"Hello from Vanta\" << std::endl;\n    return 0;\n}\n"},
            },
            .language_id = "cpp",
        }));
        context.Log().Info("Activated C++ core plugin");
    }
};

}

std::unique_ptr<CoreExtension> CreateCppCoreExtension() {
    return std::make_unique<CppCoreExtension>();
}

}
