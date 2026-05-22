# ABI Boundaries

Vanta has three API surfaces. They must stay separate because they have different
compatibility and lifetime rules.

## Layers

### Core C++ API

The core C++ API is used by Vanta itself, built-in extensions, tests, and source
compatible integrations that are compiled with the same toolchain expectations.
It may use C++ types such as `std::vector`, `std::string`, `std::unique_ptr`,
references, virtual classes, and templates.

This layer is not a stable binary ABI. Public headers under `include/vanta`
belong to this layer unless they explicitly use the native plugin ABI prefix.

### Native Plugin ABI

The native plugin ABI is the stable binary boundary for third-party native
plugins. It must remain a C ABI.

Rules:

- Use opaque handles such as `VantaHost*`, `VantaPlugin*`, and `VantaHandle*`.
- Use function tables with `abi_version` and `struct_size`.
- Use `VantaStringView` for borrowed strings.
- Use `VantaOwnedBytes` or explicit release callbacks for owned memory.
- Use `function pointer + user_data` instead of `std::function`.
- Use output parameters such as `T** out_value` for created handles.
- Return `VantaStatus` for errors.
- Never expose C++ references, STL containers, smart pointers, exceptions, RTTI,
  virtual classes, or `Value`.

### Plugin RPC Protocol

Out-of-process plugins communicate through a protocol boundary. The transport can
use JSON text because process isolation already owns the compatibility boundary.

Rules:

- Use JSON text or bytes on the wire.
- Do not expose `Value` as an ABI type.
- Version every request/response shape that may be used by external plugins.
- Treat process crashes as plugin failures, not IDE failures.

## Pointer And Reference Policy

### Use References In Core C++ For Required Borrowed Inputs

Use `T&` or `const T&` when the parameter is required, synchronous, and not
stored beyond the call. Examples include operation contexts and immutable request
inputs.

Good examples:

- `BuildProvider::Plan(WorkspaceContext& context, const BuildRequest& request)`
- `IndexProvider::Refresh(WorkspaceContext& context, JobContext& job)`
- `ProjectManager::Refresh(WorkspaceContext& context, Project& project)`

References in this category must not be retained by the callee.

### Use Pointers For Optional Or Stored Borrowed Objects

Use raw pointers for nullable relationships, lookup results, or stored borrowed
objects whose owner is elsewhere.

Good examples:

- `WorkspaceContext::CurrentProject()` can return `Project*` because a workspace
  may not have an active project.
- `WorkspaceContext` stores `WorkspaceRuntime*` because the runtime owns the
  context and controls its lifetime.
- `VirtualFile` stores `const VirtualFileSystem*` as a non-owning provider
  pointer.

Stored raw pointers must have an owner that outlives the borrower. The owner must
be clear from construction or attachment order.

### Use Smart Pointers Only For Core Ownership

Use `std::unique_ptr` for ownership transfer inside the Core C++ API. Provider
registration methods may accept `std::unique_ptr<T>` because they are source API,
not ABI.

Use `std::shared_ptr` only for shared internal state, such as asynchronous handle
state. Do not use `std::shared_ptr` to express service ownership or plugin ABI
ownership.

### Native ABI Always Uses Pointers

The native ABI must use pointers even for required values. Required pointers are
validated at the function boundary and return `VANTA_STATUS_INVALID_ARGUMENT`
when null.

## Dependency Injection Policy

### Constructor Injection

Use constructor injection for dependencies that are stable for the lifetime of
the object.

Good examples:

- `WorkspaceRuntime` receives job and main dispatchers at construction.
- A synchronizer can receive services that it watches for its lifetime.

Constructor-injected dependencies should not vary by request or project.

### Function Parameter Injection

Use function parameters for per-operation context, current workspace state, or
objects that may vary by request.

Good examples:

- `WorkspaceContext&` for provider operations.
- `Project&` for project lifecycle operations.
- `JobContext&` for progress and cancellation during a submitted job.
- `ExecutionTarget` for a specific run or build step.

Providers should not store `WorkspaceContext&`. A single provider may serve
multiple projects, multiple workspaces, tests, or reloaded plugin sessions.

### Avoid Hidden Service Proxies

Services should not expose convenience methods that merely proxy unrelated
services. Callers should access the needed service through `WorkspaceContext`.
This keeps ownership and dependency flow visible.

## Error And Lifetime Rules

- Core C++ may use `std::optional`, `Result<T>`, and error strings where already
  established.
- Native ABI returns `VantaStatus` and must not throw across the boundary.
- Plugin RPC returns versioned response objects with explicit error fields.
- Handles returned across a native ABI boundary must have an explicit release
  function.
- Borrowed data is valid only during the call unless the API explicitly says
  otherwise.
- Callbacks must document their thread and lifetime expectations.

## Threading Rules

- Core async work is represented by dispatcher callbacks, not a globally exposed
  runtime object.
- UI or host applications own the main-thread dispatcher.
- Services should publish state changes through IDE events or service-specific
  event buses.
- ABI callbacks must be treated as host calls. They cannot assume Vanta internal
  locks, C++ exceptions, or UI thread availability.

## Current Audit Findings

These areas are acceptable as Core C++ API but must not be treated as native ABI:

- `ExtensionContext` and in-process extension virtual classes.
- Provider registration methods that take `std::unique_ptr`.
- Service callbacks based on `std::function`.
- `WorkspaceContext` service accessors returning references.
- `Value` in core data paths.

These areas should remain under review:

- Public headers that expose implementation-only helper functions.
- Public headers that include platform runtime types unnecessarily.
- Any provider or plugin object that stores `WorkspaceContext&` beyond one call.
- Any use of `std::shared_ptr` outside async handle state.

## Native ABI Expansion Direction

The native ABI should evolve as a host function table, not as exported C++
classes.

Expected shape:

- `VantaHostApi` owns host functions.
- `VantaPluginApi` owns plugin callbacks.
- Every ABI struct starts with `struct_size`.
- Every function validates `abi_version` and required pointers.
- Every returned handle has a matching release function.
- Every large result uses owned bytes with host-controlled release.

The first stable ABI capabilities should be narrow:

- command registration
- event subscription
- workspace metadata query
- file read/write through URI strings
- language/build/run provider registration through C descriptors
- RPC fallback for complex or evolving payloads

Complex typed C++ objects should stay in the Core C++ API until there is a clear
native ABI need.
