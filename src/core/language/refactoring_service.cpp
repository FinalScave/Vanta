#include "vanta/language/refactoring_service.h"

#include <utility>

#include "vanta/workspace/workspace_context.h"

namespace vanta {

bool RefactoringProvider::Supports(RefactoringKind) const {
    return false;
}

RefactoringPrepareResult RefactoringProvider::Prepare(WorkspaceContext&, const RefactoringRequest& request) const {
    return {
        .ok = false,
        .error = "Refactoring provider does not support request kind: " + ToString(request.kind),
    };
}

RefactoringPlan RefactoringProvider::Plan(WorkspaceContext&, const RefactoringRequest& request) const {
    return {
        .ok = false,
        .error = "Refactoring provider does not support request kind: " + ToString(request.kind),
    };
}

RegistrationHandle RefactoringService::RegisterProvider(std::unique_ptr<RefactoringProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string id = provider->Id();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_[id] = std::move(provider);
    }
    return RegistrationHandle([this, id] {
        RemoveProvider(id);
    });
}

void RefactoringService::RemoveProvider(const std::string& provider_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    providers_.erase(provider_id);
}

std::vector<std::string> RefactoringService::ProviderIds() const {
    std::vector<std::string> ids;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

RefactoringPrepareResult RefactoringService::Prepare(WorkspaceContext& context, const RefactoringRequest& request) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RefactoringProvider* provider = SelectProviderLocked(request);
    if (provider == nullptr) {
        return {
            .ok = false,
            .error = "No refactoring provider supports request kind: " + ToString(request.kind),
        };
    }
    return provider->Prepare(context, request);
}

RefactoringPlan RefactoringService::Plan(WorkspaceContext& context, const RefactoringRequest& request) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RefactoringProvider* provider = SelectProviderLocked(request);
    if (provider == nullptr) {
        return {
            .ok = false,
            .error = "No refactoring provider supports request kind: " + ToString(request.kind),
        };
    }
    return provider->Plan(context, request);
}

std::optional<ChangeSet> RefactoringService::CreateChangeSet(WorkspaceContext& context, const RefactoringPlan& plan, std::string source) const {
    if (!plan.ok) {
        return std::nullopt;
    }
    const std::string title = plan.title.empty() ? "Refactoring" : plan.title;
    return context.Changes().Create(std::move(source), title, plan.edit);
}

RefactoringProvider* RefactoringService::SelectProviderLocked(const RefactoringRequest& request) const {
    if (!request.provider_id.empty()) {
        auto it = providers_.find(request.provider_id);
        return it == providers_.end() ? nullptr : it->second.get();
    }
    for (const auto& [id, provider] : providers_) {
        (void)id;
        if (provider->Supports(request.kind)) {
            return provider.get();
        }
    }
    return nullptr;
}

std::string ToString(RefactoringKind kind) {
    switch (kind) {
    case RefactoringKind::RenameSymbol:
        return "renameSymbol";
    case RefactoringKind::ChangeSignature:
        return "changeSignature";
    case RefactoringKind::MoveSymbol:
        return "moveSymbol";
    case RefactoringKind::ExtractFunction:
        return "extractFunction";
    case RefactoringKind::SafeDelete:
        return "safeDelete";
    case RefactoringKind::OrganizeIncludes:
        return "organizeIncludes";
    }
    return "renameSymbol";
}

}
