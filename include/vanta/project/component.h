#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/workspace/ide_event.h"
#include "vanta/platform/json.h"

namespace vanta {

class AgentToolRegistry;
class ChangeSetService;
class CommandRegistry;
class Component;
class ContributionRegistry;
class CppSemanticIndex;
class DiagnosticService;
class DocumentService;
class LanguageRequestPipeline;
class DefaultLanguageRegistry;
class Project;
class RunConfigurationService;
class ExecutionService;
class SearchService;
class Workspace;
class WorkspaceRuntime;
class WorkspaceContext;
struct ProjectModel;

struct ComponentMatch {
    bool allProjects = false;
    std::vector<std::string> projectTypes;
    std::vector<std::string> facets;

    bool matches(const ProjectModel& model) const;
};

struct ComponentContribution {
    std::string id;
    std::string title;
    std::string pluginId;
    ComponentMatch match;
    std::function<std::unique_ptr<Component>()> factory;
};

class ComponentContributionRegistry {
public:
    void add(ComponentContribution contribution);
    bool remove(const std::string& id);
    std::optional<ComponentContribution> contribution(const std::string& id) const;
    std::vector<ComponentContribution> list() const;
    std::vector<ComponentContribution> matching(const ProjectModel& model) const;

private:
    std::map<std::string, ComponentContribution> contributions_;
};

struct ProjectState {
    int schemaVersion = 1;
    std::map<std::string, Json> componentStates;
};

class Component {
public:
    virtual ~Component() = default;

    virtual std::string id() const = 0;
    virtual void onAttach(WorkspaceContext& context);
    virtual void restoreState(const Json& state);
    virtual void onOpenProject(Project& project);
    virtual void onProjectChanged(Project& project);
    virtual Json saveState() const;
    virtual void onCloseProject(Project& project);
    virtual void onDetach();
};

class ComponentRegistry {
public:
    void bind(std::unique_ptr<Component> component);
    bool unbind(const std::string& id);
    Component* get(const std::string& id);
    const Component* get(const std::string& id) const;

    template <class T>
    T* get(const std::string& id) {
        return dynamic_cast<T*>(get(id));
    }

    template <class T>
    const T* get(const std::string& id) const {
        return dynamic_cast<const T*>(get(id));
    }

    std::vector<std::string> ids() const;
    void rememberState(const ProjectState& state);
    void attachAll(WorkspaceContext& context);
    void restoreAll(const ProjectState& state);
    void openProject(Project& project);
    void projectChanged(Project& project);
    ProjectState saveAll(const ProjectState& previous = {}) const;
    void closeProject(Project& project);
    void detachAll();

private:
    struct Entry {
        std::unique_ptr<Component> component;
        bool attached = false;
        bool restored = false;
        bool projectOpened = false;
    };

    bool attachEntry(const std::string& id, Entry& entry);
    void restoreEntry(const std::string& id, Entry& entry, const ProjectState& state);
    void openEntry(Entry& entry, Project& project);
    void projectChangedEntry(Entry& entry, Project& project);
    void closeEntry(Entry& entry, Project& project);
    void detachEntry(const std::string& id, Entry& entry);

    std::map<std::string, Entry> components_;
    WorkspaceContext* context_ = nullptr;
    ProjectState restoredState_;
    bool hasRestoredState_ = false;
    Project* openProject_ = nullptr;
    bool projectOpen_ = false;
};

}
