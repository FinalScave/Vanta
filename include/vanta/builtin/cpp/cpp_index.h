#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "vanta/workspace/workspace.h"
#include "vanta/platform/json.h"

namespace vanta {

struct CppCompileCommand {
    std::filesystem::path directory;
    std::filesystem::path file;
    std::string command;
    std::vector<std::string> arguments;
};

struct CppTranslationUnit {
    VirtualFile sourceFile;
    CppCompileCommand command;
    std::vector<VirtualFile> includeDirectories;
    std::vector<std::string> defines;
    std::vector<std::string> compileArguments;
};

struct CppCompilationDatabase {
    static constexpr const char* attachmentId = "vanta.cpp.compilationDatabase";
    static constexpr const char* attachmentKind = "cpp.compilationDatabase";

    VirtualFile file;
    std::vector<CppCompileCommand> commands;
    std::vector<CppTranslationUnit> translationUnits;

    bool available() const;
};

struct CppSemanticIndexSnapshot {
    bool ready = false;
    std::string providerId;
    CppCompilationDatabase compilationDatabase;
    Json metadata;
};

class CppSemanticIndexProvider {
public:
    virtual ~CppSemanticIndexProvider() = default;

    virtual std::string id() const = 0;
    virtual CppSemanticIndexSnapshot snapshot() const = 0;
};

class CppSemanticIndex {
public:
    void addProvider(std::unique_ptr<CppSemanticIndexProvider> provider);
    void removeProvider(const std::string& providerId);
    std::vector<std::string> providerIds() const;
    CppSemanticIndexSnapshot snapshot() const;

private:
    std::map<std::string, std::unique_ptr<CppSemanticIndexProvider>> providers_;
};

std::vector<std::string> cppCompileArguments(const CppCompileCommand& command);
std::vector<CppCompileCommand> loadCppCompileCommands(const std::filesystem::path& path);
CppCompilationDatabase loadCppCompilationDatabase(const Workspace& workspace, const std::filesystem::path& path);

Json toJson(const CppCompileCommand& command);
Json toJson(const CppCompilationDatabase& database);

}
