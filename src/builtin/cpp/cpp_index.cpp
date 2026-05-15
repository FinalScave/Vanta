#include "vanta/builtin/cpp/cpp_index.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace vanta {
namespace {

std::vector<std::string> splitCommandLine(const std::string& command) {
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

std::filesystem::path resolveCompilePath(const CppCompileCommand& command, const std::string& path) {
    const std::filesystem::path parsed(path);
    if (parsed.is_absolute()) {
        return parsed;
    }
    return command.directory / parsed;
}

void addUniqueFile(std::vector<VirtualFile>& values, std::set<Uri>& seen, VirtualFile file) {
    if (!file.valid() || !seen.insert(file.toUri()).second) {
        return;
    }
    values.push_back(std::move(file));
}

void addUniqueString(std::vector<std::string>& values, std::set<std::string>& seen, std::string value) {
    if (value.empty() || !seen.insert(value).second) {
        return;
    }
    values.push_back(std::move(value));
}

CppTranslationUnit buildTranslationUnit(const Workspace& workspace, const CppCompileCommand& command) {
    CppTranslationUnit unit;
    unit.command = command;
    unit.sourceFile = workspace.file(command.file);

    std::set<Uri> includeUris;
    std::set<std::string> defines;
    const std::vector<std::string> arguments = cppCompileArguments(command);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        unit.compileArguments.push_back(argument);

        std::string includePath;
        if (argument == "-I" || argument == "-isystem" || argument == "/I") {
            if (i + 1 < arguments.size()) {
                includePath = arguments[++i];
            }
        } else if (argument.rfind("-I", 0) == 0 && argument.size() > 2) {
            includePath = argument.substr(2);
        } else if (argument.rfind("/I", 0) == 0 && argument.size() > 2) {
            includePath = argument.substr(2);
        }

        if (!includePath.empty()) {
            addUniqueFile(unit.includeDirectories, includeUris, workspace.file(resolveCompilePath(command, includePath)));
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
        addUniqueString(unit.defines, defines, define);
    }

    return unit;
}

}

bool CppCompilationDatabase::available() const {
    return file.valid() && !commands.empty();
}

void CppSemanticIndex::addProvider(std::unique_ptr<CppSemanticIndexProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return;
    }
    providers_[provider->id()] = std::move(provider);
}

void CppSemanticIndex::removeProvider(const std::string& providerId) {
    providers_.erase(providerId);
}

std::vector<std::string> CppSemanticIndex::providerIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

CppSemanticIndexSnapshot CppSemanticIndex::snapshot() const {
    for (const auto& [id, provider] : providers_) {
        (void)id;
        CppSemanticIndexSnapshot value = provider->snapshot();
        if (value.ready) {
            return value;
        }
    }
    return {};
}

std::vector<std::string> cppCompileArguments(const CppCompileCommand& command) {
    if (!command.arguments.empty()) {
        return command.arguments;
    }
    return splitCommandLine(command.command);
}

std::vector<CppCompileCommand> loadCppCompileCommands(const std::filesystem::path& path) {
    std::vector<CppCompileCommand> commands;
    std::ifstream input(path);
    if (!input) {
        return commands;
    }
    std::ostringstream stream;
    stream << input.rdbuf();

    Json root = Json::parse(stream.str());
    if (!root.isArray()) {
        return commands;
    }

    for (const Json& item : root.asArray()) {
        if (!item.isObject()) {
            continue;
        }
        CppCompileCommand command;
        command.directory = item.stringValue("directory").value_or("");
        command.file = item.stringValue("file").value_or("");
        command.command = item.stringValue("command").value_or("");
        if (item.contains("arguments") && item["arguments"].isArray()) {
            for (const Json& argument : item["arguments"].asArray()) {
                if (argument.isString()) {
                    command.arguments.push_back(argument.asString());
                }
            }
        }
        commands.push_back(std::move(command));
    }

    return commands;
}

CppCompilationDatabase loadCppCompilationDatabase(const Workspace& workspace, const std::filesystem::path& path) {
    CppCompilationDatabase database;
    database.file = workspace.file(path);
    database.commands = loadCppCompileCommands(path);
    for (const CppCompileCommand& command : database.commands) {
        database.translationUnits.push_back(buildTranslationUnit(workspace, command));
    }
    return database;
}

Json toJson(const CppCompileCommand& command) {
    Json::Array arguments;
    for (const std::string& argument : cppCompileArguments(command)) {
        arguments.push_back(Json(argument));
    }
    return Json::object({
        {"directory", Json(command.directory.string())},
        {"file", Json(command.file.string())},
        {"command", Json(command.command)},
        {"arguments", Json::array(std::move(arguments))},
    });
}

Json toJson(const CppCompilationDatabase& database) {
    Json::Array commands;
    for (const CppCompileCommand& command : database.commands) {
        commands.push_back(toJson(command));
    }
    return Json::object({
        {"file", Json(database.file.toUri().string())},
        {"available", Json(database.available())},
        {"commands", Json::array(std::move(commands))},
    });
}

}
