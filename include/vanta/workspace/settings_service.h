#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "vanta/core/event.h"
#include "vanta/core/result.h"
#include "vanta/core/value.h"

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

    static SettingValue BoolValue(bool value);
    static SettingValue IntValue(int value);
    static SettingValue DoubleValue(double value);
    static SettingValue StringValue(std::string value);
    static SettingValue StringListValue(std::vector<std::string> value);
    static SettingValue PathValue(std::string value);
};

struct SettingNode {
    std::string id;
    std::string parent_id;
    std::string owner_id;
    std::string title;
    std::string description;
    int order = 0;
};

struct SettingDefinition {
    std::string id;
    std::string owner_id;
    std::string node_id;
    std::string title;
    std::string description;
    SettingValueType type = SettingValueType::String;
    SettingValue default_value = SettingValue::StringValue("");
    std::vector<SettingScopeKind> supported_scopes;
    std::vector<SettingScopeKind> resolution_order;
    std::vector<std::string> tags;
    std::vector<std::string> aliases;
    int order = 0;
};

struct SettingQuery {
    std::string workspace_id;
    std::string project_id;
    std::string language_id;
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
    bool has_value = false;
    bool effective_source = false;
};

struct SettingSearchResult {
    std::string setting_id;
    std::string node_id;
    std::vector<std::string> path;
    std::string title;
    std::string description;
    int score = 0;
    std::vector<std::string> matched_fields;
};

struct SettingChangeEvent {
    std::string id;
    SettingScope scope;
    std::optional<SettingValue> old_value;
    std::optional<SettingValue> new_value;
    SettingResolution effective_value;
};

class SettingsStore {
public:
    std::optional<SettingValue> Get(const std::string& id) const;
    void Set(std::string id, SettingValue value);
    bool Remove(const std::string& id);
    const std::map<std::string, SettingValue>& Values() const;
    void Clear();

    Result<void> Load(const std::filesystem::path& path);
    Result<void> Save(const std::filesystem::path& path) const;

private:
    std::map<std::string, SettingValue> values_;
};

class SettingsService {
public:
    static constexpr const char* kServiceId = "vanta.settings";

    void RegisterNode(SettingNode node);
    void RegisterSetting(SettingDefinition definition);

    SettingResolution Resolve(const std::string& id, const SettingQuery& query = {}) const;
    std::optional<SettingValue> ValueAt(const std::string& id, SettingScope scope) const;
    bool SetValue(const std::string& id, SettingScope scope, SettingValue value, std::string* error_message = nullptr);
    bool ResetValue(const std::string& id, SettingScope scope);

    std::vector<SettingNode> Nodes() const;
    std::vector<SettingNode> Children(const std::string& parent_id) const;
    std::vector<SettingDefinition> Settings(const std::string& node_id) const;
    std::optional<SettingDefinition> Definition(const std::string& id) const;

    std::vector<SettingScopeDescriptor> ScopesFor(const std::string& id, const SettingQuery& query = {}) const;
    std::vector<SettingSearchResult> Search(const std::string& query, const SettingQuery& setting_query = {}) const;

    Result<void> Load(SettingScope scope, const std::filesystem::path& path);
    Result<void> Save(SettingScope scope, const std::filesystem::path& path) const;
    SettingsStore& Store(SettingScope scope);
    const SettingsStore* Store(SettingScope scope) const;

    std::uint64_t OnDidChangeSetting(EventBus<SettingChangeEvent>::Listener listener);
    void RemoveSettingListener(std::uint64_t listener_id);

private:
    std::vector<SettingScopeKind> ResolutionOrder(const SettingDefinition& definition) const;
    std::optional<SettingScope> ScopeFor(SettingScopeKind kind, const SettingQuery& query) const;
    std::vector<std::string> NodePath(const std::string& node_id) const;
    bool ValueMatchesType(const SettingDefinition& definition, const SettingValue& value) const;
    void Publish(SettingChangeEvent event);

    std::map<std::string, SettingNode> nodes_;
    std::map<std::string, SettingDefinition> definitions_;
    std::map<SettingScope, SettingsStore> stores_;
    EventBus<SettingChangeEvent> on_did_change_;
};

void RegisterDefaultSettings(SettingsService& settings);
std::string ToString(SettingScopeKind kind);
std::string ToString(SettingValueType type);
std::optional<SettingValue> SettingValueFromValue(const Value& value, SettingValueType type);
std::string SettingValueToString(const SettingValue& value);

}
