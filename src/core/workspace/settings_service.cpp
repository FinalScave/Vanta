#include "vanta/workspace/settings_service.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <utility>
#include <variant>

#include "vanta/core/json_codec.h"

namespace vanta {
namespace {

std::string Lowercase(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

bool ContainsString(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

SettingValueType ValueTypeFromString(const std::string& type) {
    if (type == "bool") {
        return SettingValueType::Bool;
    }
    if (type == "int") {
        return SettingValueType::Int;
    }
    if (type == "double") {
        return SettingValueType::Double;
    }
    if (type == "stringList") {
        return SettingValueType::StringList;
    }
    if (type == "path") {
        return SettingValueType::Path;
    }
    return SettingValueType::String;
}

std::optional<SettingValue> SettingValueFromTypedJson(const Value& json) {
    if (json.IsObject() && json.Contains("type") && json["type"].IsString() && json.Contains("value")) {
        return SettingValueFromValue(json["value"], ValueTypeFromString(json["type"].AsString()));
    }
    if (json.IsBool()) {
        return SettingValue::BoolValue(json.AsBool());
    }
    if (json.IsInt()) {
        return SettingValue::IntValue(static_cast<int>(json.AsInt()));
    }
    if (json.IsDouble()) {
        return SettingValue::DoubleValue(json.AsDouble());
    }
    if (json.IsString()) {
        return SettingValue::StringValue(json.AsString());
    }
    if (json.IsArray()) {
        std::vector<std::string> values;
        for (const Value& item : json.AsArray()) {
            if (!item.IsString()) {
                return std::nullopt;
            }
            values.push_back(item.AsString());
        }
        return SettingValue::StringListValue(std::move(values));
    }
    return std::nullopt;
}

Value SettingValueProjection(const SettingValue& value) {
    switch (value.type) {
    case SettingValueType::Bool:
        return Value(std::get<bool>(value.data));
    case SettingValueType::Int:
        return Value(static_cast<std::int64_t>(std::get<int>(value.data)));
    case SettingValueType::Double:
        return Value(std::get<double>(value.data));
    case SettingValueType::String:
    case SettingValueType::Path:
        return Value(std::get<std::string>(value.data));
    case SettingValueType::StringList: {
        Value::Array values;
        for (const std::string& item : std::get<std::vector<std::string>>(value.data)) {
            values.push_back(Value(item));
        }
        return Value::ArrayValue(std::move(values));
    }
    }
    return Value();
}

Value TypedSettingValueProjection(const SettingValue& value) {
    return Value::ObjectValue({
        {"type", Value(ToString(value.type))},
        {"value", SettingValueProjection(value)},
    });
}

std::string ScopeTitle(SettingScopeKind kind, const std::string& qualifier) {
    switch (kind) {
    case SettingScopeKind::Ide:
        return "IDE";
    case SettingScopeKind::Workspace:
        return qualifier.empty() ? "Workspace" : "Workspace";
    case SettingScopeKind::Project:
        return qualifier.empty() ? "Project" : "Current Project";
    case SettingScopeKind::Language:
        return qualifier.empty() ? "Language" : qualifier;
    }
    return "";
}

int MatchScore(const std::string& query, const std::string& field) {
    if (query.empty() || field.empty()) {
        return 0;
    }
    const std::string value = Lowercase(field);
    if (value == query) {
        return 120;
    }
    if (value.rfind(query, 0) == 0) {
        return 80;
    }
    if (value.find(query) != std::string::npos) {
        return 40;
    }
    return 0;
}

void AddMatch(std::vector<std::string>& fields, const std::string& field) {
    if (!ContainsString(fields, field)) {
        fields.push_back(field);
    }
}

std::vector<SettingScopeKind> DefaultResolutionOrder(const std::vector<SettingScopeKind>& supported_scopes) {
    const std::vector<SettingScopeKind> priority = {
        SettingScopeKind::Language,
        SettingScopeKind::Project,
        SettingScopeKind::Workspace,
        SettingScopeKind::Ide,
    };
    std::vector<SettingScopeKind> values;
    for (SettingScopeKind kind : priority) {
        if (std::find(supported_scopes.begin(), supported_scopes.end(), kind) != supported_scopes.end()) {
            values.push_back(kind);
        }
    }
    return values.empty() ? std::vector<SettingScopeKind>{SettingScopeKind::Ide} : values;
}

std::vector<SettingScopeKind> DefaultSupportedScopes() {
    return {SettingScopeKind::Ide};
}

SettingQuery QueryFromScope(SettingScope scope) {
    SettingQuery query;
    switch (scope.kind) {
    case SettingScopeKind::Ide:
        break;
    case SettingScopeKind::Workspace:
        query.workspace_id = scope.qualifier;
        break;
    case SettingScopeKind::Project:
        query.project_id = scope.qualifier;
        break;
    case SettingScopeKind::Language:
        query.language_id = scope.qualifier;
        break;
    }
    return query;
}

}

bool SettingScope::operator<(const SettingScope& other) const noexcept {
    if (kind != other.kind) {
        return static_cast<int>(kind) < static_cast<int>(other.kind);
    }
    return qualifier < other.qualifier;
}

bool SettingScope::operator==(const SettingScope& other) const noexcept {
    return kind == other.kind && qualifier == other.qualifier;
}

SettingValue SettingValue::BoolValue(bool value) {
    return {.type = SettingValueType::Bool, .data = value};
}

SettingValue SettingValue::IntValue(int value) {
    return {.type = SettingValueType::Int, .data = value};
}

SettingValue SettingValue::DoubleValue(double value) {
    return {.type = SettingValueType::Double, .data = value};
}

SettingValue SettingValue::StringValue(std::string value) {
    return {.type = SettingValueType::String, .data = std::move(value)};
}

SettingValue SettingValue::StringListValue(std::vector<std::string> value) {
    return {.type = SettingValueType::StringList, .data = std::move(value)};
}

SettingValue SettingValue::PathValue(std::string value) {
    return {.type = SettingValueType::Path, .data = std::move(value)};
}

std::optional<SettingValue> SettingsStore::Get(const std::string& id) const {
    auto it = values_.find(id);
    return it == values_.end() ? std::nullopt : std::optional<SettingValue>(it->second);
}

void SettingsStore::Set(std::string id, SettingValue value) {
    values_[std::move(id)] = std::move(value);
}

bool SettingsStore::Remove(const std::string& id) {
    return values_.erase(id) > 0;
}

const std::map<std::string, SettingValue>& SettingsStore::Values() const {
    return values_;
}

void SettingsStore::Clear() {
    values_.clear();
}

Result<void> SettingsStore::Load(const std::filesystem::path& path) {
    values_.clear();
    if (!std::filesystem::exists(path)) {
        return Result<void>::Success();
    }

    std::ifstream input(path);
    if (!input) {
        return Result<void>::Failure("settings.read", "Failed to read settings file");
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    Result<Value> parsed = ValueFromJsonText(stream.str());
    if (!parsed) {
        return Result<void>::Failure("settings.parse", parsed.ErrorValue().message);
    }
    const Value& root = parsed.Value();
    if (!root.IsObject()) {
        return Result<void>::Failure("settings.format", "Settings root must be an object");
    }

    for (const auto& [key, value] : root.AsObject()) {
        if (auto parsed = SettingValueFromTypedJson(value)) {
            values_[key] = std::move(*parsed);
        }
    }
    return Result<void>::Success();
}

Result<void> SettingsStore::Save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return Result<void>::Failure("settings.directory", "Failed to create settings directory");
    }

    Value::Object values;
    for (const auto& [key, value] : values_) {
        values[key] = TypedSettingValueProjection(value);
    }

    std::ofstream output(path);
    if (!output) {
        return Result<void>::Failure("settings.write", "Failed to write settings file");
    }
    output << ValueToJsonText(Value::ObjectValue(std::move(values)));
    return Result<void>::Success();
}

void SettingsService::RegisterNode(SettingNode node) {
    if (node.id.empty()) {
        return;
    }
    nodes_[node.id] = std::move(node);
}

void SettingsService::RegisterSetting(SettingDefinition definition) {
    if (definition.id.empty()) {
        return;
    }
    if (definition.supported_scopes.empty()) {
        definition.supported_scopes = DefaultSupportedScopes();
    }
    if (definition.resolution_order.empty()) {
        definition.resolution_order = DefaultResolutionOrder(definition.supported_scopes);
    }
    definitions_[definition.id] = std::move(definition);
}

SettingResolution SettingsService::Resolve(const std::string& id, const SettingQuery& query) const {
    auto definition_value = Definition(id);
    if (!definition_value) {
        return {
            .value = SettingValue::StringValue(""),
            .source = {},
            .defaulted = true,
        };
    }

    const SettingDefinition& setting = *definition_value;
    for (SettingScopeKind kind : ResolutionOrder(setting)) {
        auto scope = ScopeFor(kind, query);
        if (!scope) {
            continue;
        }
        if (const SettingsStore* found = Store(*scope)) {
            if (auto value = found->Get(id)) {
                return {
                    .value = *value,
                    .source = *scope,
                    .defaulted = false,
                };
            }
        }
    }
    return {
        .value = setting.default_value,
        .source = {},
        .defaulted = true,
    };
}

std::optional<SettingValue> SettingsService::ValueAt(const std::string& id, SettingScope scope) const {
    const SettingsStore* found = Store(std::move(scope));
    return found == nullptr ? std::nullopt : found->Get(id);
}

bool SettingsService::SetValue(const std::string& id, SettingScope scope, SettingValue value, std::string* error_message) {
    auto definition_value = Definition(id);
    if (!definition_value) {
        if (error_message != nullptr) {
            *error_message = "Setting is not registered";
        }
        return false;
    }
    const SettingDefinition& setting = *definition_value;
    if (std::find(setting.supported_scopes.begin(), setting.supported_scopes.end(), scope.kind) == setting.supported_scopes.end()) {
        if (error_message != nullptr) {
            *error_message = "Setting does not support this scope";
        }
        return false;
    }
    if (!ValueMatchesType(setting, value)) {
        if (error_message != nullptr) {
            *error_message = "Setting value type does not match definition";
        }
        return false;
    }

    SettingsStore& target = Store(scope);
    std::optional<SettingValue> old_value = target.Get(id);
    target.Set(id, std::move(value));
    Publish({
        .id = id,
        .scope = scope,
        .old_value = std::move(old_value),
        .new_value = target.Get(id),
        .effective_value = Resolve(id, QueryFromScope(scope)),
    });
    return true;
}

bool SettingsService::ResetValue(const std::string& id, SettingScope scope) {
    SettingsStore& target = Store(scope);
    std::optional<SettingValue> old_value = target.Get(id);
    if (!target.Remove(id)) {
        return false;
    }
    Publish({
        .id = id,
        .scope = scope,
        .old_value = std::move(old_value),
        .new_value = std::nullopt,
        .effective_value = Resolve(id, QueryFromScope(scope)),
    });
    return true;
}

std::vector<SettingNode> SettingsService::Nodes() const {
    std::vector<SettingNode> values;
    for (const auto& [id, node] : nodes_) {
        (void)id;
        values.push_back(node);
    }
    std::sort(values.begin(), values.end(), [](const SettingNode& left, const SettingNode& right) {
        if (left.order != right.order) {
            return left.order < right.order;
        }
        return left.id < right.id;
    });
    return values;
}

std::vector<SettingNode> SettingsService::Children(const std::string& parent_id) const {
    std::vector<SettingNode> values;
    for (const auto& [id, node] : nodes_) {
        (void)id;
        if (node.parent_id == parent_id) {
            values.push_back(node);
        }
    }
    std::sort(values.begin(), values.end(), [](const SettingNode& left, const SettingNode& right) {
        if (left.order != right.order) {
            return left.order < right.order;
        }
        return left.id < right.id;
    });
    return values;
}

std::vector<SettingDefinition> SettingsService::Settings(const std::string& node_id) const {
    std::vector<SettingDefinition> values;
    for (const auto& [id, definition] : definitions_) {
        (void)id;
        if (definition.node_id == node_id) {
            values.push_back(definition);
        }
    }
    std::sort(values.begin(), values.end(), [](const SettingDefinition& left, const SettingDefinition& right) {
        if (left.order != right.order) {
            return left.order < right.order;
        }
        return left.id < right.id;
    });
    return values;
}

std::optional<SettingDefinition> SettingsService::Definition(const std::string& id) const {
    auto it = definitions_.find(id);
    return it == definitions_.end() ? std::nullopt : std::optional<SettingDefinition>(it->second);
}

std::vector<SettingScopeDescriptor> SettingsService::ScopesFor(const std::string& id, const SettingQuery& query) const {
    auto definition_value = Definition(id);
    if (!definition_value) {
        return {};
    }
    const SettingResolution current = Resolve(id, query);
    std::vector<SettingScopeDescriptor> values;
    for (SettingScopeKind kind : definition_value->supported_scopes) {
        auto scope = ScopeFor(kind, query).value_or(SettingScope{.kind = kind});
        values.push_back({
            .scope = scope,
            .title = ScopeTitle(kind, scope.qualifier),
            .readable = true,
            .writable = true,
            .has_value = ValueAt(id, scope).has_value(),
            .effective_source = !current.defaulted && current.source == scope,
        });
    }
    return values;
}

std::vector<SettingSearchResult> SettingsService::Search(const std::string& query, const SettingQuery&) const {
    const std::string needle = Lowercase(query);
    std::vector<SettingSearchResult> results;
    for (const auto& [id, definition] : definitions_) {
        int score = 0;
        std::vector<std::string> matched_fields;
        auto score_field = [&](const std::string& name, const std::string& field, int weight = 1) {
            const int field_score = MatchScore(needle, field);
            if (field_score > 0) {
                score += field_score * weight;
                AddMatch(matched_fields, name);
            }
        };

        score_field("id", definition.id, 2);
        score_field("title", definition.title, 3);
        score_field("description", definition.description);
        score_field("owner", definition.owner_id);
        for (const std::string& tag : definition.tags) {
            score_field("tag", tag, 2);
        }
        for (const std::string& alias : definition.aliases) {
            score_field("alias", alias, 2);
        }
        const std::vector<std::string> path = NodePath(definition.node_id);
        for (const std::string& path_item : path) {
            score_field("path", path_item, 2);
        }

        if (needle.empty()) {
            score = 1;
        }
        if (score == 0) {
            continue;
        }
        results.push_back({
            .setting_id = id,
            .node_id = definition.node_id,
            .path = path,
            .title = definition.title,
            .description = definition.description,
            .score = score,
            .matched_fields = std::move(matched_fields),
        });
    }
    std::sort(results.begin(), results.end(), [](const SettingSearchResult& left, const SettingSearchResult& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.setting_id < right.setting_id;
    });
    return results;
}

Result<void> SettingsService::Load(SettingScope scope, const std::filesystem::path& path) {
    return Store(scope).Load(path);
}

Result<void> SettingsService::Save(SettingScope scope, const std::filesystem::path& path) const {
    const SettingsStore* found = Store(scope);
    if (found == nullptr) {
        return Result<void>::Success();
    }
    return found->Save(path);
}

SettingsStore& SettingsService::Store(SettingScope scope) {
    return stores_[std::move(scope)];
}

const SettingsStore* SettingsService::Store(SettingScope scope) const {
    auto it = stores_.find(scope);
    return it == stores_.end() ? nullptr : &it->second;
}

std::uint64_t SettingsService::OnDidChangeSetting(EventBus<SettingChangeEvent>::Listener listener) {
    return on_did_change_.Subscribe(std::move(listener));
}

void SettingsService::RemoveSettingListener(std::uint64_t listener_id) {
    on_did_change_.Unsubscribe(listener_id);
}

std::vector<SettingScopeKind> SettingsService::ResolutionOrder(const SettingDefinition& definition) const {
    return definition.resolution_order.empty() ? DefaultResolutionOrder(definition.supported_scopes) : definition.resolution_order;
}

std::optional<SettingScope> SettingsService::ScopeFor(SettingScopeKind kind, const SettingQuery& query) const {
    switch (kind) {
    case SettingScopeKind::Ide:
        return SettingScope{.kind = SettingScopeKind::Ide};
    case SettingScopeKind::Workspace:
        return SettingScope{.kind = SettingScopeKind::Workspace, .qualifier = query.workspace_id};
    case SettingScopeKind::Project:
        if (query.project_id.empty()) {
            return std::nullopt;
        }
        return SettingScope{.kind = SettingScopeKind::Project, .qualifier = query.project_id};
    case SettingScopeKind::Language:
        if (query.language_id.empty()) {
            return std::nullopt;
        }
        return SettingScope{.kind = SettingScopeKind::Language, .qualifier = query.language_id};
    }
    return std::nullopt;
}

std::vector<std::string> SettingsService::NodePath(const std::string& node_id) const {
    std::vector<std::string> reversed;
    std::string current = node_id;
    while (!current.empty()) {
        auto it = nodes_.find(current);
        if (it == nodes_.end()) {
            break;
        }
        reversed.push_back(it->second.title);
        current = it->second.parent_id;
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

bool SettingsService::ValueMatchesType(const SettingDefinition& definition, const SettingValue& value) const {
    return definition.type == value.type;
}

void SettingsService::Publish(SettingChangeEvent event) {
    on_did_change_.Publish(std::move(event));
}

void RegisterDefaultSettings(SettingsService& settings) {
    settings.RegisterNode({.id = "editor", .owner_id = "vanta.core", .title = "Editor", .order = 10});
    settings.RegisterNode({.id = "editor.behavior", .parent_id = "editor", .owner_id = "vanta.core", .title = "Behavior", .order = 10});
    settings.RegisterNode({.id = "editor.font", .parent_id = "editor", .owner_id = "vanta.core", .title = "Font", .order = 20});
    settings.RegisterNode({.id = "ai", .owner_id = "vanta.core", .title = "AI", .order = 20});
    settings.RegisterNode({.id = "ai.agent", .parent_id = "ai", .owner_id = "vanta.core", .title = "Agent", .order = 10});
    settings.RegisterNode({.id = "ai.inlineCompletion", .parent_id = "ai", .owner_id = "vanta.core", .title = "Inline Completion", .order = 20});
    settings.RegisterNode({.id = "build", .owner_id = "vanta.core", .title = "Build", .order = 30});
    settings.RegisterNode({.id = "execution", .owner_id = "vanta.core", .title = "Execution", .order = 40});
    settings.RegisterNode({.id = "index", .owner_id = "vanta.core", .title = "Indexing", .order = 50});

    settings.RegisterSetting({
        .id = "editor.fontSize",
        .owner_id = "vanta.core",
        .node_id = "editor.font",
        .title = "Font Size",
        .description = "Default editor font size.",
        .type = SettingValueType::Int,
        .default_value = SettingValue::IntValue(14),
        .supported_scopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace},
        .resolution_order = {SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"font", "editor"},
        .order = 10,
    });
    settings.RegisterSetting({
        .id = "editor.formatOnSave",
        .owner_id = "vanta.core",
        .node_id = "editor.behavior",
        .title = "Format On Save",
        .description = "Format documents when they are saved.",
        .type = SettingValueType::Bool,
        .default_value = SettingValue::BoolValue(false),
        .supported_scopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace, SettingScopeKind::Language},
        .resolution_order = {SettingScopeKind::Language, SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"format", "save", "language"},
        .order = 10,
    });
    settings.RegisterSetting({
        .id = "ai.agent.model",
        .owner_id = "vanta.core",
        .node_id = "ai.agent",
        .title = "Agent Model",
        .description = "Model used for agent coding tasks.",
        .type = SettingValueType::String,
        .default_value = SettingValue::StringValue("default"),
        .supported_scopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace, SettingScopeKind::Project},
        .resolution_order = {SettingScopeKind::Project, SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"ai", "agent", "model"},
        .order = 10,
    });
    settings.RegisterSetting({
        .id = "ai.inlineCompletion.enabled",
        .owner_id = "vanta.core",
        .node_id = "ai.inlineCompletion",
        .title = "Inline Completion",
        .description = "Enable inline completion suggestions.",
        .type = SettingValueType::Bool,
        .default_value = SettingValue::BoolValue(true),
        .supported_scopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace, SettingScopeKind::Language},
        .resolution_order = {SettingScopeKind::Language, SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"ai", "completion", "inline"},
        .order = 10,
    });
    settings.RegisterSetting({
        .id = "execution.defaultTarget",
        .owner_id = "vanta.core",
        .node_id = "execution",
        .title = "Default Target",
        .description = "Default execution target id.",
        .type = SettingValueType::String,
        .default_value = SettingValue::StringValue("local.default"),
        .supported_scopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace, SettingScopeKind::Project},
        .resolution_order = {SettingScopeKind::Project, SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"run", "execution", "target"},
        .order = 10,
    });
    settings.RegisterSetting({
        .id = "index.autoRefresh",
        .owner_id = "vanta.core",
        .node_id = "index",
        .title = "Auto Refresh Index",
        .description = "Refresh indexes when workspace files change.",
        .type = SettingValueType::Bool,
        .default_value = SettingValue::BoolValue(true),
        .supported_scopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace},
        .resolution_order = {SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"index", "refresh"},
        .order = 10,
    });
}

std::string ToString(SettingScopeKind kind) {
    switch (kind) {
    case SettingScopeKind::Ide:
        return "ide";
    case SettingScopeKind::Workspace:
        return "workspace";
    case SettingScopeKind::Project:
        return "project";
    case SettingScopeKind::Language:
        return "language";
    }
    return "ide";
}

std::string ToString(SettingValueType type) {
    switch (type) {
    case SettingValueType::Bool:
        return "bool";
    case SettingValueType::Int:
        return "int";
    case SettingValueType::Double:
        return "double";
    case SettingValueType::String:
        return "string";
    case SettingValueType::StringList:
        return "stringList";
    case SettingValueType::Path:
        return "path";
    }
    return "string";
}

std::optional<SettingValue> SettingValueFromValue(const Value& json, SettingValueType type) {
    switch (type) {
    case SettingValueType::Bool:
        return json.IsBool() ? std::optional<SettingValue>(SettingValue::BoolValue(json.AsBool())) : std::nullopt;
    case SettingValueType::Int:
        return json.IsInt() ? std::optional<SettingValue>(SettingValue::IntValue(static_cast<int>(json.AsInt()))) : std::nullopt;
    case SettingValueType::Double:
        return json.IsNumber() ? std::optional<SettingValue>(SettingValue::DoubleValue(json.IsDouble() ? json.AsDouble() : static_cast<double>(json.AsInt()))) : std::nullopt;
    case SettingValueType::String:
        return json.IsString() ? std::optional<SettingValue>(SettingValue::StringValue(json.AsString())) : std::nullopt;
    case SettingValueType::Path:
        return json.IsString() ? std::optional<SettingValue>(SettingValue::PathValue(json.AsString())) : std::nullopt;
    case SettingValueType::StringList: {
        if (!json.IsArray()) {
            return std::nullopt;
        }
        std::vector<std::string> values;
        for (const Value& item : json.AsArray()) {
            if (!item.IsString()) {
                return std::nullopt;
            }
            values.push_back(item.AsString());
        }
        return SettingValue::StringListValue(std::move(values));
    }
    }
    return std::nullopt;
}

std::string SettingValueToString(const SettingValue& value) {
    switch (value.type) {
    case SettingValueType::Bool:
        return std::get<bool>(value.data) ? "true" : "false";
    case SettingValueType::Int:
        return std::to_string(std::get<int>(value.data));
    case SettingValueType::Double:
        return std::to_string(std::get<double>(value.data));
    case SettingValueType::String:
    case SettingValueType::Path:
        return std::get<std::string>(value.data);
    case SettingValueType::StringList: {
        std::string result;
        for (const std::string& item : std::get<std::vector<std::string>>(value.data)) {
            if (!result.empty()) {
                result += ", ";
            }
            result += item;
        }
        return result;
    }
    }
    return "";
}

}
