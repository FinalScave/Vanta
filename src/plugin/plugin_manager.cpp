#include "vanta/plugin/plugin_manager.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

#include "vanta/plugin/plugin_process_host.h"
#include "vanta/project/project_manager.h"

namespace vanta {
namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

std::vector<std::string> parseStringArray(const Json& object, const std::string& key) {
    std::vector<std::string> result;
    if (!object.contains(key) || !object[key].isArray()) {
        return result;
    }
    for (const Json& item : object[key].asArray()) {
        if (item.isString()) {
            result.push_back(item.asString());
        }
    }
    return result;
}

class ScopedWorkspaceContext final : public WorkspaceContext {
public:
    ScopedWorkspaceContext(
        WorkspaceContext& base,
        std::shared_ptr<PluginActivationState> state,
        PermissionSet permissions,
        ApprovalService& approvals,
        std::string subject)
        : WorkspaceContext(*base.runtime()),
          base_(base),
          state_(std::move(state)),
          permissions_(std::move(permissions)),
          approvals_(approvals),
          subject_(std::move(subject)) {}

    void add(const std::string& id, CommandHandler handler) override {
        state_->track(base_.commands().registerCommand(id, std::move(handler)));
    }

    RegistrationHandle registerCommand(const std::string& id, CommandHandler handler) override {
        return base_.commands().registerCommand(id, std::move(handler));
    }

    std::optional<Json> execute(const std::string& id, const Json& arguments) const override {
        return base_.commands().execute(id, arguments);
    }

    std::vector<std::string> list() const override {
        return base_.commands().list();
    }

    RegistrationHandle subscribe(IdeEventBus::Listener listener) override {
        RegistrationHandle registration = base_.events().subscribe(std::move(listener));
        if (registration.registered()) {
            state_->track(std::move(registration));
        }
        return {};
    }

    RegistrationHandle subscribe(IdeEventKind kind, IdeEventBus::Listener listener) override {
        RegistrationHandle registration = base_.events().subscribe(kind, std::move(listener));
        if (registration.registered()) {
            state_->track(std::move(registration));
        }
        return {};
    }

    void publish(IdeEvent event) override {
        base_.events().publish(std::move(event));
    }

    const WorkspaceInfo& info() const override {
        return base_.workspaceFiles().info();
    }

    std::optional<std::string> readTextFile(const VirtualFile& file) const override {
        if (!permissions_.contains(Permission::WorkspaceRead)) {
            return std::nullopt;
        }
        return base_.workspaceFiles().readTextFile(file);
    }

    bool writeTextFile(const VirtualFile& file, const std::string& text, std::string* errorMessage) override {
        if (!permissions_.contains(Permission::WorkspaceWrite)) {
            if (errorMessage != nullptr) {
                *errorMessage = "Missing workspace.write permission";
            }
            return false;
        }
        if (approvals_.requestApproval({.subject = subject_, .permission = Permission::WorkspaceWrite, .action = "write " + file.toUri().string(), .highRisk = true}) == ApprovalDecision::Deny) {
            if (errorMessage != nullptr) {
                *errorMessage = "workspace.write was denied";
            }
            return false;
        }
        return base_.workspaceFiles().writeTextFile(file, text, errorMessage);
    }

    void addLanguage(Language language) override {
        if (!permissions_.contains(Permission::LanguageService)) {
            return;
        }
        state_->track(base_.languages().registerLanguage(std::move(language)));
    }

    RegistrationHandle registerLanguage(Language language) override {
        if (!permissions_.contains(Permission::LanguageService)) {
            return {};
        }
        return base_.languages().registerLanguage(std::move(language));
    }

    std::vector<Language> languages() const override {
        return base_.languages().languages();
    }

    const Language* languageForFile(const VirtualFile& file) const override {
        return base_.languages().languageForFile(file);
    }

    const Language* languageForFile(const VirtualFile& file, const LanguageResolutionContext& context) const override {
        return base_.languages().languageForFile(file, context);
    }

    const Language* languageForId(const std::string& languageId) const override {
        return base_.languages().languageForId(languageId);
    }

    const Language* languageForId(const std::string& languageId, const LanguageResolutionContext& context) const override {
        return base_.languages().languageForId(languageId, context);
    }

    LanguageService* serviceForLanguage(const std::string& languageId) const override {
        return base_.languages().serviceForLanguage(languageId);
    }

    LanguageService* serviceForLanguage(const std::string& languageId, const LanguageResolutionContext& context) const override {
        return base_.languages().serviceForLanguage(languageId, context);
    }

    LanguageService* serviceForDocument(const VirtualFile& file) const override {
        return base_.languages().serviceForDocument(file);
    }

    LanguageService* serviceForDocument(const VirtualFile& file, const LanguageResolutionContext& context) const override {
        return base_.languages().serviceForDocument(file, context);
    }

    std::string languageIdForFile(const VirtualFile& file) const override {
        return base_.languages().languageIdForFile(file);
    }

    std::string languageIdForFile(const VirtualFile& file, const LanguageResolutionContext& context) const override {
        return base_.languages().languageIdForFile(file, context);
    }

    std::vector<std::string> languageIds() const override {
        return base_.languages().languageIds();
    }

    void addProvider(std::unique_ptr<BuildProvider> provider) override {
        if (!permissions_.contains(Permission::BuildProvider)) {
            return;
        }
        state_->track(base_.build().registerProvider(std::move(provider)));
    }

    RegistrationHandle registerProvider(std::unique_ptr<BuildProvider> provider) override {
        if (!permissions_.contains(Permission::BuildProvider)) {
            return {};
        }
        return base_.build().registerProvider(std::move(provider));
    }

    void removeProvider(const std::string& providerId) override {
        base_.build().removeProvider(providerId);
    }

    std::vector<std::string> buildProviderIds() const override {
        return base_.build().buildProviderIds();
    }

    BuildEnvironment detect(const std::filesystem::path& workspaceRoot) const override {
        return base_.build().detect(workspaceRoot);
    }

    BuildHandle start(WorkspaceContext& context, const std::filesystem::path& workspaceRoot, const BuildTask& task, ExecutionEventCallback onEvent = {}) const override {
        if (!permissions_.contains(Permission::ProcessExecute)) {
            if (onEvent) {
                onEvent({
                    .kind = ExecutionEventKind::Finished,
                    .jobId = task.jobId,
                    .text = "Missing process.execute permission\n",
                    .progress = 1.0,
                    .exitCode = -1,
                });
            }
            return {};
        }
        if (approvals_.requestApproval({.subject = subject_, .permission = Permission::ProcessExecute, .action = "run " + toString(task.kind), .highRisk = true}) == ApprovalDecision::Deny) {
            if (onEvent) {
                onEvent({
                    .kind = ExecutionEventKind::Finished,
                    .jobId = task.jobId,
                    .text = "process.execute was denied\n",
                    .progress = 1.0,
                    .exitCode = -1,
                });
            }
            return {};
        }
        return base_.build().start(context, workspaceRoot, task, std::move(onEvent));
    }

    BuildResult run(WorkspaceContext& context, const std::filesystem::path& workspaceRoot, const BuildTask& task, ExecutionEventCallback onEvent = {}) const override {
        if (!permissions_.contains(Permission::ProcessExecute)) {
            return {
                .exitCode = -1,
                .output = "Missing process.execute permission\n",
                .diagnostics = {},
            };
        }
        if (approvals_.requestApproval({.subject = subject_, .permission = Permission::ProcessExecute, .action = "run " + toString(task.kind), .highRisk = true}) == ApprovalDecision::Deny) {
            return {
                .exitCode = -1,
                .output = "process.execute was denied\n",
                .diagnostics = {},
            };
        }
        return base_.build().run(context, workspaceRoot, task, std::move(onEvent));
    }

    void contribute(ComponentContribution contribution) override {
        state_->track(base_.components().registerContribution(std::move(contribution)));
    }

    RegistrationHandle registerContribution(ComponentContribution contribution) override {
        return base_.components().registerContribution(std::move(contribution));
    }

    std::vector<ComponentContribution> contributions() const override {
        return base_.components().contributions();
    }

    void addProvider(std::unique_ptr<ProjectModelProvider> provider) override {
        state_->track(base_.projectModels().registerProvider(std::move(provider)));
    }

    RegistrationHandle registerProvider(std::unique_ptr<ProjectModelProvider> provider) override {
        return base_.projectModels().registerProvider(std::move(provider));
    }

    std::vector<std::string> modelProviderIds() const override {
        return base_.projectModels().modelProviderIds();
    }

    void addTool(AgentToolDefinition definition) override {
        if (!permissions_.contains(Permission::AgentTool)) {
            return;
        }
        state_->track(base_.agentTools().registerTool(std::move(definition)));
    }

    RegistrationHandle registerTool(AgentToolDefinition definition) override {
        if (!permissions_.contains(Permission::AgentTool)) {
            return {};
        }
        return base_.agentTools().registerTool(std::move(definition));
    }

    void addProvider(std::unique_ptr<AgentContextProvider> provider) override {
        state_->track(base_.agentContext().registerProvider(std::move(provider)));
    }

    RegistrationHandle registerProvider(std::unique_ptr<AgentContextProvider> provider) override {
        return base_.agentContext().registerProvider(std::move(provider));
    }

    AgentContext collect(const AgentContextRequest& request) const override {
        return base_.agentContext().collect(request);
    }

    AgentOperationService& agentOperations() override {
        return base_.agentOperations();
    }

    ApprovalService& approvals() override {
        return approvals_;
    }

    void addProvider(std::unique_ptr<ExecutionProvider> provider) override {
        if (!permissions_.contains(Permission::ProcessExecute)) {
            return;
        }
        state_->track(base_.execution().registerProvider(std::move(provider)));
    }

    RegistrationHandle registerProvider(std::unique_ptr<ExecutionProvider> provider) override {
        if (!permissions_.contains(Permission::ProcessExecute)) {
            return {};
        }
        return base_.execution().registerProvider(std::move(provider));
    }

    std::vector<std::string> providerIds() const override {
        return base_.execution().providerIds();
    }

    std::vector<ExecutionTarget> targets() const override {
        return base_.execution().targets();
    }

    ExecutionHandle start(const ExecutionRequest& request, const ExecutionTarget& target, ExecutionEventCallback onEvent = {}) const override {
        if (!permissions_.contains(Permission::ProcessExecute)) {
            ExecutionEvent event{
                .kind = ExecutionEventKind::Finished,
                .jobId = request.jobId,
                .executorId = target.executorId,
                .targetId = target.id,
                .text = "Missing process.execute permission\n",
                .progress = 1.0,
                .exitCode = -1,
            };
            if (onEvent) {
                onEvent(event);
            }
            return {};
        }
        if (approvals_.requestApproval({.subject = subject_, .permission = Permission::ProcessExecute, .action = "execute " + request.executable, .highRisk = true}) == ApprovalDecision::Deny) {
            ExecutionEvent event{
                .kind = ExecutionEventKind::Finished,
                .jobId = request.jobId,
                .executorId = target.executorId,
                .targetId = target.id,
                .text = "process.execute was denied\n",
                .progress = 1.0,
                .exitCode = -1,
            };
            if (onEvent) {
                onEvent(event);
            }
            return {};
        }
        return base_.execution().start(request, target, std::move(onEvent));
    }

    ExecutionResult execute(const ExecutionRequest& request, const ExecutionTarget& target, ExecutionEventCallback onEvent = {}) const override {
        if (!permissions_.contains(Permission::ProcessExecute)) {
            return {
                .exitCode = -1,
                .output = "Missing process.execute permission\n",
                .jobId = request.jobId,
            };
        }
        if (approvals_.requestApproval({.subject = subject_, .permission = Permission::ProcessExecute, .action = "execute " + request.executable, .highRisk = true}) == ApprovalDecision::Deny) {
            return {
                .exitCode = -1,
                .output = "process.execute was denied\n",
                .jobId = request.jobId,
            };
        }
        return base_.execution().execute(request, target, std::move(onEvent));
    }

    void addType(std::unique_ptr<RunConfigurationType> type) override {
        state_->track(base_.runConfigurationRegistry().registerType(std::move(type)));
    }

    RegistrationHandle registerType(std::unique_ptr<RunConfigurationType> type) override {
        return base_.runConfigurationRegistry().registerType(std::move(type));
    }

    void addProducer(std::unique_ptr<RunConfigurationProducer> producer) override {
        state_->track(base_.runConfigurationRegistry().registerProducer(std::move(producer)));
    }

    RegistrationHandle registerProducer(std::unique_ptr<RunConfigurationProducer> producer) override {
        return base_.runConfigurationRegistry().registerProducer(std::move(producer));
    }

    void addConfiguration(RunConfiguration configuration) override {
        state_->track(base_.runConfigurationRegistry().registerConfiguration(std::move(configuration)));
    }

    RegistrationHandle registerConfiguration(RunConfiguration configuration) override {
        return base_.runConfigurationRegistry().registerConfiguration(std::move(configuration));
    }

    std::vector<RunConfiguration> configurations(bool includeTemporary) const override {
        return base_.runConfigurationRegistry().configurations(includeTemporary);
    }

    GitDiff diff() const override {
        if (!permissions_.contains(Permission::GitRead)) {
            return {
                .exitCode = -1,
                .text = "Missing git.read permission\n",
            };
        }
        return base_.git().diff();
    }

private:
    WorkspaceContext& base_;
    std::shared_ptr<PluginActivationState> state_;
    PermissionSet permissions_;
    ApprovalService& approvals_;
    std::string subject_;
};

}

void ConsoleLogger::info(const std::string& message) {
    std::cout << "[info] " << message << '\n';
}

void ConsoleLogger::warn(const std::string& message) {
    std::cout << "[warn] " << message << '\n';
}

void ConsoleLogger::error(const std::string& message) {
    std::cerr << "[error] " << message << '\n';
}

void PluginActivationState::track(RegistrationHandle registration) {
    if (registration.registered()) {
        registrations.push_back(std::move(registration));
    }
}

void PluginActivationState::clear() {
    registrations.clear();
}

std::size_t PluginActivationState::registrationCount() const {
    return registrations.size();
}

BasicExtensionContext::BasicExtensionContext(
    ExtensionInfo extension,
    std::shared_ptr<PluginActivationState> activationState,
    PermissionSet permissions,
    ApprovalService& approvals,
    Logger& logger,
    WorkspaceContext& workspace)
    : extension_(std::move(extension)),
      activationState_(std::move(activationState)),
      permissions_(std::move(permissions)),
      approvals_(approvals),
      logger_(logger),
      workspaceContext_(std::make_unique<ScopedWorkspaceContext>(workspace, activationState_, permissions_, approvals_, extension_.id)) {}

const ExtensionInfo& BasicExtensionContext::extension() const {
    return extension_;
}

const WorkspaceInfo& BasicExtensionContext::workspace() const {
    return workspaceContext_->info();
}

Logger& BasicExtensionContext::logger() {
    return logger_;
}

WorkspaceContext& BasicExtensionContext::workspaceContext() {
    return *workspaceContext_;
}

const PermissionSet& BasicExtensionContext::permissions() const {
    return permissions_;
}

void PluginProcessHostDeleter::operator()(PluginProcessHost* host) const {
    delete host;
}

std::string toString(PluginLifecycleState state) {
    switch (state) {
    case PluginLifecycleState::Discovered:
        return "discovered";
    case PluginLifecycleState::Activating:
        return "activating";
    case PluginLifecycleState::Active:
        return "active";
    case PluginLifecycleState::Deactivating:
        return "deactivating";
    case PluginLifecycleState::Inactive:
        return "inactive";
    case PluginLifecycleState::Failed:
        return "failed";
    }
    return "discovered";
}

PluginManager::~PluginManager() {
    deactivateAll();
}

std::vector<PluginManifest> PluginManager::scan(const std::filesystem::path& pluginsRoot) {
    pluginsRoot_ = pluginsRoot;
    manifests_.clear();
    contributions_.clear();
    lifecycle_.clear();
    if (!std::filesystem::exists(pluginsRoot)) {
        return manifests_;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(pluginsRoot)) {
        if (!entry.is_regular_file() || entry.path().filename() != "vanta.plugin.json") {
            continue;
        }
        if (auto manifest = loadManifest(entry.path())) {
            updateLifecycle(*manifest, PluginLifecycleState::Discovered, manifest->runtimeKind == "process");
            manifests_.push_back(std::move(*manifest));
        }
    }
    return manifests_;
}

std::optional<PluginManifest> PluginManager::loadManifest(const std::filesystem::path& manifestPath) const {
    Json root = Json::parse(readFile(manifestPath));
    if (!root.isObject()) {
        return std::nullopt;
    }

    PluginManifest manifest;
    manifest.extension.id = root.stringValue("id").value_or("");
    manifest.extension.name = root.stringValue("name").value_or(manifest.extension.id);
    manifest.extension.version = root.stringValue("version").value_or("0.0.0");
    manifest.extension.publisher = root.stringValue("publisher").value_or("");
    manifest.extension.location = manifestPath.parent_path();

    if (root.contains("runtime") && root["runtime"].isObject()) {
        const Json& runtime = root["runtime"];
        manifest.runtimeKind = runtime.stringValue("kind").value_or("");
        manifest.entry = runtime.stringValue("entry").value_or("");
    }

    manifest.permissions = parseStringArray(root, "permissions");
    manifest.activationEvents = parseStringArray(root, "activationEvents");

    if (manifest.extension.id.empty()) {
        return std::nullopt;
    }
    return manifest;
}

std::vector<std::string> PluginManager::activateCorePlugins(
    const CorePluginRegistry& registry,
    Logger& logger,
    WorkspaceContext& workspace,
    ApprovalService& approvals) {
    std::vector<std::string> messages;

    for (const PluginManifest& manifest : manifests_) {
        if (manifest.runtimeKind != "core") {
            continue;
        }

        std::unique_ptr<CoreExtension> extension = registry.create(manifest.entry);
        if (!extension) {
            updateLifecycle(manifest, PluginLifecycleState::Failed, false, "No core plugin registered for " + manifest.entry);
            messages.push_back("No core plugin registered for " + manifest.entry);
            continue;
        }

        updateLifecycle(manifest, PluginLifecycleState::Activating, false);
        auto activationState = std::make_shared<PluginActivationState>();
        PermissionSet permissions = PermissionSet::fromStrings(manifest.permissions);
        auto context = std::make_unique<BasicExtensionContext>(
            manifest.extension,
            activationState,
            permissions,
            approvals,
            logger,
            workspace);

        try {
            extension->activate(*context);
            activeSessions_.push_back({
                .manifest = manifest,
                .activationState = std::move(activationState),
                .extension = std::move(extension),
                .processHost = nullptr,
                .context = std::move(context),
                .unloadable = false,
                .state = PluginLifecycleState::Active,
            });
            updateLifecycle(manifest, PluginLifecycleState::Active, false, {}, activeSessions_.back().activationState->registrationCount());
            messages.push_back("Activated " + manifest.extension.id);
        } catch (const std::exception& exception) {
            updateLifecycle(manifest, PluginLifecycleState::Failed, false, exception.what());
            messages.push_back("Failed to activate " + manifest.extension.id + ": " + exception.what());
        }
    }

    return messages;
}

std::vector<std::string> PluginManager::reloadCorePlugins(
    const CorePluginRegistry& registry,
    Logger& logger,
    WorkspaceContext& workspace,
    ApprovalService& approvals) {
    deactivateAll();
    if (!pluginsRoot_.empty()) {
        scan(pluginsRoot_);
    }
    return activateCorePlugins(registry, logger, workspace, approvals);
}

std::vector<std::string> PluginManager::activateExternalPlugins(
    Logger& logger,
    WorkspaceContext& workspace,
    ApprovalService& approvals) {
    std::vector<std::string> messages;
    for (const PluginManifest& manifest : manifests_) {
        if (manifest.runtimeKind != "process") {
            continue;
        }
        messages.push_back(activateExternalPlugin(manifest, logger, workspace, approvals));
    }
    return messages;
}

bool PluginManager::unloadPlugin(const std::string& pluginId, std::string* message) {
    for (auto it = activeSessions_.begin(); it != activeSessions_.end(); ++it) {
        if (it->manifest.extension.id != pluginId) {
            continue;
        }
        if (!it->unloadable) {
            if (message != nullptr) {
                *message = "Built-in plugin cannot be unloaded: " + pluginId;
            }
            return false;
        }
        it->state = PluginLifecycleState::Deactivating;
        updateLifecycle(it->manifest, PluginLifecycleState::Deactivating, it->unloadable, {}, it->activationState ? it->activationState->registrationCount() : 0);
        if (it->processHost) {
            it->processHost->deactivate(pluginId);
            it->processHost->stop();
        }
        if (it->extension) {
            it->extension->deactivate();
        }
        if (it->activationState) {
            it->activationState->clear();
        }
        updateLifecycle(it->manifest, PluginLifecycleState::Inactive, it->unloadable);
        activeSessions_.erase(it);
        if (message != nullptr) {
            *message = "Unloaded " + pluginId;
        }
        return true;
    }
    if (message != nullptr) {
        *message = "Plugin is not active: " + pluginId;
    }
    return false;
}

std::vector<std::string> PluginManager::reloadPlugin(
    const std::string& pluginId,
    Logger& logger,
    WorkspaceContext& workspace,
    ApprovalService& approvals) {
    std::vector<std::string> messages;
    const PluginManifest* manifest = manifestById(pluginId);
    if (manifest == nullptr && !pluginsRoot_.empty()) {
        scan(pluginsRoot_);
        manifest = manifestById(pluginId);
    }
    if (manifest == nullptr) {
        messages.push_back("Plugin manifest not found: " + pluginId);
        return messages;
    }
    if (manifest->runtimeKind == "core") {
        messages.push_back("Built-in plugin cannot be reloaded individually: " + pluginId);
        return messages;
    }

    std::string unloadMessage;
    unloadPlugin(pluginId, &unloadMessage);
    if (!unloadMessage.empty()) {
        messages.push_back(unloadMessage);
    }
    messages.push_back(activateExternalPlugin(*manifest, logger, workspace, approvals));
    return messages;
}

void PluginManager::deactivateAll() {
    for (auto it = activeSessions_.rbegin(); it != activeSessions_.rend(); ++it) {
        it->state = PluginLifecycleState::Deactivating;
        updateLifecycle(it->manifest, PluginLifecycleState::Deactivating, it->unloadable, {}, it->activationState ? it->activationState->registrationCount() : 0);
        if (it->processHost) {
            it->processHost->deactivate(it->manifest.extension.id);
            it->processHost->stop();
        }
        if (it->extension) {
            it->extension->deactivate();
        }
        if (it->activationState) {
            it->activationState->clear();
        }
        updateLifecycle(it->manifest, PluginLifecycleState::Inactive, it->unloadable);
    }
    activeSessions_.clear();
}

const std::vector<PluginManifest>& PluginManager::manifests() const {
    return manifests_;
}

const ContributionRegistry& PluginManager::contributions() const {
    return contributions_;
}

std::vector<std::string> PluginManager::activePluginIds() const {
    std::vector<std::string> result;
    for (const ActivePluginSession& session : activeSessions_) {
        result.push_back(session.manifest.extension.id);
    }
    return result;
}

std::vector<PluginLifecycleInfo> PluginManager::pluginLifecycle() const {
    std::vector<PluginLifecycleInfo> result;
    for (const auto& [id, info] : lifecycle_) {
        (void)id;
        result.push_back(info);
    }
    return result;
}

std::string PluginManager::activateExternalPlugin(
    const PluginManifest& manifest,
    Logger& logger,
    WorkspaceContext& workspace,
    ApprovalService& approvals) {
    (void)logger;
    (void)approvals;
    if (manifest.runtimeKind != "process") {
        updateLifecycle(manifest, PluginLifecycleState::Failed, false, "Plugin is not an external process plugin");
        return "Plugin is not an external process plugin: " + manifest.extension.id;
    }
    for (const ActivePluginSession& session : activeSessions_) {
        if (session.manifest.extension.id == manifest.extension.id) {
            return "Plugin is already active: " + manifest.extension.id;
        }
    }

    updateLifecycle(manifest, PluginLifecycleState::Activating, true);
    std::unique_ptr<PluginProcessHost, PluginProcessHostDeleter> host(new PluginProcessHost());
    std::string error;
    if (!host->start(manifest, workspace.info().rootPath, &error)) {
        updateLifecycle(manifest, PluginLifecycleState::Failed, true, error);
        return "Failed to start " + manifest.extension.id + ": " + error;
    }
    const auto response = host->activate(manifest, workspace.info().rootPath);
    if (!response || !response->ok) {
        const std::string responseError = response ? response->error : "No activation response";
        host->stop();
        updateLifecycle(manifest, PluginLifecycleState::Failed, true, responseError);
        return "Failed to activate " + manifest.extension.id + ": " + responseError;
    }

    auto activationState = std::make_shared<PluginActivationState>();
    activeSessions_.push_back({
        .manifest = manifest,
        .activationState = std::move(activationState),
        .extension = nullptr,
        .processHost = std::move(host),
        .context = nullptr,
        .unloadable = true,
        .state = PluginLifecycleState::Active,
    });
    updateLifecycle(manifest, PluginLifecycleState::Active, true);
    return "Activated " + manifest.extension.id;
}

const PluginManifest* PluginManager::manifestById(const std::string& pluginId) const {
    for (const PluginManifest& manifest : manifests_) {
        if (manifest.extension.id == pluginId) {
            return &manifest;
        }
    }
    return nullptr;
}

void PluginManager::updateLifecycle(
    const PluginManifest& manifest,
    PluginLifecycleState state,
    bool unloadable,
    std::string error,
    std::size_t registrationCount) {
    PluginLifecycleInfo& info = lifecycle_[manifest.extension.id];
    info.id = manifest.extension.id;
    info.state = state;
    info.unloadable = unloadable;
    info.runtimeKind = manifest.runtimeKind;
    info.error = std::move(error);
    info.registrationCount = registrationCount;
}

}
