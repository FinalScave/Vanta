#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/language/language_service.h"
#include "vanta/workspace/change_set_service.h"

namespace vanta {

class WorkspaceContext;

enum class RefactoringKind {
    RenameSymbol,
    ChangeSignature,
    MoveSymbol,
    ExtractFunction,
    SafeDelete,
    OrganizeIncludes,
};

struct FunctionParameterInfo {
    std::string name;
    std::string type;
    std::string default_value;
};

struct RenameSymbolParams {
    std::string new_name;
};

struct ChangeSignatureParams {
    std::string symbol_id;
    std::vector<FunctionParameterInfo> parameters;
    std::string return_type;
};

struct MoveSymbolParams {
    std::string symbol_id;
    VirtualFile target_file;
    std::string target_container;
};

struct ExtractFunctionParams {
    std::string function_name;
    std::vector<FunctionParameterInfo> parameters;
    std::string return_type;
};

struct SafeDeleteParams {
    std::string symbol_id;
    bool delete_usages = false;
};

using RefactoringParams = std::variant<
    std::monostate,
    RenameSymbolParams,
    ChangeSignatureParams,
    MoveSymbolParams,
    ExtractFunctionParams,
    SafeDeleteParams>;

struct RefactoringRequest {
    RefactoringKind kind = RefactoringKind::RenameSymbol;
    TextDocumentIdentifier document;
    TextPosition position;
    TextRange range;
    std::string provider_id;
    std::string title;
    RefactoringParams params;
};

struct RefactoringPrepareResult {
    bool ok = false;
    std::string error;
    std::string title;
    std::vector<std::string> warnings;
    std::vector<CodeSymbol> affected_symbols;
};

struct RefactoringPlan {
    bool ok = false;
    std::string error;
    std::string title;
    WorkspaceEdit edit;
    std::vector<std::string> warnings;
    std::vector<CodeSymbol> affected_symbols;
};

class RefactoringProvider {
public:
    virtual ~RefactoringProvider() = default;

    virtual std::string Id() const = 0;
    virtual bool Supports(RefactoringKind kind) const;
    virtual RefactoringPrepareResult Prepare(WorkspaceContext& context, const RefactoringRequest& request) const;
    virtual RefactoringPlan Plan(WorkspaceContext& context, const RefactoringRequest& request) const;
};

class RefactoringService {
public:
    static constexpr const char* kServiceId = "vanta.refactorings";

    RegistrationHandle RegisterProvider(std::unique_ptr<RefactoringProvider> provider);
    void RemoveProvider(const std::string& provider_id);
    std::vector<std::string> ProviderIds() const;

    RefactoringPrepareResult Prepare(WorkspaceContext& context, const RefactoringRequest& request) const;
    RefactoringPlan Plan(WorkspaceContext& context, const RefactoringRequest& request) const;
    std::optional<ChangeSet> CreateChangeSet(
        WorkspaceContext& context,
        const RefactoringPlan& plan,
        std::string source = "refactoring") const;

private:
    RefactoringProvider* SelectProviderLocked(const RefactoringRequest& request) const;

    std::map<std::string, std::unique_ptr<RefactoringProvider>> providers_;
    mutable std::mutex mutex_;
};

std::string ToString(RefactoringKind kind);

}
