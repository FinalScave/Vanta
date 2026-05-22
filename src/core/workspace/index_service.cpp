#include "vanta/workspace/index_service.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <utility>

#include "vanta/core/json_codec.h"
#include "vanta/workspace/workspace_context.h"
#include "vanta/workspace/workspace_runtime.h"

namespace vanta {
namespace {

struct SearchIndexEntry {
    VirtualFile file;
    std::string name;
    bool directory = false;
};

bool ShouldSkipDirectory(const VirtualFile& file) {
    const std::string name = file.DisplayName();
    return name == ".git" || name == ".vanta" || name == "build" || name == ".cache";
}

bool IsDirectory(const VirtualFile& file) {
    return file.Valid() && file.Stat().kind == VirtualFileKind::Directory;
}

std::vector<VirtualFile> SortedVisibleChildren(const VirtualFile& directory) {
    std::vector<VirtualFile> children;
    for (const VirtualFile& child : directory.ListChildren()) {
        if (IsDirectory(child) && ShouldSkipDirectory(child)) {
            continue;
        }
        children.push_back(child);
    }
    std::sort(children.begin(), children.end(), [](const VirtualFile& left, const VirtualFile& right) {
        const bool left_directory = IsDirectory(left);
        const bool right_directory = IsDirectory(right);
        if (left_directory != right_directory) {
            return left_directory > right_directory;
        }
        return left.DisplayName() < right.DisplayName();
    });
    return children;
}

std::string Lowercase(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

int FileScore(const SearchIndexEntry& entry, const std::string& query) {
    if (query.empty()) {
        return 1;
    }
    const std::string name = Lowercase(entry.name);
    const std::string target = Lowercase(entry.file.ToUri().ToString());
    const std::string needle = Lowercase(query);
    if (name == needle) {
        return 100;
    }
    if (name.rfind(needle, 0) == 0) {
        return 80;
    }
    if (name.find(needle) != std::string::npos) {
        return 60;
    }
    if (target.find(needle) != std::string::npos) {
        return 30;
    }
    return 0;
}

Value IndexSnapshotProjection(const IndexSnapshot& snapshot) {
    return Value::ObjectValue({
        {"id", Value(snapshot.id)},
        {"kind", Value(snapshot.kind)},
        {"status", Value(ToString(snapshot.status))},
        {"version", Value(static_cast<std::int64_t>(snapshot.version))},
        {"itemCount", Value(static_cast<std::int64_t>(snapshot.item_count))},
        {"message", Value(snapshot.message)},
    });
}

Value IndexSnapshotsProjection(const std::vector<IndexSnapshot>& snapshots) {
    Value::Array values;
    for (const IndexSnapshot& snapshot : snapshots) {
        values.push_back(IndexSnapshotProjection(snapshot));
    }
    return Value::ArrayValue(std::move(values));
}

std::string TrimPreview(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = line.find_last_not_of(" \t");
    return line.substr(first, last - first + 1);
}

class SearchIndexProvider final : public IndexProvider {
public:
    std::string Id() const override {
        return "vanta.index.search";
    }

    std::string Kind() const override {
        return "text";
    }

    IndexSnapshot Refresh(WorkspaceContext& context, JobContext& job) override {
        job.Report(0.1, "Refreshing search index");
        entries_.clear();
        if (context.CurrentWorkspace().IsOpen()) {
            AddFile(context.CurrentWorkspace().RootFile());
        }
        return {
            .id = Id(),
            .kind = Kind(),
            .status = IndexStatus::Ready,
            .item_count = entries_.size(),
            .message = "Search index ready",
        };
    }

    bool Supports(IndexQueryKind kind) const override {
        return kind == IndexQueryKind::Files || kind == IndexQueryKind::Text;
    }

    IndexQueryResult Query(WorkspaceContext&, const IndexQuery& query) const override {
        if (query.kind == IndexQueryKind::Files) {
            return SearchFiles(query);
        }
        if (query.kind == IndexQueryKind::Text) {
            return SearchText(query);
        }
        return {
            .ok = false,
            .error = "Search index does not support query kind: " + ToString(query.kind),
        };
    }

private:
    void AddFile(const VirtualFile& file) {
        if (!file.Valid() || !file.Exists()) {
            return;
        }
        const bool directory = IsDirectory(file);
        entries_.push_back({
            .file = file,
            .name = file.DisplayName(),
            .directory = directory,
        });
        if (!directory) {
            return;
        }
        for (const VirtualFile& child : SortedVisibleChildren(file)) {
            AddFile(child);
        }
    }

    IndexQueryResult SearchFiles(const IndexQuery& query) const {
        IndexQueryResult result;
        result.ok = true;
        for (const SearchIndexEntry& entry : entries_) {
            const int score = FileScore(entry, query.query);
            if (score == 0) {
                continue;
            }
            result.hits.push_back({
                .file = entry.file,
                .title = entry.name,
                .preview = entry.name,
                .provider_id = Id(),
                .score = score,
            });
        }
        std::sort(result.hits.begin(), result.hits.end(), [](const IndexHit& left, const IndexHit& right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.file.ToUri().ToString() < right.file.ToUri().ToString();
        });
        if (result.hits.size() > query.limit) {
            result.hits.resize(query.limit);
        }
        return result;
    }

    IndexQueryResult SearchText(const IndexQuery& query) const {
        IndexQueryResult result;
        result.ok = true;
        if (query.query.empty()) {
            return result;
        }

        const std::string needle = Lowercase(query.query);
        for (const SearchIndexEntry& entry : entries_) {
            if (entry.directory) {
                continue;
            }
            auto text = entry.file.ReadText();
            if (!text) {
                continue;
            }

            std::istringstream stream(*text);
            std::string line;
            int line_number = 0;
            while (std::getline(stream, line)) {
                const std::string lowered_line = Lowercase(line);
                const std::size_t found = lowered_line.find(needle);
                if (found != std::string::npos) {
                    result.hits.push_back({
                        .file = entry.file,
                        .range = {
                            .start = {.line = line_number, .character = static_cast<int>(found)},
                            .end = {.line = line_number, .character = static_cast<int>(found + query.query.size())},
                        },
                        .title = entry.name,
                        .preview = TrimPreview(line),
                        .provider_id = Id(),
                        .score = 100,
                    });
                    if (result.hits.size() >= query.limit) {
                        return result;
                    }
                }
                ++line_number;
            }
        }
        return result;
    }

    std::vector<SearchIndexEntry> entries_;
};

}

bool IndexProvider::Supports(IndexQueryKind) const {
    return false;
}

IndexQueryResult IndexProvider::Query(WorkspaceContext&, const IndexQuery& query) const {
    return {
        .ok = false,
        .error = "Index provider does not support query kind: " + ToString(query.kind),
    };
}

CodeGraphSnapshot IndexProvider::CodeGraph(WorkspaceContext&) const {
    return {};
}

SymbolQueryResult IndexProvider::Symbols(WorkspaceContext&, const SymbolQuery&) const {
    return {};
}

ReferenceQueryResult IndexProvider::References(WorkspaceContext&, const ReferenceQuery&) const {
    return {};
}

CodeGraphQueryResult IndexProvider::GraphEdges(WorkspaceContext&, const CodeGraphQuery&) const {
    return {};
}

RegistrationHandle IndexService::RegisterProvider(std::unique_ptr<IndexProvider> provider) {
    if (provider == nullptr || provider->Id().empty()) {
        return {};
    }
    const std::string id = provider->Id();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_[id] = std::shared_ptr<IndexProvider>(std::move(provider));
    }
    return RegistrationHandle([this, id] {
        RemoveProvider(id);
    });
}

void IndexService::RemoveProvider(const std::string& provider_id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_.erase(provider_id);
        snapshots_.erase(provider_id);
    }
    Publish();
}

std::vector<std::string> IndexService::ProviderIds() const {
    std::vector<std::string> ids;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

JobId IndexService::Refresh(WorkspaceContext& context, std::string title) {
    JobHandle handle = context.Jobs().Submit({
        .kind = JobKind::Index,
        .title = title.empty() ? "Refresh indexes" : std::move(title),
    }, [this, &context](JobContext& job) {
        bool ok = true;
        std::string message = "Indexes ready";
        std::vector<std::pair<std::string, std::shared_ptr<IndexProvider>>> providers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, provider] : providers_) {
                providers.emplace_back(id, provider);
            }
        }
        std::vector<IndexSnapshot> refreshed;
        for (const auto& [id, provider] : providers) {
            IndexSnapshot value = provider->Refresh(context, job);
            if (value.id.empty()) {
                value.id = id;
            }
            if (value.kind.empty()) {
                value.kind = provider->Kind();
            }
            if (value.status == IndexStatus::Failed) {
                ok = false;
                if (!value.message.empty()) {
                    message = value.message;
                }
            }
            refreshed.push_back(std::move(value));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (IndexSnapshot& value : refreshed) {
                value.version = next_version_++;
                snapshots_[value.id] = std::move(value);
            }
        }
        Publish();
        return JobResult{
            .success = ok,
            .message = std::move(message),
            .payload = Value::ObjectValue({
                {"snapshots", IndexSnapshotsProjection(Snapshots())},
            }),
        };
    });
    return handle.Id();
}

IndexQueryResult IndexService::Query(WorkspaceContext& context, const IndexQuery& query) const {
    IndexQueryResult result;
    result.ok = true;
    std::vector<std::shared_ptr<IndexProvider>> providers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!query.provider_id.empty()) {
            auto provider = providers_.find(query.provider_id);
            if (provider == providers_.end()) {
                return {
                    .ok = false,
                    .error = "Index provider was not found: " + query.provider_id,
                };
            }
            providers.push_back(provider->second);
        } else {
            for (const auto& [id, provider] : providers_) {
                (void)id;
                if (provider->Supports(query.kind)) {
                    providers.push_back(provider);
                }
            }
        }
    }
    if (!query.provider_id.empty()) {
        return providers.front()->Query(context, query);
    }

    for (const std::shared_ptr<IndexProvider>& provider : providers) {
        IndexQueryResult provider_result = provider->Query(context, query);
        if (!provider_result.ok) {
            result.ok = false;
            if (!provider_result.error.empty()) {
                result.error = provider_result.error;
            }
            continue;
        }
        result.hits.insert(result.hits.end(), provider_result.hits.begin(), provider_result.hits.end());
    }
    if (providers.empty()) {
        return {
            .ok = false,
            .error = "No index provider supports query kind: " + ToString(query.kind),
        };
    }
    std::sort(result.hits.begin(), result.hits.end(), [](const IndexHit& left, const IndexHit& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.file.ToUri().ToString() < right.file.ToUri().ToString();
    });
    if (result.hits.size() > query.limit) {
        result.hits.resize(query.limit);
    }
    return result;
}

CodeGraphSnapshot IndexService::CodeGraph(WorkspaceContext& context, std::string provider_id) const {
    CodeGraphSnapshot graph;
    std::vector<std::shared_ptr<IndexProvider>> providers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!provider_id.empty()) {
            auto provider = providers_.find(provider_id);
            if (provider == providers_.end()) {
                return {
                    .ok = false,
                    .error = "Index provider was not found: " + provider_id,
                };
            }
            providers.push_back(provider->second);
        } else {
            for (const auto& [id, provider] : providers_) {
                (void)id;
                providers.push_back(provider);
            }
        }
    }
    if (!provider_id.empty()) {
        return providers.front()->CodeGraph(context);
    }
    if (providers.empty()) {
        return {
            .ok = false,
            .error = "No index provider is registered for code graph queries",
        };
    }

    for (const std::shared_ptr<IndexProvider>& provider : providers) {
        CodeGraphSnapshot provider_graph = provider->CodeGraph(context);
        if (!provider_graph.ok) {
            graph.ok = false;
            if (!provider_graph.error.empty()) {
                graph.error = provider_graph.error;
            }
        }
        graph.symbols.insert(graph.symbols.end(), provider_graph.symbols.begin(), provider_graph.symbols.end());
        graph.references.insert(graph.references.end(), provider_graph.references.begin(), provider_graph.references.end());
        graph.edges.insert(graph.edges.end(), provider_graph.edges.begin(), provider_graph.edges.end());
    }
    return graph;
}

SymbolQueryResult IndexService::Symbols(WorkspaceContext& context, const SymbolQuery& query) const {
    SymbolQueryResult result;
    std::vector<std::shared_ptr<IndexProvider>> providers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!query.provider_id.empty()) {
            auto provider = providers_.find(query.provider_id);
            if (provider == providers_.end()) {
                return {
                    .ok = false,
                    .error = "Index provider was not found: " + query.provider_id,
                };
            }
            providers.push_back(provider->second);
        } else {
            for (const auto& [id, provider] : providers_) {
                (void)id;
                providers.push_back(provider);
            }
        }
    }
    if (!query.provider_id.empty()) {
        return providers.front()->Symbols(context, query);
    }
    if (providers.empty()) {
        return {
            .ok = false,
            .error = "No index provider is registered for symbol queries",
        };
    }
    for (const std::shared_ptr<IndexProvider>& provider : providers) {
        SymbolQueryResult provider_result = provider->Symbols(context, query);
        if (!provider_result.ok) {
            result.ok = false;
            if (!provider_result.error.empty()) {
                result.error = provider_result.error;
            }
            continue;
        }
        result.symbols.insert(result.symbols.end(), provider_result.symbols.begin(), provider_result.symbols.end());
        if (result.symbols.size() >= query.limit) {
            result.symbols.resize(query.limit);
            return result;
        }
    }
    return result;
}

ReferenceQueryResult IndexService::References(WorkspaceContext& context, const ReferenceQuery& query) const {
    ReferenceQueryResult result;
    std::vector<std::shared_ptr<IndexProvider>> providers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!query.provider_id.empty()) {
            auto provider = providers_.find(query.provider_id);
            if (provider == providers_.end()) {
                return {
                    .ok = false,
                    .error = "Index provider was not found: " + query.provider_id,
                };
            }
            providers.push_back(provider->second);
        } else {
            for (const auto& [id, provider] : providers_) {
                (void)id;
                providers.push_back(provider);
            }
        }
    }
    if (!query.provider_id.empty()) {
        return providers.front()->References(context, query);
    }
    if (providers.empty()) {
        return {
            .ok = false,
            .error = "No index provider is registered for reference queries",
        };
    }
    for (const std::shared_ptr<IndexProvider>& provider : providers) {
        ReferenceQueryResult provider_result = provider->References(context, query);
        if (!provider_result.ok) {
            result.ok = false;
            if (!provider_result.error.empty()) {
                result.error = provider_result.error;
            }
            continue;
        }
        result.references.insert(result.references.end(), provider_result.references.begin(), provider_result.references.end());
        if (result.references.size() >= query.limit) {
            result.references.resize(query.limit);
            return result;
        }
    }
    return result;
}

CodeGraphQueryResult IndexService::GraphEdges(WorkspaceContext& context, const CodeGraphQuery& query) const {
    CodeGraphQueryResult result;
    std::vector<std::shared_ptr<IndexProvider>> providers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!query.provider_id.empty()) {
            auto provider = providers_.find(query.provider_id);
            if (provider == providers_.end()) {
                return {
                    .ok = false,
                    .error = "Index provider was not found: " + query.provider_id,
                };
            }
            providers.push_back(provider->second);
        } else {
            for (const auto& [id, provider] : providers_) {
                (void)id;
                providers.push_back(provider);
            }
        }
    }
    if (!query.provider_id.empty()) {
        return providers.front()->GraphEdges(context, query);
    }
    if (providers.empty()) {
        return {
            .ok = false,
            .error = "No index provider is registered for graph edge queries",
        };
    }
    for (const std::shared_ptr<IndexProvider>& provider : providers) {
        CodeGraphQueryResult provider_result = provider->GraphEdges(context, query);
        if (!provider_result.ok) {
            result.ok = false;
            if (!provider_result.error.empty()) {
                result.error = provider_result.error;
            }
            continue;
        }
        result.edges.insert(result.edges.end(), provider_result.edges.begin(), provider_result.edges.end());
        if (result.edges.size() >= query.limit) {
            result.edges.resize(query.limit);
            return result;
        }
    }
    return result;
}

std::vector<IndexSnapshot> IndexService::Snapshots() const {
    std::vector<IndexSnapshot> values;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, snapshot] : snapshots_) {
        (void)id;
        values.push_back(snapshot);
    }
    return values;
}

std::optional<IndexSnapshot> IndexService::Snapshot(const std::string& provider_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = snapshots_.find(provider_id);
    return it == snapshots_.end() ? std::nullopt : std::optional<IndexSnapshot>(it->second);
}

void IndexService::ClearSnapshots() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots_.clear();
    }
    Publish();
}

std::uint64_t IndexService::OnDidChangeIndex(EventBus<IndexChangeEvent>::Listener listener) {
    return on_did_change_.Subscribe(std::move(listener));
}

void IndexService::RemoveIndexListener(std::uint64_t listener_id) {
    on_did_change_.Unsubscribe(listener_id);
}

void IndexService::Publish() {
    on_did_change_.Publish({.snapshots = Snapshots()});
}

void RegisterDefaultIndexProviders(IndexService& indexes) {
    indexes.RegisterProvider(std::make_unique<SearchIndexProvider>());
}

std::string ToString(IndexStatus status) {
    switch (status) {
    case IndexStatus::Unknown:
        return "unknown";
    case IndexStatus::Indexing:
        return "indexing";
    case IndexStatus::Ready:
        return "ready";
    case IndexStatus::Stale:
        return "stale";
    case IndexStatus::Failed:
        return "failed";
    }
    return "unknown";
}

std::string ToString(IndexQueryKind kind) {
    switch (kind) {
    case IndexQueryKind::Files:
        return "files";
    case IndexQueryKind::Text:
        return "text";
    case IndexQueryKind::Includes:
        return "includes";
    case IndexQueryKind::Custom:
        return "custom";
    }
    return "files";
}

}
