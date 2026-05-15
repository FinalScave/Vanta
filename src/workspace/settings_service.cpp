#include "vanta/workspace/settings_service.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <utility>

namespace vanta {
namespace {

std::string lowercase(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

bool containsString(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

SettingValueType valueTypeFromString(const std::string& type) {
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

std::optional<SettingValue> settingValueFromTypedJson(const Json& json) {
    if (json.isObject() && json.contains("type") && json["type"].isString() && json.contains("value")) {
        return settingValueFromJson(json["value"], valueTypeFromString(json["type"].asString()));
    }
    if (json.isBool()) {
        return SettingValue::boolValue(json.asBool());
    }
    if (json.isInt()) {
        return SettingValue::intValue(static_cast<int>(json.asInt()));
    }
    if (json.isDouble()) {
        return SettingValue::doubleValue(json.asDouble());
    }
    if (json.isString()) {
        return SettingValue::stringValue(json.asString());
    }
    if (json.isArray()) {
        std::vector<std::string> values;
        for (const Json& item : json.asArray()) {
            if (!item.isString()) {
                return std::nullopt;
            }
            values.push_back(item.asString());
        }
        return SettingValue::stringListValue(std::move(values));
    }
    return std::nullopt;
}

Json typedSettingValueToJson(const SettingValue& value) {
    return Json::object({
        {"type", Json(toString(value.type))},
        {"value", toJson(value)},
    });
}

std::string scopeTitle(SettingScopeKind kind, const std::string& qualifier) {
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

int matchScore(const std::string& query, const std::string& field) {
    if (query.empty() || field.empty()) {
        return 0;
    }
    const std::string value = lowercase(field);
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

void addMatch(std::vector<std::string>& fields, const std::string& field) {
    if (!containsString(fields, field)) {
        fields.push_back(field);
    }
}

std::vector<SettingScopeKind> defaultResolutionOrder(const std::vector<SettingScopeKind>& supportedScopes) {
    const std::vector<SettingScopeKind> priority = {
        SettingScopeKind::Language,
        SettingScopeKind::Project,
        SettingScopeKind::Workspace,
        SettingScopeKind::Ide,
    };
    std::vector<SettingScopeKind> values;
    for (SettingScopeKind kind : priority) {
        if (std::find(supportedScopes.begin(), supportedScopes.end(), kind) != supportedScopes.end()) {
            values.push_back(kind);
        }
    }
    return values.empty() ? std::vector<SettingScopeKind>{SettingScopeKind::Ide} : values;
}

std::vector<SettingScopeKind> defaultSupportedScopes() {
    return {SettingScopeKind::Ide};
}

SettingQuery queryFromScope(SettingScope scope) {
    SettingQuery query;
    switch (scope.kind) {
    case SettingScopeKind::Ide:
        break;
    case SettingScopeKind::Workspace:
        query.workspaceId = scope.qualifier;
        break;
    case SettingScopeKind::Project:
        query.projectId = scope.qualifier;
        break;
    case SettingScopeKind::Language:
        query.languageId = scope.qualifier;
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

SettingValue SettingValue::boolValue(bool value) {
    return {.type = SettingValueType::Bool, .data = value};
}

SettingValue SettingValue::intValue(int value) {
    return {.type = SettingValueType::Int, .data = value};
}

SettingValue SettingValue::doubleValue(double value) {
    return {.type = SettingValueType::Double, .data = value};
}

SettingValue SettingValue::stringValue(std::string value) {
    return {.type = SettingValueType::String, .data = std::move(value)};
}

SettingValue SettingValue::stringListValue(std::vector<std::string> value) {
    return {.type = SettingValueType::StringList, .data = std::move(value)};
}

SettingValue SettingValue::pathValue(std::string value) {
    return {.type = SettingValueType::Path, .data = std::move(value)};
}

std::optional<SettingValue> SettingsStore::get(const std::string& id) const {
    auto it = values_.find(id);
    return it == values_.end() ? std::nullopt : std::optional<SettingValue>(it->second);
}

void SettingsStore::set(std::string id, SettingValue value) {
    values_[std::move(id)] = std::move(value);
}

bool SettingsStore::remove(const std::string& id) {
    return values_.erase(id) > 0;
}

const std::map<std::string, SettingValue>& SettingsStore::values() const {
    return values_;
}

void SettingsStore::clear() {
    values_.clear();
}

Result<void> SettingsStore::load(const std::filesystem::path& path) {
    values_.clear();
    if (!std::filesystem::exists(path)) {
        return Result<void>::success();
    }

    std::ifstream input(path);
    if (!input) {
        return Result<void>::failure("settings.read", "Failed to read settings file");
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    Json root = Json::parse(stream.str());
    if (!root.isObject()) {
        return Result<void>::failure("settings.format", "Settings root must be an object");
    }

    for (const auto& [key, value] : root.asObject()) {
        if (auto parsed = settingValueFromTypedJson(value)) {
            values_[key] = std::move(*parsed);
        }
    }
    return Result<void>::success();
}

Result<void> SettingsStore::save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return Result<void>::failure("settings.directory", "Failed to create settings directory");
    }

    Json::Object values;
    for (const auto& [key, value] : values_) {
        values[key] = typedSettingValueToJson(value);
    }

    std::ofstream output(path);
    if (!output) {
        return Result<void>::failure("settings.write", "Failed to write settings file");
    }
    output << Json::object(std::move(values)).dump();
    return Result<void>::success();
}

void SettingsService::registerNode(SettingNode node) {
    if (node.id.empty()) {
        return;
    }
    nodes_[node.id] = std::move(node);
}

void SettingsService::registerSetting(SettingDefinition definition) {
    if (definition.id.empty()) {
        return;
    }
    if (definition.supportedScopes.empty()) {
        definition.supportedScopes = defaultSupportedScopes();
    }
    if (definition.resolutionOrder.empty()) {
        definition.resolutionOrder = defaultResolutionOrder(definition.supportedScopes);
    }
    definitions_[definition.id] = std::move(definition);
}

SettingResolution SettingsService::resolve(const std::string& id, const SettingQuery& query) const {
    auto definitionValue = definition(id);
    if (!definitionValue) {
        return {
            .value = SettingValue::stringValue(""),
            .source = {},
            .defaulted = true,
        };
    }

    const SettingDefinition& setting = *definitionValue;
    for (SettingScopeKind kind : resolutionOrder(setting)) {
        auto scope = scopeFor(kind, query);
        if (!scope) {
            continue;
        }
        if (const SettingsStore* found = store(*scope)) {
            if (auto value = found->get(id)) {
                return {
                    .value = *value,
                    .source = *scope,
                    .defaulted = false,
                };
            }
        }
    }
    return {
        .value = setting.defaultValue,
        .source = {},
        .defaulted = true,
    };
}

std::optional<SettingValue> SettingsService::valueAt(const std::string& id, SettingScope scope) const {
    const SettingsStore* found = store(std::move(scope));
    return found == nullptr ? std::nullopt : found->get(id);
}

bool SettingsService::setValue(const std::string& id, SettingScope scope, SettingValue value, std::string* errorMessage) {
    auto definitionValue = definition(id);
    if (!definitionValue) {
        if (errorMessage != nullptr) {
            *errorMessage = "Setting is not registered";
        }
        return false;
    }
    const SettingDefinition& setting = *definitionValue;
    if (std::find(setting.supportedScopes.begin(), setting.supportedScopes.end(), scope.kind) == setting.supportedScopes.end()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Setting does not support this scope";
        }
        return false;
    }
    if (!valueMatchesType(setting, value)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Setting value type does not match definition";
        }
        return false;
    }

    SettingsStore& target = store(scope);
    std::optional<SettingValue> oldValue = target.get(id);
    target.set(id, std::move(value));
    publish({
        .id = id,
        .scope = scope,
        .oldValue = std::move(oldValue),
        .newValue = target.get(id),
        .effectiveValue = resolve(id, queryFromScope(scope)),
    });
    return true;
}

bool SettingsService::resetValue(const std::string& id, SettingScope scope) {
    SettingsStore& target = store(scope);
    std::optional<SettingValue> oldValue = target.get(id);
    if (!target.remove(id)) {
        return false;
    }
    publish({
        .id = id,
        .scope = scope,
        .oldValue = std::move(oldValue),
        .newValue = std::nullopt,
        .effectiveValue = resolve(id, queryFromScope(scope)),
    });
    return true;
}

std::vector<SettingNode> SettingsService::nodes() const {
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

std::vector<SettingNode> SettingsService::children(const std::string& parentId) const {
    std::vector<SettingNode> values;
    for (const auto& [id, node] : nodes_) {
        (void)id;
        if (node.parentId == parentId) {
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

std::vector<SettingDefinition> SettingsService::settings(const std::string& nodeId) const {
    std::vector<SettingDefinition> values;
    for (const auto& [id, definition] : definitions_) {
        (void)id;
        if (definition.nodeId == nodeId) {
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

std::optional<SettingDefinition> SettingsService::definition(const std::string& id) const {
    auto it = definitions_.find(id);
    return it == definitions_.end() ? std::nullopt : std::optional<SettingDefinition>(it->second);
}

std::vector<SettingScopeDescriptor> SettingsService::scopesFor(const std::string& id, const SettingQuery& query) const {
    auto definitionValue = definition(id);
    if (!definitionValue) {
        return {};
    }
    const SettingResolution current = resolve(id, query);
    std::vector<SettingScopeDescriptor> values;
    for (SettingScopeKind kind : definitionValue->supportedScopes) {
        auto scope = scopeFor(kind, query).value_or(SettingScope{.kind = kind});
        values.push_back({
            .scope = scope,
            .title = scopeTitle(kind, scope.qualifier),
            .readable = true,
            .writable = true,
            .hasValue = valueAt(id, scope).has_value(),
            .effectiveSource = !current.defaulted && current.source == scope,
        });
    }
    return values;
}

std::vector<SettingSearchResult> SettingsService::search(const std::string& query, const SettingQuery&) const {
    const std::string needle = lowercase(query);
    std::vector<SettingSearchResult> results;
    for (const auto& [id, definition] : definitions_) {
        int score = 0;
        std::vector<std::string> matchedFields;
        auto scoreField = [&](const std::string& name, const std::string& field, int weight = 1) {
            const int fieldScore = matchScore(needle, field);
            if (fieldScore > 0) {
                score += fieldScore * weight;
                addMatch(matchedFields, name);
            }
        };

        scoreField("id", definition.id, 2);
        scoreField("title", definition.title, 3);
        scoreField("description", definition.description);
        scoreField("owner", definition.ownerId);
        for (const std::string& tag : definition.tags) {
            scoreField("tag", tag, 2);
        }
        for (const std::string& alias : definition.aliases) {
            scoreField("alias", alias, 2);
        }
        const std::vector<std::string> path = nodePath(definition.nodeId);
        for (const std::string& pathItem : path) {
            scoreField("path", pathItem, 2);
        }

        if (needle.empty()) {
            score = 1;
        }
        if (score == 0) {
            continue;
        }
        results.push_back({
            .settingId = id,
            .nodeId = definition.nodeId,
            .path = path,
            .title = definition.title,
            .description = definition.description,
            .score = score,
            .matchedFields = std::move(matchedFields),
        });
    }
    std::sort(results.begin(), results.end(), [](const SettingSearchResult& left, const SettingSearchResult& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.settingId < right.settingId;
    });
    return results;
}

Result<void> SettingsService::load(SettingScope scope, const std::filesystem::path& path) {
    return store(scope).load(path);
}

Result<void> SettingsService::save(SettingScope scope, const std::filesystem::path& path) const {
    const SettingsStore* found = store(scope);
    if (found == nullptr) {
        return Result<void>::success();
    }
    return found->save(path);
}

SettingsStore& SettingsService::store(SettingScope scope) {
    return stores_[std::move(scope)];
}

const SettingsStore* SettingsService::store(SettingScope scope) const {
    auto it = stores_.find(scope);
    return it == stores_.end() ? nullptr : &it->second;
}

std::uint64_t SettingsService::onDidChangeSetting(EventBus<SettingChangeEvent>::Listener listener) {
    return onDidChange_.subscribe(std::move(listener));
}

void SettingsService::removeSettingListener(std::uint64_t listenerId) {
    onDidChange_.unsubscribe(listenerId);
}

std::vector<SettingScopeKind> SettingsService::resolutionOrder(const SettingDefinition& definition) const {
    return definition.resolutionOrder.empty() ? defaultResolutionOrder(definition.supportedScopes) : definition.resolutionOrder;
}

std::optional<SettingScope> SettingsService::scopeFor(SettingScopeKind kind, const SettingQuery& query) const {
    switch (kind) {
    case SettingScopeKind::Ide:
        return SettingScope{.kind = SettingScopeKind::Ide};
    case SettingScopeKind::Workspace:
        return SettingScope{.kind = SettingScopeKind::Workspace, .qualifier = query.workspaceId};
    case SettingScopeKind::Project:
        if (query.projectId.empty()) {
            return std::nullopt;
        }
        return SettingScope{.kind = SettingScopeKind::Project, .qualifier = query.projectId};
    case SettingScopeKind::Language:
        if (query.languageId.empty()) {
            return std::nullopt;
        }
        return SettingScope{.kind = SettingScopeKind::Language, .qualifier = query.languageId};
    }
    return std::nullopt;
}

std::vector<std::string> SettingsService::nodePath(const std::string& nodeId) const {
    std::vector<std::string> reversed;
    std::string current = nodeId;
    while (!current.empty()) {
        auto it = nodes_.find(current);
        if (it == nodes_.end()) {
            break;
        }
        reversed.push_back(it->second.title);
        current = it->second.parentId;
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

bool SettingsService::valueMatchesType(const SettingDefinition& definition, const SettingValue& value) const {
    return definition.type == value.type;
}

void SettingsService::publish(SettingChangeEvent event) {
    onDidChange_.publish(std::move(event));
}

void registerDefaultSettings(SettingsService& settings) {
    settings.registerNode({.id = "editor", .ownerId = "vanta.core", .title = "Editor", .order = 10});
    settings.registerNode({.id = "editor.behavior", .parentId = "editor", .ownerId = "vanta.core", .title = "Behavior", .order = 10});
    settings.registerNode({.id = "editor.font", .parentId = "editor", .ownerId = "vanta.core", .title = "Font", .order = 20});
    settings.registerNode({.id = "ai", .ownerId = "vanta.core", .title = "AI", .order = 20});
    settings.registerNode({.id = "ai.agent", .parentId = "ai", .ownerId = "vanta.core", .title = "Agent", .order = 10});
    settings.registerNode({.id = "ai.inlineCompletion", .parentId = "ai", .ownerId = "vanta.core", .title = "Inline Completion", .order = 20});
    settings.registerNode({.id = "build", .ownerId = "vanta.core", .title = "Build", .order = 30});
    settings.registerNode({.id = "build.cmake", .parentId = "build", .ownerId = "vanta.cmake", .title = "CMake", .order = 10});
    settings.registerNode({.id = "execution", .ownerId = "vanta.core", .title = "Execution", .order = 40});
    settings.registerNode({.id = "index", .ownerId = "vanta.core", .title = "Indexing", .order = 50});

    settings.registerSetting({
        .id = "editor.fontSize",
        .ownerId = "vanta.core",
        .nodeId = "editor.font",
        .title = "Font Size",
        .description = "Default editor font size.",
        .type = SettingValueType::Int,
        .defaultValue = SettingValue::intValue(14),
        .supportedScopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace},
        .resolutionOrder = {SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"font", "editor"},
        .order = 10,
    });
    settings.registerSetting({
        .id = "editor.formatOnSave",
        .ownerId = "vanta.core",
        .nodeId = "editor.behavior",
        .title = "Format On Save",
        .description = "Format documents when they are saved.",
        .type = SettingValueType::Bool,
        .defaultValue = SettingValue::boolValue(false),
        .supportedScopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace, SettingScopeKind::Language},
        .resolutionOrder = {SettingScopeKind::Language, SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"format", "save", "language"},
        .order = 10,
    });
    settings.registerSetting({
        .id = "ai.agent.model",
        .ownerId = "vanta.core",
        .nodeId = "ai.agent",
        .title = "Agent Model",
        .description = "Model used for agent coding tasks.",
        .type = SettingValueType::String,
        .defaultValue = SettingValue::stringValue("default"),
        .supportedScopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace, SettingScopeKind::Project},
        .resolutionOrder = {SettingScopeKind::Project, SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"ai", "agent", "model"},
        .order = 10,
    });
    settings.registerSetting({
        .id = "ai.inlineCompletion.enabled",
        .ownerId = "vanta.core",
        .nodeId = "ai.inlineCompletion",
        .title = "Inline Completion",
        .description = "Enable inline completion suggestions.",
        .type = SettingValueType::Bool,
        .defaultValue = SettingValue::boolValue(true),
        .supportedScopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace, SettingScopeKind::Language},
        .resolutionOrder = {SettingScopeKind::Language, SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"ai", "completion", "inline"},
        .order = 10,
    });
    settings.registerSetting({
        .id = "cmake.buildDirectory",
        .ownerId = "vanta.cmake",
        .nodeId = "build.cmake",
        .title = "Build Directory",
        .description = "Default CMake build directory.",
        .type = SettingValueType::Path,
        .defaultValue = SettingValue::pathValue("build"),
        .supportedScopes = {SettingScopeKind::Workspace, SettingScopeKind::Project},
        .resolutionOrder = {SettingScopeKind::Project, SettingScopeKind::Workspace},
        .tags = {"cmake", "build", "directory"},
        .aliases = {"cmake build dir"},
        .order = 10,
    });
    settings.registerSetting({
        .id = "execution.defaultTarget",
        .ownerId = "vanta.core",
        .nodeId = "execution",
        .title = "Default Target",
        .description = "Default execution target id.",
        .type = SettingValueType::String,
        .defaultValue = SettingValue::stringValue("local.default"),
        .supportedScopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace, SettingScopeKind::Project},
        .resolutionOrder = {SettingScopeKind::Project, SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"run", "execution", "target"},
        .order = 10,
    });
    settings.registerSetting({
        .id = "index.autoRefresh",
        .ownerId = "vanta.core",
        .nodeId = "index",
        .title = "Auto Refresh Index",
        .description = "Refresh indexes when workspace files change.",
        .type = SettingValueType::Bool,
        .defaultValue = SettingValue::boolValue(true),
        .supportedScopes = {SettingScopeKind::Ide, SettingScopeKind::Workspace},
        .resolutionOrder = {SettingScopeKind::Workspace, SettingScopeKind::Ide},
        .tags = {"index", "refresh"},
        .order = 10,
    });
}

std::string toString(SettingScopeKind kind) {
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

std::string toString(SettingValueType type) {
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

Json toJson(const SettingValue& value) {
    switch (value.type) {
    case SettingValueType::Bool:
        return Json(std::get<bool>(value.data));
    case SettingValueType::Int:
        return Json(static_cast<std::int64_t>(std::get<int>(value.data)));
    case SettingValueType::Double:
        return Json(std::get<double>(value.data));
    case SettingValueType::String:
    case SettingValueType::Path:
        return Json(std::get<std::string>(value.data));
    case SettingValueType::StringList: {
        Json::Array values;
        for (const std::string& item : std::get<std::vector<std::string>>(value.data)) {
            values.push_back(Json(item));
        }
        return Json::array(std::move(values));
    }
    }
    return Json();
}

std::optional<SettingValue> settingValueFromJson(const Json& json, SettingValueType type) {
    switch (type) {
    case SettingValueType::Bool:
        return json.isBool() ? std::optional<SettingValue>(SettingValue::boolValue(json.asBool())) : std::nullopt;
    case SettingValueType::Int:
        return json.isInt() ? std::optional<SettingValue>(SettingValue::intValue(static_cast<int>(json.asInt()))) : std::nullopt;
    case SettingValueType::Double:
        return json.isNumber() ? std::optional<SettingValue>(SettingValue::doubleValue(json.isDouble() ? json.asDouble() : static_cast<double>(json.asInt()))) : std::nullopt;
    case SettingValueType::String:
        return json.isString() ? std::optional<SettingValue>(SettingValue::stringValue(json.asString())) : std::nullopt;
    case SettingValueType::Path:
        return json.isString() ? std::optional<SettingValue>(SettingValue::pathValue(json.asString())) : std::nullopt;
    case SettingValueType::StringList: {
        if (!json.isArray()) {
            return std::nullopt;
        }
        std::vector<std::string> values;
        for (const Json& item : json.asArray()) {
            if (!item.isString()) {
                return std::nullopt;
            }
            values.push_back(item.asString());
        }
        return SettingValue::stringListValue(std::move(values));
    }
    }
    return std::nullopt;
}

std::string settingValueToString(const SettingValue& value) {
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

PluginStorageService::PluginStorageService(std::filesystem::path root)
    : root_(std::move(root)) {}

void PluginStorageService::setRoot(std::filesystem::path root) {
    root_ = std::move(root);
}

Result<void> PluginStorageService::write(std::string pluginId, std::string key, Json value) const {
    const std::filesystem::path path = pathFor(pluginId, key);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return Result<void>::failure("storage.directory", "Failed to create plugin storage directory");
    }

    std::ofstream output(path);
    if (!output) {
        return Result<void>::failure("storage.write", "Failed to write plugin storage");
    }
    output << value.dump();
    return Result<void>::success();
}

Result<Json> PluginStorageService::read(const std::string& pluginId, const std::string& key) const {
    const std::filesystem::path path = pathFor(pluginId, key);
    std::ifstream input(path);
    if (!input) {
        return Result<Json>::failure("storage.read", "Failed to read plugin storage");
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return Result<Json>::success(Json::parse(stream.str()));
}

std::filesystem::path PluginStorageService::pathFor(const std::string& pluginId, const std::string& key) const {
    return root_ / pluginId / (key + ".json");
}

}
