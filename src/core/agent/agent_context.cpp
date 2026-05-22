#include "vanta/agent/agent_context.h"

#include <sstream>

#include "internal/projection.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/project/project.h"
#include "vanta/workspace/diagnostic_service.h"
#include "vanta/workspace/document_service.h"
#include "vanta/workspace/git_service.h"
#include "vanta/workspace/index_service.h"

namespace vanta {
namespace {

std::string DiagnosticExplanation(const Diagnostic& diagnostic) {
    std::ostringstream stream;
    stream << diagnostic.location.file.ToUri().ToString() << ':' << diagnostic.location.line << ':' << diagnostic.location.column;
    stream << " reports " << ToString(diagnostic.severity) << " from " << diagnostic.source << ". ";
    stream << diagnostic.message;
    return stream.str();
}

Value AttachmentsProjection(const ProjectModel& model) {
    Value::Array values;
    for (const ProjectAttachment* attachment : model.Attachments()) {
        if (attachment == nullptr) {
            continue;
        }
        values.push_back(Value::ObjectValue({
            {"id", Value(attachment->Id())},
            {"kind", Value(attachment->Kind())},
            {"title", Value(attachment->Title())},
            {"data", attachment->Projection()},
        }));
    }
    return Value::ArrayValue(std::move(values));
}

class DocumentContextProvider final : public AgentContextProvider {
public:
    std::string Id() const override {
        return "vanta.context.documents";
    }

    std::vector<AgentContextItem> Collect(const AgentContextRequest& request, WorkspaceContext& context) const override {
        std::vector<AgentContextItem> items;
        if (request.focus_file.Valid()) {
            AddDocument(context, request.focus_file, "Focused document", items);
        }
        for (const VirtualFile& file : context.Documents().OpenDocuments()) {
            if (request.focus_file.Valid() && file.ToUri() == request.focus_file.ToUri()) {
                continue;
            }
            AddDocument(context, file, "Open document", items);
        }
        return items;
    }

private:
    void AddDocument(WorkspaceContext& context, const VirtualFile& file, std::string title, std::vector<AgentContextItem>& items) const {
        const auto snapshot = context.Documents().ReadSnapshot(file);
        if (!snapshot) {
            return;
        }
        items.push_back({
            .provider_id = Id(),
            .kind = AgentContextKind::kDocument,
            .title = std::move(title),
            .file = file,
            .text = snapshot->text,
            .payload = Value::ObjectValue({
                {"uri", Value(file.ToUri().ToString())},
                {"open", Value(snapshot->open)},
                {"dirty", Value(snapshot->dirty)},
                {"version", Value(static_cast<std::int64_t>(snapshot->version))},
            }),
        });
    }
};

class DiagnosticsContextProvider final : public AgentContextProvider {
public:
    std::string Id() const override {
        return "vanta.context.diagnostics";
    }

    std::vector<AgentContextItem> Collect(const AgentContextRequest& request, WorkspaceContext& context) const override {
        const std::vector<Diagnostic> diagnostics = request.diagnostics.empty() ? context.Diagnostics().AllDiagnostics() : request.diagnostics;
        Value::Array values;
        std::string text;
        for (const Diagnostic& diagnostic : diagnostics) {
            values.push_back(internal::DiagnosticProjection(diagnostic));
            text += DiagnosticExplanation(diagnostic);
            text += '\n';
        }
        if (diagnostics.empty()) {
            return {};
        }
        return {{
            .provider_id = Id(),
            .kind = AgentContextKind::kDiagnostics,
            .title = "Diagnostics",
            .file = {},
            .text = std::move(text),
            .payload = Value::ArrayValue(std::move(values)),
        }};
    }
};

class ProjectModelContextProvider final : public AgentContextProvider {
public:
    std::string Id() const override {
        return "vanta.context.project";
    }

    std::vector<AgentContextItem> Collect(const AgentContextRequest&, WorkspaceContext& context) const override {
        const ProjectModel& model = context.RequireProject().Model();
        return {{
            .provider_id = Id(),
            .kind = AgentContextKind::kProject,
            .title = "Project model",
            .file = model.root,
            .text = PrimaryProjectType(model),
            .payload = Value::ObjectValue({
                {"root", Value(model.root.ToUri().ToString())},
                {"type", Value(PrimaryProjectType(model))},
                {"modules", Value(static_cast<std::int64_t>(model.modules.size()))},
                {"attachments", AttachmentsProjection(model)},
            }),
        }};
    }
};

class JobContextProvider final : public AgentContextProvider {
public:
    std::string Id() const override {
        return "vanta.context.jobs";
    }

    std::vector<AgentContextItem> Collect(const AgentContextRequest&, WorkspaceContext& context) const override {
        Value::Array values;
        std::string text;
        for (const JobRecord& job : context.Jobs().Jobs()) {
            values.push_back(Value::ObjectValue({
                {"id", Value(static_cast<std::int64_t>(job.id))},
                {"kind", Value(ToString(job.kind))},
                {"status", Value(ToString(job.status))},
                {"title", Value(job.title)},
            }));
            text += job.title + " [" + ToString(job.status) + "]\n";
        }
        if (values.empty()) {
            return {};
        }
        return {{
            .provider_id = Id(),
            .kind = AgentContextKind::kJob,
            .title = "Recent jobs",
            .file = {},
            .text = std::move(text),
            .payload = Value::ArrayValue(std::move(values)),
        }};
    }
};

class SearchIndexContextProvider final : public AgentContextProvider {
public:
    std::string Id() const override {
        return "vanta.context.searchIndex";
    }

    std::vector<AgentContextItem> Collect(const AgentContextRequest&, WorkspaceContext& context) const override {
        std::size_t entries = 0;
        if (auto snapshot = context.Indexes().Snapshot("vanta.index.search")) {
            entries = snapshot->item_count;
        }
        return {{
            .provider_id = Id(),
            .kind = AgentContextKind::kSearchIndex,
            .title = "Workspace file index",
            .file = context.CurrentWorkspace().RootFile(),
            .text = std::to_string(entries) + " indexed entries",
            .payload = Value::ObjectValue({
                {"entries", Value(static_cast<std::int64_t>(entries))},
            }),
        }};
    }
};

class GitDiffContextProvider final : public AgentContextProvider {
public:
    std::string Id() const override {
        return "vanta.context.gitDiff";
    }

    std::vector<AgentContextItem> Collect(const AgentContextRequest&, WorkspaceContext& context) const override {
        const GitDiff diff = context.Git().Diff();
        if (diff.exit_code != 0 || diff.text.empty()) {
            return {};
        }
        return {{
            .provider_id = Id(),
            .kind = AgentContextKind::kGitDiff,
            .title = "Git diff",
            .file = context.CurrentWorkspace().RootFile(),
            .text = diff.text,
            .payload = Value::ObjectValue({
                {"exitCode", Value(static_cast<std::int64_t>(diff.exit_code))},
            }),
        }};
    }
};

}

RegistrationHandle AgentContextCollector::RegisterProvider(std::unique_ptr<AgentContextProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string id = provider->Id();
    providers_[id] = std::move(provider);
    return RegistrationHandle([this, id] {
        RemoveProvider(id);
    });
}

void AgentContextCollector::RemoveProvider(const std::string& provider_id) {
    providers_.erase(provider_id);
}

void AgentContextCollector::ClearProviders() {
    providers_.clear();
}

std::vector<std::string> AgentContextCollector::ProviderIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

AgentContext AgentContextCollector::Collect(const AgentContextRequest& request, WorkspaceContext& workspace) const {
    AgentContext context;
    for (const auto& [id, provider] : providers_) {
        (void)id;
        try {
            std::vector<AgentContextItem> items = provider->Collect(request, workspace);
            for (AgentContextItem& item : items) {
                if (context.items.size() >= request.max_items) {
                    return context;
                }
                context.items.push_back(std::move(item));
            }
        } catch (...) {
        }
    }
    return context;
}

void RegisterDefaultAgentContextProviders(AgentContextCollector& service) {
    service.RegisterProvider(std::make_unique<DocumentContextProvider>());
    service.RegisterProvider(std::make_unique<DiagnosticsContextProvider>());
    service.RegisterProvider(std::make_unique<ProjectModelContextProvider>());
    service.RegisterProvider(std::make_unique<JobContextProvider>());
    service.RegisterProvider(std::make_unique<SearchIndexContextProvider>());
    service.RegisterProvider(std::make_unique<GitDiffContextProvider>());
}

}
