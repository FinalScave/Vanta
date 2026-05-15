#include "vanta/agent/agent_context.h"

#include <algorithm>

#include "vanta/workspace/workspace_context.h"
#include "vanta/project/project.h"
#include "vanta/project/project_manager.h"
#include "vanta/workspace/search_service.h"

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

Json attachmentInfosToJson(const ProjectModel& model) {
    Json::Array values;
    for (const ProjectAttachmentInfo& info : model.attachmentInfos) {
        values.push_back(Json::object({
            {"id", Json(info.id)},
            {"kind", Json(info.kind)},
            {"title", Json(info.title)},
            {"summary", info.summary},
        }));
    }
    return Json::array(std::move(values));
}

class DocumentContextProvider final : public AgentContextProvider {
public:
    std::string id() const override {
        return "vanta.context.documents";
    }

    std::vector<AgentContextItem> collect(const AgentContextRequest& request, WorkspaceContext& context) const override {
        std::vector<AgentContextItem> items;
        if (request.focusFile.valid()) {
            addDocument(context, request.focusFile, "Focused document", items);
        }
        for (const VirtualFile& file : context.documents().openDocuments()) {
            if (request.focusFile.valid() && file.toUri() == request.focusFile.toUri()) {
                continue;
            }
            addDocument(context, file, "Open document", items);
        }
        return items;
    }

private:
    void addDocument(WorkspaceContext& context, const VirtualFile& file, std::string title, std::vector<AgentContextItem>& items) const {
        const auto snapshot = context.documents().readSnapshot(file);
        if (!snapshot) {
            return;
        }
        items.push_back({
            .providerId = id(),
            .kind = AgentContextItemKind::Document,
            .title = std::move(title),
            .file = file,
            .text = snapshot->text,
            .data = Json::object({
                {"uri", Json(file.toUri().string())},
                {"open", Json(snapshot->open)},
                {"dirty", Json(snapshot->dirty)},
                {"version", Json(static_cast<std::int64_t>(snapshot->version))},
            }),
        });
    }
};

class DiagnosticsContextProvider final : public AgentContextProvider {
public:
    std::string id() const override {
        return "vanta.context.diagnostics";
    }

    std::vector<AgentContextItem> collect(const AgentContextRequest& request, WorkspaceContext& context) const override {
        const std::vector<Diagnostic> diagnostics = request.diagnostics.empty() ? context.diagnostics().allDiagnostics() : request.diagnostics;
        Json::Array values;
        std::string text;
        for (const Diagnostic& diagnostic : diagnostics) {
            values.push_back(diagnosticToJson(diagnostic));
            text += context.agent().explainDiagnostic(diagnostic);
            text += '\n';
        }
        if (diagnostics.empty()) {
            return {};
        }
        return {{
            .providerId = id(),
            .kind = AgentContextItemKind::Diagnostics,
            .title = "Diagnostics",
            .file = {},
            .text = std::move(text),
            .data = Json::array(std::move(values)),
        }};
    }
};

class ProjectModelContextProvider final : public AgentContextProvider {
public:
    std::string id() const override {
        return "vanta.context.project";
    }

    std::vector<AgentContextItem> collect(const AgentContextRequest&, WorkspaceContext& context) const override {
        const ProjectModel& model = context.requireProject().model();
        return {{
            .providerId = id(),
            .kind = AgentContextItemKind::Project,
            .title = "Project model",
            .file = model.root,
            .text = primaryProjectType(model),
            .data = Json::object({
                {"root", Json(model.root.toUri().string())},
                {"type", Json(primaryProjectType(model))},
                {"modules", Json(static_cast<std::int64_t>(model.modules.size()))},
                {"attachments", attachmentInfosToJson(model)},
            }),
        }};
    }
};

class JobContextProvider final : public AgentContextProvider {
public:
    std::string id() const override {
        return "vanta.context.jobs";
    }

    std::vector<AgentContextItem> collect(const AgentContextRequest&, WorkspaceContext& context) const override {
        Json::Array values;
        std::string text;
        for (const JobRecord& job : context.jobs().jobs()) {
            values.push_back(Json::object({
                {"id", Json(static_cast<std::int64_t>(job.id))},
                {"kind", Json(toString(job.kind))},
                {"status", Json(toString(job.status))},
                {"title", Json(job.title)},
            }));
            text += job.title + " [" + toString(job.status) + "]\n";
        }
        if (values.empty()) {
            return {};
        }
        return {{
            .providerId = id(),
            .kind = AgentContextItemKind::Job,
            .title = "Recent jobs",
            .file = {},
            .text = std::move(text),
            .data = Json::array(std::move(values)),
        }};
    }
};

class SearchIndexContextProvider final : public AgentContextProvider {
public:
    std::string id() const override {
        return "vanta.context.searchIndex";
    }

    std::vector<AgentContextItem> collect(const AgentContextRequest&, WorkspaceContext& context) const override {
        return {{
            .providerId = id(),
            .kind = AgentContextItemKind::SearchIndex,
            .title = "Workspace file index",
            .file = context.workspace().rootFile(),
            .text = std::to_string(context.search().entries().size()) + " indexed entries",
            .data = Json::object({
                {"entries", Json(static_cast<std::int64_t>(context.search().entries().size()))},
            }),
        }};
    }
};

class GitDiffContextProvider final : public AgentContextProvider {
public:
    explicit GitDiffContextProvider(const GitClient& git) : git_(git) {}

    std::string id() const override {
        return "vanta.context.gitDiff";
    }

    std::vector<AgentContextItem> collect(const AgentContextRequest&, WorkspaceContext& context) const override {
        const GitDiff diff = git_.diff(context.workspace().info().rootPath);
        if (diff.exitCode != 0 || diff.text.empty()) {
            return {};
        }
        return {{
            .providerId = id(),
            .kind = AgentContextItemKind::GitDiff,
            .title = "Git diff",
            .file = context.workspace().rootFile(),
            .text = diff.text,
            .data = Json::object({
                {"exitCode", Json(static_cast<std::int64_t>(diff.exitCode))},
            }),
        }};
    }

private:
    const GitClient& git_;
};

}

void AgentContextCollector::addProvider(std::unique_ptr<AgentContextProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return;
    }
    providers_[provider->id()] = std::move(provider);
}

void AgentContextCollector::removeProvider(const std::string& providerId) {
    providers_.erase(providerId);
}

void AgentContextCollector::clearProviders() {
    providers_.clear();
}

std::vector<std::string> AgentContextCollector::providerIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

AgentContext AgentContextCollector::collect(const AgentContextRequest& request, WorkspaceContext& workspace) const {
    AgentContext context;
    for (const auto& [id, provider] : providers_) {
        (void)id;
        try {
            std::vector<AgentContextItem> items = provider->collect(request, workspace);
            for (AgentContextItem& item : items) {
                if (context.items.size() >= request.maxItems) {
                    return context;
                }
                context.items.push_back(std::move(item));
            }
        } catch (...) {
        }
    }
    return context;
}

void registerDefaultAgentContextProviders(AgentContextCollector& service) {
    service.addProvider(std::make_unique<DocumentContextProvider>());
    service.addProvider(std::make_unique<DiagnosticsContextProvider>());
    service.addProvider(std::make_unique<ProjectModelContextProvider>());
    service.addProvider(std::make_unique<JobContextProvider>());
    service.addProvider(std::make_unique<SearchIndexContextProvider>());
}

std::unique_ptr<AgentContextProvider> createGitDiffAgentContextProvider(const GitClient& git) {
    return std::make_unique<GitDiffContextProvider>(git);
}

std::string toString(AgentContextItemKind kind) {
    switch (kind) {
    case AgentContextItemKind::Text:
        return "text";
    case AgentContextItemKind::Document:
        return "document";
    case AgentContextItemKind::Diagnostics:
        return "diagnostics";
    case AgentContextItemKind::Project:
        return "project";
    case AgentContextItemKind::Job:
        return "job";
    case AgentContextItemKind::SearchIndex:
        return "searchIndex";
    case AgentContextItemKind::GitDiff:
        return "gitDiff";
    }
    return "text";
}

Json toJson(const AgentContextItem& item) {
    return Json::object({
        {"providerId", Json(item.providerId)},
        {"kind", Json(toString(item.kind))},
        {"title", Json(item.title)},
        {"file", Json(item.file.toUri().string())},
        {"text", Json(item.text)},
        {"data", item.data},
    });
}

Json toJson(const AgentContext& context) {
    Json::Array items;
    for (const AgentContextItem& item : context.items) {
        items.push_back(toJson(item));
    }
    return Json::object({
        {"items", Json::array(std::move(items))},
    });
}

}
