#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "vanta/platform/async.h"
#include "vanta/platform/json.h"
#include "vanta/platform/result.h"

namespace vanta {

enum class SettingScopeKind {
    Ide,
    Workspace,
    Project,
    Language,
};

struct SettingScope {
    SettingScopeKind kind = SettingScopeKind::Ide;
    std::string qualifier;

    bool operator<(const SettingScope& other) const noexcept;
    bool operator==(const SettingScope& other) const noexcept;
};

enum class SettingValueType {
    Bool,
    Int,
    Double,
    String,
    StringList,
    Path,
};

using SettingValueData = std::variant<bool, int, double, std::string, std::vector<std::string>>;

struct SettingValue {
    SettingValueType type = SettingValueType::String;
    SettingValueData data = std::string();

    static SettingValue boolValue(bool value);
    static SettingValue intValue(int value);
    static SettingValue doubleValue(double value);
    static SettingValue stringValue(std::string value);
    static SettingValue stringListValue(std::vector<std::string> value);
    static SettingValue pathValue(std::string value);
};

struct SettingNode {
    std::string id;
    std::string parentId;
    std::string ownerId;
    std::string title;
    std::string description;
    int order = 0;
};

struct SettingDefinition {
    std::string id;
    std::string ownerId;
    std::string nodeId;
    std::string title;
    std::string description;
    SettingValueType type = SettingValueType::String;
    SettingValue defaultValue = SettingValue::stringValue("");
    std::vector<SettingScopeKind> supportedScopes;
    std::vector<SettingScopeKind> resolutionOrder;
    std::vector<std::string> tags;
    std::vector<std::string> aliases;
    int order = 0;
};

struct SettingQuery {
    std::string workspaceId;
    std::string projectId;
    std::string languageId;
};

struct SettingResolution {
    SettingValue value;
    SettingScope source;
    bool defaulted = false;
};

struct SettingScopeDescriptor {
    SettingScope scope;
    std::string title;
    bool readable = true;
    bool writable = true;
    bool hasValue = false;
    bool effectiveSource = false;
};

struct SettingSearchResult {
    std::string settingId;
    std::string nodeId;
    std::vector<std::string> path;
    std::string title;
    std::string description;
    int score = 0;
    std::vector<std::string> matchedFields;
};

struct SettingChangeEvent {
    std::string id;
    SettingScope scope;
    std::optional<SettingValue> oldValue;
    std::optional<SettingValue> newValue;
    SettingResolution effectiveValue;
};

class SettingsStore {
public:
    std::optional<SettingValue> get(const std::string& id) const;
    void set(std::string id, SettingValue value);
    bool remove(const std::string& id);
    const std::map<std::string, SettingValue>& values() const;
    void clear();

    Result<void> load(const std::filesystem::path& path);
    Result<void> save(const std::filesystem::path& path) const;

private:
    std::map<std::string, SettingValue> values_;
};

class SettingsService {
public:
    void registerNode(SettingNode node);
    void registerSetting(SettingDefinition definition);

    SettingResolution resolve(const std::string& id, const SettingQuery& query = {}) const;
    std::optional<SettingValue> valueAt(const std::string& id, SettingScope scope) const;
    bool setValue(const std::string& id, SettingScope scope, SettingValue value, std::string* errorMessage = nullptr);
    bool resetValue(const std::string& id, SettingScope scope);

    std::vector<SettingNode> nodes() const;
    std::vector<SettingNode> children(const std::string& parentId) const;
    std::vector<SettingDefinition> settings(const std::string& nodeId) const;
    std::optional<SettingDefinition> definition(const std::string& id) const;

    std::vector<SettingScopeDescriptor> scopesFor(const std::string& id, const SettingQuery& query = {}) const;
    std::vector<SettingSearchResult> search(const std::string& query, const SettingQuery& settingQuery = {}) const;

    Result<void> load(SettingScope scope, const std::filesystem::path& path);
    Result<void> save(SettingScope scope, const std::filesystem::path& path) const;
    SettingsStore& store(SettingScope scope);
    const SettingsStore* store(SettingScope scope) const;

    std::uint64_t onDidChangeSetting(EventBus<SettingChangeEvent>::Listener listener);
    void removeSettingListener(std::uint64_t listenerId);

private:
    std::vector<SettingScopeKind> resolutionOrder(const SettingDefinition& definition) const;
    std::optional<SettingScope> scopeFor(SettingScopeKind kind, const SettingQuery& query) const;
    std::vector<std::string> nodePath(const std::string& nodeId) const;
    bool valueMatchesType(const SettingDefinition& definition, const SettingValue& value) const;
    void publish(SettingChangeEvent event);

    std::map<std::string, SettingNode> nodes_;
    std::map<std::string, SettingDefinition> definitions_;
    std::map<SettingScope, SettingsStore> stores_;
    EventBus<SettingChangeEvent> onDidChange_;
};

class PluginStorageService {
public:
    explicit PluginStorageService(std::filesystem::path root = {});

    void setRoot(std::filesystem::path root);
    Result<void> write(std::string pluginId, std::string key, Json value) const;
    Result<Json> read(const std::string& pluginId, const std::string& key) const;

private:
    std::filesystem::path pathFor(const std::string& pluginId, const std::string& key) const;

    std::filesystem::path root_;
};

void registerDefaultSettings(SettingsService& settings);
std::string toString(SettingScopeKind kind);
std::string toString(SettingValueType type);
Json toJson(const SettingValue& value);
std::optional<SettingValue> settingValueFromJson(const Json& json, SettingValueType type);
std::string settingValueToString(const SettingValue& value);

}
