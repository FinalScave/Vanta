#include "cpp_index.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

#include "vanta/core/json_codec.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

std::vector<std::string> SplitCommandLine(const std::string& command) {
    std::vector<std::string> result;
    std::string current;
    bool quoted = false;
    char quote = '\0';

    for (char ch : command) {
        if ((ch == '"' || ch == '\'') && (!quoted || quote == ch)) {
            quoted = !quoted;
            quote = quoted ? ch : '\0';
            continue;
        }
        if (!quoted && std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                result.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }

    if (!current.empty()) {
        result.push_back(std::move(current));
    }
    return result;
}

std::filesystem::path ResolveCompilePath(const CppCompileCommand& command, const std::string& path) {
    const std::filesystem::path parsed(path);
    if (parsed.is_absolute()) {
        return parsed;
    }
    return command.directory / parsed;
}

void AddUniqueFile(std::vector<VirtualFile>& values, std::set<Uri>& seen, VirtualFile file) {
    if (!file.Valid() || !seen.insert(file.ToUri()).second) {
        return;
    }
    values.push_back(std::move(file));
}

void AddUniqueString(std::vector<std::string>& values, std::set<std::string>& seen, std::string value) {
    if (value.empty() || !seen.insert(value).second) {
        return;
    }
    values.push_back(std::move(value));
}

CppTranslationUnit BuildTranslationUnit(const Workspace& workspace, const CppCompileCommand& command) {
    CppTranslationUnit unit;
    unit.command = command;
    unit.source_file = workspace.File(command.file);

    std::set<Uri> include_uris;
    std::set<std::string> defines;
    const std::vector<std::string> arguments = CppCompileArguments(command);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        unit.compile_arguments.push_back(argument);

        std::string include_path;
        if (argument == "-I" || argument == "-isystem" || argument == "/I") {
            if (i + 1 < arguments.size()) {
                include_path = arguments[++i];
            }
        } else if (argument.rfind("-I", 0) == 0 && argument.size() > 2) {
            include_path = argument.substr(2);
        } else if (argument.rfind("/I", 0) == 0 && argument.size() > 2) {
            include_path = argument.substr(2);
        }

        if (!include_path.empty()) {
            AddUniqueFile(unit.include_directories, include_uris, workspace.File(ResolveCompilePath(command, include_path)));
        }

        std::string define;
        if (argument == "-D" || argument == "/D") {
            if (i + 1 < arguments.size()) {
                define = arguments[++i];
            }
        } else if (argument.rfind("-D", 0) == 0 && argument.size() > 2) {
            define = argument.substr(2);
        } else if (argument.rfind("/D", 0) == 0 && argument.size() > 2) {
            define = argument.substr(2);
        }
        AddUniqueString(unit.defines, defines, define);
    }

    return unit;
}

}

bool CppCompilationDatabase::Available() const {
    return file.Valid() && !commands.empty();
}

std::string CppCompilationDatabase::Id() const {
    return kAttachmentId;
}

std::string CppCompilationDatabase::Kind() const {
    return kAttachmentKind;
}

std::string CppCompilationDatabase::Title() const {
    return "C++ Compilation Database";
}

Value CppCompilationDatabase::Projection() const {
    return internal::CppCompilationDatabaseProjection(*this);
}

std::vector<std::string> CppCompileArguments(const CppCompileCommand& command) {
    if (!command.arguments.empty()) {
        return command.arguments;
    }
    return SplitCommandLine(command.command);
}

std::vector<CppCompileCommand> LoadCppCompileCommands(const std::filesystem::path& path) {
    std::vector<CppCompileCommand> commands;
    std::ifstream input(path);
    if (!input) {
        return commands;
    }
    std::ostringstream stream;
    stream << input.rdbuf();

    Result<Value> parsed = ValueFromJsonText(stream.str());
    if (!parsed) {
        return commands;
    }
    const Value& root = parsed.Value();
    if (!root.IsArray()) {
        return commands;
    }

    for (const Value& item : root.AsArray()) {
        if (!item.IsObject()) {
            continue;
        }
        CppCompileCommand command;
        command.directory = item.StringValue("directory").value_or("");
        command.file = item.StringValue("file").value_or("");
        command.command = item.StringValue("command").value_or("");
        if (item.Contains("arguments") && item["arguments"].IsArray()) {
            for (const Value& argument : item["arguments"].AsArray()) {
                if (argument.IsString()) {
                    command.arguments.push_back(argument.AsString());
                }
            }
        }
        commands.push_back(std::move(command));
    }

    return commands;
}

CppCompilationDatabase LoadCppCompilationDatabase(const Workspace& workspace, const std::filesystem::path& path) {
    CppCompilationDatabase database;
    database.file = workspace.File(path);
    database.commands = LoadCppCompileCommands(path);
    for (const CppCompileCommand& command : database.commands) {
        database.translation_units.push_back(BuildTranslationUnit(workspace, command));
    }
    return database;
}

std::unique_ptr<IndexProvider> CreateCppCompilationDatabaseIndexProvider() {
    class Provider final : public IndexProvider {
    public:
        std::string Id() const override {
            return "vanta.index.cpp.compilationDatabase";
        }

        std::string Kind() const override {
            return "cpp.compilationDatabase";
        }

        IndexSnapshot Refresh(WorkspaceContext& context, JobContext& job) override {
            job.Report(0.1, "Refreshing C++ compilation database");
            database_ = {};
            const std::filesystem::path database_path = FindDatabase(context.CurrentWorkspace().Info().root_path);
            if (database_path.empty()) {
                return {
                    .id = Id(),
                    .kind = Kind(),
                    .status = IndexStatus::Stale,
                    .message = "compile_commands.json was not found",
                };
            }

            database_ = LoadCppCompilationDatabase(context.CurrentWorkspace(), database_path);
            return {
                .id = Id(),
                .kind = Kind(),
                .status = database_.Available() ? IndexStatus::Ready : IndexStatus::Failed,
                .item_count = database_.translation_units.size(),
                .message = database_.Available() ? "C++ compilation database ready" : "C++ compilation database is empty",
            };
        }

        bool Supports(IndexQueryKind query_kind) const override {
            return query_kind == IndexQueryKind::Includes;
        }

        IndexQueryResult Query(WorkspaceContext&, const IndexQuery& query) const override {
            if (query.kind == IndexQueryKind::Includes) {
                return QueryIncludes(query);
            }
            return {
                .ok = false,
                .error = "C++ compilation database index does not support query kind: " + ToString(query.kind),
            };
        }

    private:
        static std::filesystem::path FindDatabase(const std::filesystem::path& workspace_root) {
            const std::filesystem::path root_database = workspace_root / "compile_commands.json";
            if (std::filesystem::exists(root_database)) {
                return root_database;
            }
            const std::filesystem::path build_database = workspace_root / "build" / "compile_commands.json";
            if (std::filesystem::exists(build_database)) {
                return build_database;
            }
            return {};
        }

        IndexQueryResult QueryIncludes(const IndexQuery& query) const {
            IndexQueryResult result;
            result.ok = true;
            std::set<Uri> seen;
            const std::string needle = query.query;
            for (const CppTranslationUnit& unit : database_.translation_units) {
                if (!needle.empty() && unit.source_file.ToUri().ToString().find(needle) == std::string::npos &&
                    unit.source_file.DisplayName().find(needle) == std::string::npos) {
                    continue;
                }
                for (const VirtualFile& include_directory : unit.include_directories) {
                    if (!seen.insert(include_directory.ToUri()).second) {
                        continue;
                    }
                    result.hits.push_back({
                        .file = include_directory,
                        .title = include_directory.DisplayName(),
                        .preview = include_directory.ToUri().ToString(),
                        .provider_id = Id(),
                        .score = 100,
                    });
                    if (result.hits.size() >= query.limit) {
                        return result;
                    }
                }
            }
            return result;
        }

        CppCompilationDatabase database_;
    };

    return std::make_unique<Provider>();
}

namespace internal {

namespace {

Value StringsProjection(const std::vector<std::string>& values) {
    Value::Array result;
    for (const std::string& value : values) {
        result.push_back(Value(value));
    }
    return Value::ArrayValue(std::move(result));
}

Value VirtualFilesProjection(const std::vector<VirtualFile>& files) {
    Value::Array values;
    for (const VirtualFile& file : files) {
        values.push_back(Value(file.ToUri().ToString()));
    }
    return Value::ArrayValue(std::move(values));
}

Value CppCompileCommandProjection(const CppCompileCommand& command) {
    Value::Array arguments;
    for (const std::string& argument : CppCompileArguments(command)) {
        arguments.push_back(Value(argument));
    }
    return Value::ObjectValue({
        {"directory", Value(command.directory.string())},
        {"file", Value(command.file.string())},
        {"command", Value(command.command)},
        {"arguments", Value::ArrayValue(std::move(arguments))},
    });
}

Value CppTranslationUnitProjection(const CppTranslationUnit& unit) {
    return Value::ObjectValue({
        {"sourceFile", Value(unit.source_file.ToUri().ToString())},
        {"includeDirectories", VirtualFilesProjection(unit.include_directories)},
        {"defines", StringsProjection(unit.defines)},
        {"compileArguments", StringsProjection(unit.compile_arguments)},
    });
}

}

Value CppCompilationDatabaseProjection(const CppCompilationDatabase& database) {
    Value::Array commands;
    for (const CppCompileCommand& command : database.commands) {
        commands.push_back(CppCompileCommandProjection(command));
    }
    Value::Array translation_units;
    for (const CppTranslationUnit& unit : database.translation_units) {
        translation_units.push_back(CppTranslationUnitProjection(unit));
    }
    return Value::ObjectValue({
        {"file", Value(database.file.ToUri().ToString())},
        {"available", Value(database.Available())},
        {"commands", Value::ArrayValue(std::move(commands))},
        {"translationUnits", Value::ArrayValue(std::move(translation_units))},
    });
}

}

}
