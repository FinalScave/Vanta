#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/execution/job_service.h"
#include "vanta/platform/json.h"

namespace vanta {

class CppSemanticIndex;
class AsyncRuntime;
class SearchService;
class WorkspaceContext;

enum class IndexStatus {
    Unknown,
    Indexing,
    Ready,
    Stale,
    Failed,
};

struct IndexSnapshot {
    std::string id;
    std::string kind;
    IndexStatus status = IndexStatus::Unknown;
    std::uint64_t version = 0;
    std::size_t itemCount = 0;
    std::string message;
    Json data;
};

struct IndexChangeEvent {
    std::vector<IndexSnapshot> snapshots;
};

class IndexProvider {
public:
    virtual ~IndexProvider() = default;

    virtual std::string id() const = 0;
    virtual std::string kind() const = 0;
    virtual IndexSnapshot refresh(WorkspaceContext& context, JobContext& job) = 0;
};

class IndexCoordinator {
public:
    void addProvider(std::unique_ptr<IndexProvider> provider);
    RegistrationHandle registerProvider(std::unique_ptr<IndexProvider> provider);
    void removeProvider(const std::string& providerId);
    std::vector<std::string> providerIds() const;

    JobId refresh(WorkspaceContext& context, JobService& jobs, AsyncRuntime& runtime, std::string title = {});
    std::vector<IndexSnapshot> snapshots() const;
    std::optional<IndexSnapshot> snapshot(const std::string& providerId) const;
    void clearSnapshots();
    std::uint64_t onDidChangeIndex(EventBus<IndexChangeEvent>::Listener listener);
    void removeIndexListener(std::uint64_t listenerId);

private:
    void publish();

    std::map<std::string, std::unique_ptr<IndexProvider>> providers_;
    std::map<std::string, IndexSnapshot> snapshots_;
    std::uint64_t nextVersion_ = 1;
    mutable std::mutex mutex_;
    EventBus<IndexChangeEvent> onDidChange_;
};

void registerDefaultIndexProviders(IndexCoordinator& indexes, SearchService& search, CppSemanticIndex& cppIndex);
std::string toString(IndexStatus status);
Json toJson(const IndexSnapshot& snapshot);
Json toJson(const std::vector<IndexSnapshot>& snapshots);

}
