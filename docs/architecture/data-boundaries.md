# Data Boundaries

Vanta uses three different data shapes for different architectural boundaries:
typed C++ objects, `Value`, and JSON text. New APIs should choose one of these
shapes deliberately instead of treating dynamic values as a default container.

## Rule Of Thumb

Core platform APIs use typed C++ objects.

`Value` is used when Vanta must preserve structured data whose schema belongs to
a component, plugin, model provider, tool, or other extension owner.

JSON text is used only at protocol and persistence boundaries where the wire or
file format is JSON.

nlohmann/json is an implementation dependency for parsing and encoding JSON
text. It should stay behind codec and adapter boundaries instead of becoming a
semantic platform type.

## Typed Core Objects

Use typed C++ structs, classes, enums, and virtual interfaces when Vanta owns the
schema and behavior.

Typed APIs should be the default for:

- Workspace, project, language, build, execution, debug, settings, indexing, and
  source-control domain models.
- `ProjectModel`, project facets, modules, project providers, and known project
  attachments.
- `BuildRequest`, `BuildPlan`, `BuildResult`, `ExecutionRequest`,
  `ExecutionTarget`, and `ExecutionResult`.
- `RunConfiguration` and concrete `RunConfigurationData` implementations.
- `LanguageService` inputs and outputs such as completion, hover, locations,
  diagnostics, and semantic tokens.
- `SettingDefinition`, `SettingValue`, setting scopes, and setting resolution.
- Built-in configuration passed to built-in plugins, such as clice executable
  path or platform-provided defaults.

Typed payloads may still provide serializers, but serialization is owned by the
type provider or by a codec layer. The payload itself should not become a
generic `Value` object just because it is persisted.

## Value Boundary

Use `Value` when the data is structured, must be retained by Vanta, and its
schema is owned outside the core service reading or storing it.

Good `Value` boundaries include:

- Component state: `Component::SaveState`, `Component::RestoreState`, and
  `ProjectState::component_states`.
- Project attachment data when the provider owns the schema.
- Plugin-owned layout/UI state such as `LayoutState::plugin_state`.
- Plugin-private storage values.
- Agent tool schemas, tool input, and tool output.
- Extension command arguments and results.
- Plugin registration metadata.
- Provider-specific extra payload attached to model, agent, debug, or job
  events when the platform does not own the schema.

Vanta-owned JSON schemas use `lowerCamel` keys. External protocols and file
formats keep their native field names at the adapter boundary.

`Value` should be used directly at these boundaries. Avoid wrapping it in empty
semantic shells when the wrapper only forwards access to the stored value.

`Value` should not replace typed objects for core behavior. If the platform
validates fields, makes routing decisions from them, or exposes the data as a
stable domain concept, the data should be modeled as typed C++.

## JSON Text Boundary

Use JSON text for process and protocol boundaries where the peer speaks JSON.
The text is the protocol payload, not a Vanta object model.

Good JSON text boundaries include:

- Out-of-process plugin RPC request and response payloads.
- Raw protocol frames that Vanta intentionally passes through.
- Future raw LSP frame pass-through APIs, if needed.

For plugin RPC, `PluginRpcRequest::paramsJson` and
`PluginRpcResponse::resultJson` keep the wire payload explicit. Adapter code may
parse the JSON text into typed objects or `Value` at the edge.

## JSON Codec Boundary

JSON text codecs parse and encode boundary payloads. Vanta uses nlohmann/json
inside these codecs, adapters, tests, CLI formatting, and persistence
implementations.

Acceptable uses include:

- `Value` to JSON text conversion and JSON text to `Value` conversion.
- LSP adapter internals.
- Plugin RPC framing internals.
- Project state, settings, layout, and other file codecs.
- App-facing presentation helpers used by the command-line shell or a future UI
  bridge.

Public platform APIs should avoid exposing nlohmann/json unless the API is
itself a JSON protocol adapter.
Projection helpers that turn domain objects into `Value` should stay in private
adapter code, such as `src/core/internal`, instead of being declared from
`include/vanta` domain headers.

## Current Code Audit

The current code has converged on the main boundary decisions:

- `Component` state uses `Value`, which is appropriate because component state
  is owned by individual components and must survive unknown or missing plugins.
- Plugin RPC uses JSON text through `paramsJson` and `resultJson`, which keeps
  cross-process wire data out of the core type system.
- Settings use typed `SettingValue`, while JSON remains the store format.
- `LanguageService` exposes Vanta language result types instead of raw LSP JSON.
- `RunConfigurationData` is typed and cloneable. Serialization belongs to
  `RunConfigurationProvider`.
- Job, model, agent, and debug extension payloads use direct optional `Value`
  fields instead of wrapper classes.
- Plugin registration metadata is direct `Value metadata`.
- Built-in plugin dependencies use typed dependency structs.
- Project attachment data uses a direct `Value` field.
- Agent tool input and schemas use direct `Value` fields instead of aliases or
  empty wrapper classes.

The current code still has areas that should continue to converge:

- `CommandRegistry` is a dynamic extension command surface. Core service-to-
  service calls should use typed service APIs instead of commands.
- Built-in command and agent-tool adapters should use small codec helpers when
  the same dynamic shape is reused.

## Refactoring Targets

Prefer this order when reducing dynamic data usage:

- Keep `RunConfigurationData` typed, with data serialization owned by
  `RunConfigurationProvider`.
- Use direct optional `Value` fields where payload data is extension-owned.
- Use direct `Value` metadata for plugin registrations.
- Use explicit typed dependency fields for built-in plugin dependencies.
- Use `Value` for project attachment metadata when the provider owns the schema.
- Keep command execution dynamic for extension commands, but keep core service
  APIs typed.
- Move public serialization helpers out of core semantic headers where practical.
- Keep nlohmann/json out of public domain headers unless a future adapter
  explicitly owns a JSON protocol surface.
- Keep domain-to-`Value` projection helpers private to adapter or protocol
  implementation code.

## Examples

Typed run configuration data:

```cpp
class RunConfigurationData {
public:
    virtual ~RunConfigurationData() = default;
    virtual std::unique_ptr<RunConfigurationData> Clone() const = 0;
};

class RunConfigurationProvider {
public:
    virtual RunConfiguration Create(WorkspaceContext& context, const VirtualFile& focus_file) const = 0;
    virtual std::unique_ptr<RunConfigurationData> LoadData(const Value& value) const = 0;
    virtual Value SaveData(const RunConfigurationData& data) const = 0;
};
```

Dynamic component state:

```cpp
class Component {
public:
    virtual void RestoreState(const Value& state);
    virtual Value SaveState() const;
};
```

JSON text protocol boundary:

```cpp
struct PluginRpcRequest {
    int id = 0;
    std::string method;
    std::string paramsJson = "{}";
};

struct PluginRpcResponse {
    int id = 0;
    bool ok = false;
    std::string resultJson = "{}";
    std::string error;
};
```
