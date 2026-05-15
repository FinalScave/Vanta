#include "vanta/plugin/core_plugin.h"

#include <algorithm>
#include <set>
#include <sstream>

#include "vanta/builtin/clice/clice_integration.h"
#include "vanta/builtin/cmake/cmake_build_provider.h"
#include "vanta/builtin/cmake/cmake_project_model.h"
#include "vanta/builtin/cpp/cpp_index.h"
#include "vanta/language/language_service.h"
#include "vanta/project/project_manager.h"

namespace vanta {
namespace {

Json diagnosticToJson(const Diagnostic& diagnostic) {
    return Json::object({
        {"file", Json(diagnostic.location.file.toUri().string())},
        {"line", Json(static_cast<std::int64_t>(diagnostic.location.line))},
        {"column", Json(static_cast<std::int64_t>(diagnostic.location.column))},
        {"severity", Json(toString(diagnostic.severity))},
        {"source", Json(diagnostic.source)},
        {"message", Json(diagnostic.message)},
    });
}

Json buildEnvironmentToJson(const BuildEnvironment& environment) {
    return Json::object({
        {"providerId", Json(environment.providerId)},
        {"detected", Json(environment.detected)},
        {"buildDirectory", Json(environment.buildDirectory.string())},
        {"metadata", environment.metadata},
    });
}

Json buildResultToJson(const BuildResult& result) {
    Json::Array diagnostics;
    for (const Diagnostic& diagnostic : result.diagnostics) {
        diagnostics.push_back(diagnosticToJson(diagnostic));
    }
    return Json::object({
        {"exitCode", Json(static_cast<std::int64_t>(result.exitCode))},
        {"output", Json(result.output)},
        {"diagnostics", Json::array(std::move(diagnostics))},
        {"events", toJson(result.events)},
    });
}

TextPosition positionFromJson(const Json& input) {
    TextPosition position;
    if (input.contains("line") && input["line"].isInt()) {
        position.line = static_cast<int>(input["line"].asInt());
    }
    if (input.contains("character") && input["character"].isInt()) {
        position.character = static_cast<int>(input["character"].asInt());
    }
    return position;
}

BuildTask buildTaskFromJson(const Json& input, BuildTaskKind kind) {
    BuildTask task;
    task.kind = kind;
    task.target = input.stringValue("target").value_or("");
    if (auto buildDirectory = input.stringValue("buildDirectory")) {
        task.buildDirectory = *buildDirectory;
    }
    return task;
}

std::string normalizePathString(const std::filesystem::path& path) {
    std::error_code error;
    const auto normalized = std::filesystem::weakly_canonical(path, error);
    return (error ? path : normalized).string();
}

std::filesystem::path defaultBuildDirectory(const WorkspaceInfo& workspace) {
    return workspace.rootPath / "build";
}

std::filesystem::path compileCommandsPath(const std::filesystem::path& workspaceRoot) {
    const auto rootDatabase = workspaceRoot / "compile_commands.json";
    if (std::filesystem::exists(rootDatabase)) {
        return rootDatabase;
    }
    const auto buildDatabase = workspaceRoot / "build" / "compile_commands.json";
    if (std::filesystem::exists(buildDatabase)) {
        return buildDatabase;
    }
    return {};
}

std::filesystem::path inferBuildDirectory(const WorkspaceInfo& workspace, const std::filesystem::path& databasePath) {
    if (!databasePath.empty()) {
        return databasePath.parent_path();
    }
    return defaultBuildDirectory(workspace);
}

void addUniqueString(std::vector<std::string>& values, std::set<std::string>& seen, std::string value) {
    if (value.empty() || !seen.insert(value).second) {
        return;
    }
    values.push_back(std::move(value));
}

void addUniqueFile(std::vector<VirtualFile>& values, std::set<Uri>& seen, VirtualFile file) {
    if (!file.valid() || !seen.insert(file.toUri()).second) {
        return;
    }
    values.push_back(std::move(file));
}

CMakeProjectGraph buildCMakeGraph(const Workspace& workspace, const CppCompilationDatabase& database) {
    CMakeProjectGraph graph;
    CMakeTarget target;
    target.name = workspace.info().name.empty() ? "workspace" : workspace.info().name;
    target.kind = "compile_commands";

    std::set<Uri> sourceUris;
    std::set<Uri> targetSourceUris;
    std::set<Uri> includeUris;
    std::set<Uri> targetIncludeUris;
    std::set<std::string> defineValues;
    std::set<std::string> targetDefineValues;
    std::set<std::string> argumentValues;

    for (const CppTranslationUnit& unit : database.translationUnits) {
        addUniqueFile(graph.sourceFiles, sourceUris, unit.sourceFile);
        addUniqueFile(target.sourceFiles, targetSourceUris, unit.sourceFile);

        for (const VirtualFile& includeDirectory : unit.includeDirectories) {
            addUniqueFile(graph.includeDirectories, includeUris, includeDirectory);
            addUniqueFile(target.includeDirectories, targetIncludeUris, includeDirectory);
        }
        for (const std::string& define : unit.defines) {
            addUniqueString(graph.defines, defineValues, define);
            addUniqueString(target.defines, targetDefineValues, define);
        }
        for (const std::string& argument : unit.compileArguments) {
            addUniqueString(graph.compileArguments, argumentValues, argument);
            target.compileArguments.push_back(argument);
        }
    }

    if (!target.sourceFiles.empty() || !target.includeDirectories.empty() || !target.defines.empty()) {
        graph.targets.push_back(std::move(target));
    }
    return graph;
}

Json attachmentSummary(const CppCompilationDatabase& database) {
    return Json::object({
        {"file", Json(database.file.toUri().string())},
        {"translationUnits", Json(static_cast<std::int64_t>(database.translationUnits.size()))},
    });
}

Json attachmentSummary(const CMakeProjectModel& model) {
    return Json::object({
        {"cmakeListsFile", Json(model.cmakeListsFile.toUri().string())},
        {"compileCommandsFile", Json(model.compileCommandsFile.toUri().string())},
        {"buildDirectory", Json(model.buildDirectory.string())},
        {"targets", Json(static_cast<std::int64_t>(model.graph.targets.size()))},
        {"sourceFiles", Json(static_cast<std::int64_t>(model.graph.sourceFiles.size()))},
    });
}

class CMakeProjectModelProvider final : public ProjectModelProvider {
public:
    std::string id() const override {
        return "vanta.cmake.projectProvider";
    }

    void contribute(WorkspaceContext& context, ProjectModelBuilder& builder) const override {
        Workspace& workspace = context.workspace();
        const auto cmakeListsPath = workspace.info().rootPath / "CMakeLists.txt";
        const bool hasCMakeLists = std::filesystem::exists(cmakeListsPath);
        const auto databasePath = compileCommandsPath(workspace.info().rootPath);
        const bool hasCompileCommands = !databasePath.empty();
        if (!hasCMakeLists && !hasCompileCommands) {
            return;
        }

        CMakeProjectModel cmake;
        cmake.detected = true;
        cmake.buildDirectory = inferBuildDirectory(workspace.info(), databasePath);
        if (hasCMakeLists) {
            cmake.cmakeListsFile = workspace.file(cmakeListsPath);
            ProjectFacet facet{
                .id = "cmake",
                .type = "cmake",
                .title = "CMake",
                .metadata = Json::object(),
            };
            builder.addFacet(facet);
            builder.addFacetToPrimaryModule(std::move(facet));
        }

        if (hasCompileCommands) {
            CppCompilationDatabase database = loadCppCompilationDatabase(workspace, databasePath);
            cmake.compileCommandsFile = database.file;
            cmake.graph = buildCMakeGraph(workspace, database);
            ProjectFacet facet{
                .id = "cpp",
                .type = "cpp",
                .title = "C++",
                .metadata = Json::object(),
            };
            builder.addFacet(facet);
            builder.addFacetToPrimaryModule(std::move(facet));
            builder.setAttachment({
                .id = CppCompilationDatabase::attachmentId,
                .kind = CppCompilationDatabase::attachmentKind,
                .title = "C++ Compilation Database",
                .summary = attachmentSummary(database),
            }, std::move(database));
        }

        builder.setAttachment({
            .id = CMakeProjectModel::attachmentId,
            .kind = CMakeProjectModel::attachmentKind,
            .title = "CMake Project",
            .summary = attachmentSummary(cmake),
        }, std::move(cmake));
    }
};

class LanguagesCoreExtension final : public CoreExtension {
public:
    void activate(ExtensionContext& context) override {
        registerDefaultLanguages(context.languages());
        context.logger().info("Activated languages core plugin");
    }
};

class CMakeCoreExtension final : public CoreExtension {
public:
    void activate(ExtensionContext& context) override {
        build_ = &context.build();
        workspaceContext_ = &context.workspaceContext();
        workspaceRoot_ = context.workspace().rootPath;

        context.build().addProvider(std::make_unique<CMakeBuildProvider>());
        context.projectModels().addProvider(std::make_unique<CMakeProjectModelProvider>());

        context.commands().add("cmake.detect", [this](const Json&) {
            return buildEnvironmentToJson(build_->detect(workspaceRoot_));
        });

        context.commands().add("cmake.build", [this](const Json& input) {
            return buildResultToJson(build_->run(*workspaceContext_, workspaceRoot_, buildTaskFromJson(input, BuildTaskKind::Build)));
        });

        context.commands().add("cmake.test", [this](const Json& input) {
            return buildResultToJson(build_->run(*workspaceContext_, workspaceRoot_, buildTaskFromJson(input, BuildTaskKind::Test)));
        });

        context.agentTools().addTool({
            .id = "cmake.findOwningTarget",
            .description = "Find compile command information for a source file in the current CMake workspace.",
            .inputSchema = Json::object({
                {"type", Json("object")},
                {"required", Json::array({Json("file")})},
            }),
            .handler = [this](const Json& input) {
                const std::string file = input.stringValue("file").value_or("");
                const std::string targetPath = normalizePathString(file);
                const BuildEnvironment environment = build_->detect(workspaceRoot_);
                const std::filesystem::path databasePath = environment.metadata.stringValue("compileCommandsPath").value_or("");
                for (const CppCompileCommand& command : loadCppCompileCommands(databasePath)) {
                    if (normalizePathString(command.file) == targetPath) {
                        return Json::object({
                            {"found", Json(true)},
                            {"file", Json(command.file.string())},
                            {"directory", Json(command.directory.string())},
                            {"command", Json(command.command)},
                        });
                    }
                }
                return Json::object({
                    {"found", Json(false)},
                    {"file", Json(file)},
                });
            },
        });

        context.logger().info("Activated CMake core plugin");
    }

    void deactivate() override {
        build_ = nullptr;
        workspaceContext_ = nullptr;
        workspaceRoot_.clear();
    }

private:
    BuildService* build_ = nullptr;
    WorkspaceContext* workspaceContext_ = nullptr;
    std::filesystem::path workspaceRoot_;
};

class GitCoreExtension final : public CoreExtension {
public:
    void activate(ExtensionContext& context) override {
        git_ = &context.git();

        context.commands().add("git.diff", [this](const Json&) {
            const GitDiff diff = git_->diff();
            return Json::object({
                {"exitCode", Json(static_cast<std::int64_t>(diff.exitCode))},
                {"text", Json(diff.text)},
            });
        });

        context.agentTools().addTool({
            .id = "git.diff",
            .description = "Return the current workspace Git diff.",
            .inputSchema = Json::object({{"type", Json("object")}}),
            .handler = [this](const Json&) {
                const GitDiff diff = git_->diff();
                return Json::object({
                    {"exitCode", Json(static_cast<std::int64_t>(diff.exitCode))},
                    {"text", Json(diff.text)},
                });
            },
        });

        context.logger().info("Activated Git core plugin");
    }

    void deactivate() override {
        git_ = nullptr;
    }

private:
    GitReader* git_ = nullptr;
};

class CliceCoreExtension final : public CoreExtension {
public:
    explicit CliceCoreExtension(CorePluginDependencies dependencies) : dependencies_(std::move(dependencies)) {}

    void activate(ExtensionContext& context) override {
        languages_ = &context.languages();
        canExecuteProcess_ = context.permissions().contains(Permission::ProcessExecute);

        context.commands().add("clice.start", [this](const Json&) {
            if (!canExecuteProcess_) {
                return Json::object({
                    {"ok", Json(false)},
                    {"running", Json(false)},
                    {"error", Json("Missing process.execute permission")},
                });
            }
            LanguageService* service = languages_->serviceForLanguage("cpp");
            std::string error;
            const bool ok = service != nullptr && service->start(&error);
            return Json::object({
                {"ok", Json(ok)},
                {"running", Json(service != nullptr && service->running())},
                {"error", Json(service == nullptr ? "C++ language service is not registered" : error)},
            });
        });

        context.commands().add("clice.hover", [this](const Json& input) {
            const std::string file = input.stringValue("file").value_or("");
            return callDocumentPosition("hover", VirtualFile(Uri::parse(file), nullptr), positionFromJson(input));
        });

        context.commands().add("clice.completion", [this](const Json& input) {
            const std::string file = input.stringValue("file").value_or("");
            return callDocumentPosition("completion", VirtualFile(Uri::parse(file), nullptr), positionFromJson(input));
        });

        context.commands().add("clice.semanticTokens", [this](const Json& input) {
            const std::string file = input.stringValue("file").value_or("");
            const VirtualFile virtualFile(Uri::parse(file), nullptr);
            LanguageService* service = languages_->serviceForDocument(virtualFile);
            if (service == nullptr) {
                return languageErrorToJson("No language service is registered for document");
            }
            return languageResultToJson(service->semanticTokensFull({.file = virtualFile, .languageId = "cpp"}));
        });

        context.agentTools().addTool({
            .id = "clice.findSymbol",
            .description = "Ask clice for the definition location near a source position.",
            .inputSchema = Json::object({
                {"type", Json("object")},
                {"required", Json::array({Json("file"), Json("line"), Json("character")})},
            }),
            .handler = [this](const Json& input) {
                const std::string file = input.stringValue("file").value_or("");
                return callDocumentPosition("definition", VirtualFile(Uri::parse(file), nullptr), positionFromJson(input));
            },
        });

        clice_.configure(dependencies_.clicePath, dependencies_.workspaceRoot);
        languageService_ = clice_.createLanguageService();
        Language language;
        for (Language candidate : defaultLanguages()) {
            if (candidate.id == "cpp") {
                language = std::move(candidate);
                break;
            }
        }
        if (language.id.empty()) {
            language.id = "cpp";
            language.association.extensions = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"};
        }
        language.service = languageService_.get();
        language.priority = 100;
        context.languages().addLanguage(std::move(language));

        context.logger().info("Activated clice core plugin");
    }

    void deactivate() override {
        languages_ = nullptr;
    }

private:
    Json callDocumentPosition(const std::string& operation, const VirtualFile& file, TextPosition position) const {
        LanguageService* service = languages_->serviceForDocument(file);
        if (service == nullptr) {
            return languageErrorToJson("No language service is registered for document");
        }

        TextDocumentPosition request;
        request.document.file = file;
        request.document.languageId = "cpp";
        request.position = position;

        if (operation == "completion") {
            return languageResultToJson(service->completion(request));
        }
        if (operation == "hover") {
            return languageResultToJson(service->hover(request));
        }
        return languageResultToJson(service->definition(request));
    }

    CorePluginDependencies dependencies_;
    CliceIntegration clice_;
    std::unique_ptr<LanguageService> languageService_;
    LanguageRegistry* languages_ = nullptr;
    bool canExecuteProcess_ = false;
};

}

void CoreExtension::deactivate() {}

void CorePluginRegistry::add(std::string entry, CoreExtensionFactory factory) {
    factories_[std::move(entry)] = std::move(factory);
}

std::unique_ptr<CoreExtension> CorePluginRegistry::create(const std::string& entry) const {
    auto it = factories_.find(entry);
    if (it == factories_.end()) {
        return nullptr;
    }
    return it->second();
}

std::vector<std::string> CorePluginRegistry::entries() const {
    std::vector<std::string> result;
    for (const auto& [entry, factory] : factories_) {
        (void)factory;
        result.push_back(entry);
    }
    return result;
}

CorePluginRegistry createDefaultCorePluginRegistry(CorePluginDependencies dependencies) {
    CorePluginRegistry registry;
    registry.add("builtin:languages", [] {
        return std::make_unique<LanguagesCoreExtension>();
    });
    registry.add("builtin:cmake", [] {
        return std::make_unique<CMakeCoreExtension>();
    });
    registry.add("builtin:git", [] {
        return std::make_unique<GitCoreExtension>();
    });
    registry.add("builtin:clice", [dependencies] {
        return std::make_unique<CliceCoreExtension>(dependencies);
    });
    return registry;
}

}
