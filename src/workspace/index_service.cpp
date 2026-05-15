#include "vanta/workspace/index_service.h"

#include <cstdint>
#include <utility>

#include "vanta/builtin/cpp/cpp_index.h"
#include "vanta/workspace/search_service.h"
#include "vanta/workspace/workspace_context.h"

namespace vanta {
namespace {

class SearchIndexProvider final : public IndexProvider {
public:
    explicit SearchIndexProvider(SearchService& search) : search_(search) {}

    std::string id() const override {
        return "vanta.index.search";
    }

    std::string kind() const override {
        return "text";
    }

    IndexSnapshot refresh(WorkspaceContext& context, JobContext& job) override {
        job.report(0.1, "Refreshing search index");
        search_.rebuild(context.workspace());
        return {
            .id = id(),
            .kind = kind(),
            .status = IndexStatus::Ready,
            .itemCount = search_.entries().size(),
            .message = "Search index ready",
            .data = Json::object({
                {"entries", Json(static_cast<std::int64_t>(search_.entries().size()))},
            }),
        };
    }

private:
    SearchService& search_;
};

class CppSemanticIndexAdapter final : public IndexProvider {
public:
    explicit CppSemanticIndexAdapter(CppSemanticIndex& cppIndex) : cppIndex_(cppIndex) {}

    std::string id() const override {
        return "vanta.index.cpp";
    }

    std::string kind() const override {
        return "semantic.cpp";
    }

    IndexSnapshot refresh(WorkspaceContext&, JobContext& job) override {
        job.report(0.6, "Refreshing C++ semantic index snapshot");
        const CppSemanticIndexSnapshot value = cppIndex_.snapshot();
        const std::size_t translationUnits = value.compilationDatabase.translationUnits.size();
        return {
            .id = id(),
            .kind = kind(),
            .status = value.ready ? IndexStatus::Ready : IndexStatus::Unknown,
            .itemCount = translationUnits,
            .message = value.ready ? "C++ semantic index ready" : "C++ semantic index unavailable",
            .data = Json::object({
                {"providerId", Json(value.providerId)},
                {"translationUnits", Json(static_cast<std::int64_t>(translationUnits))},
                {"metadata", value.metadata},
            }),
        };
    }

private:
    CppSemanticIndex& cppIndex_;
};

}

void IndexCoordinator::addProvider(std::unique_ptr<IndexProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return;
    }
    const std::string id = provider->id();
    std::lock_guard<std::mutex> lock(mutex_);
    providers_[id] = std::move(provider);
}

RegistrationHandle IndexCoordinator::registerProvider(std::unique_ptr<IndexProvider> provider) {
    if (provider == nullptr || provider->id().empty()) {
        return {};
    }
    const std::string id = provider->id();
    addProvider(std::move(provider));
    return RegistrationHandle([this, id] {
        removeProvider(id);
    });
}

void IndexCoordinator::removeProvider(const std::string& providerId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_.erase(providerId);
        snapshots_.erase(providerId);
    }
    publish();
}

std::vector<std::string> IndexCoordinator::providerIds() const {
    std::vector<std::string> ids;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, provider] : providers_) {
        (void)provider;
        ids.push_back(id);
    }
    return ids;
}

JobId IndexCoordinator::refresh(WorkspaceContext& context, JobService& jobs, AsyncRuntime& runtime, std::string title) {
    JobHandle handle = jobs.submit(runtime, {
        .kind = JobKind::Index,
        .title = title.empty() ? "Refresh indexes" : std::move(title),
    }, [this, &context](JobContext& job) {
        bool ok = true;
        std::string message = "Indexes ready";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, provider] : providers_) {
                IndexSnapshot value = provider->refresh(context, job);
                if (value.id.empty()) {
                    value.id = id;
                }
                if (value.kind.empty()) {
                    value.kind = provider->kind();
                }
                value.version = nextVersion_++;
                if (!value.data.isObject()) {
                    value.data = Json::object();
                }
                if (value.status == IndexStatus::Failed) {
                    ok = false;
                    if (!value.message.empty()) {
                        message = value.message;
                    }
                }
                snapshots_[value.id] = std::move(value);
            }
        }
        publish();
        return JobResult{
            .success = ok,
            .message = std::move(message),
            .data = Json::object({
                {"snapshots", toJson(snapshots())},
            }),
        };
    });
    return handle.id();
}

std::vector<IndexSnapshot> IndexCoordinator::snapshots() const {
    std::vector<IndexSnapshot> values;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, snapshot] : snapshots_) {
        (void)id;
        values.push_back(snapshot);
    }
    return values;
}

std::optional<IndexSnapshot> IndexCoordinator::snapshot(const std::string& providerId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = snapshots_.find(providerId);
    return it == snapshots_.end() ? std::nullopt : std::optional<IndexSnapshot>(it->second);
}

void IndexCoordinator::clearSnapshots() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots_.clear();
    }
    publish();
}

std::uint64_t IndexCoordinator::onDidChangeIndex(EventBus<IndexChangeEvent>::Listener listener) {
    return onDidChange_.subscribe(std::move(listener));
}

void IndexCoordinator::removeIndexListener(std::uint64_t listenerId) {
    onDidChange_.unsubscribe(listenerId);
}

void IndexCoordinator::publish() {
    onDidChange_.publish({.snapshots = snapshots()});
}

void registerDefaultIndexProviders(IndexCoordinator& indexes, SearchService& search, CppSemanticIndex& cppIndex) {
    indexes.addProvider(std::make_unique<SearchIndexProvider>(search));
    indexes.addProvider(std::make_unique<CppSemanticIndexAdapter>(cppIndex));
}

std::string toString(IndexStatus status) {
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

Json toJson(const IndexSnapshot& snapshot) {
    return Json::object({
        {"id", Json(snapshot.id)},
        {"kind", Json(snapshot.kind)},
        {"status", Json(toString(snapshot.status))},
        {"version", Json(static_cast<std::int64_t>(snapshot.version))},
        {"itemCount", Json(static_cast<std::int64_t>(snapshot.itemCount))},
        {"message", Json(snapshot.message)},
        {"data", snapshot.data},
    });
}

Json toJson(const std::vector<IndexSnapshot>& snapshots) {
    Json::Array values;
    for (const IndexSnapshot& snapshot : snapshots) {
        values.push_back(toJson(snapshot));
    }
    return Json::array(std::move(values));
}

}
