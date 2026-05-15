# Vanta Service Boundaries

Vanta services are grouped by the API surface they belong to. A service can move between groups as the platform matures, but new APIs should declare their intended layer before being exposed to plugins.

## Core Platform Services

Core platform services are available from `WorkspaceContext` and are not tied to a specific language or build system.

- `WorkspaceFiles` owns file read/write access through `VirtualFile`.
- `WorkspaceEvents` owns typed IDE events.
- `DocumentService` owns open document overlays and save state.
- `CommandRegistry` owns commands and command invocation.
- `JobService` owns background job submission, state, progress, cancellation handlers, dependency visibility, waiting, and output.
- `IndexCoordinator` owns index providers and snapshots; refresh work is submitted through `JobService`.
- `CapabilityRegistry` owns readiness and capability state derived from runtime services.
- `WorkspaceInitializationPipeline` owns startup and project refresh stages.
- `SettingsService` owns typed setting definitions, scoped values, search, and effective value resolution.
- `ChangeSetService` owns proposed workspace edits, approval status, preflight validation, and apply.
- `ExecutionHost` owns execution targets and process execution providers; callers select an `ExecutionTarget`, not a separate executor.
- `BuildService` owns build provider detection, build plans, diagnostic resolution, and execution delegation through `ExecutionHost`.
- `RunConfigurationRegistry` owns run configuration types, producers, stored configurations, and single-job run execution.

## Project-Scoped Services

Project-scoped services depend on the current `ProjectModel` and may change when the project is refreshed.

- `ProjectManager` resolves the current project model.
- `ProjectModelProviderRegistry` lets plugins contribute project model providers.
- `ProjectComponents` binds components whose lifecycle follows the project.
- `ProjectTemplateService` owns project template categories and template materialization.
- `ScratchFileService` creates lightweight scratch files inside the workspace runtime.

## Language And Code Intelligence

Language APIs are platform-level contracts, while language servers and compiler integrations are providers.

- `LanguageRegistry` resolves language definitions and services for files with optional project context.
- `LanguageRequestPipeline` routes LSP-style document requests to the resolved language service.
- `CodeIntelligenceService` owns completion and inline completion provider orchestration.
- `LspLanguageService` is an adapter implementation, not the platform API.

## Agent Platform

Agent APIs should remain auditable and should not mutate the workspace without a `ChangeSet`.

- `AgentOperationService` owns agent runs and the structured operation protocol.
- `AgentOperationJournal` records operation input summaries, events, results, and change sets.
- `AgentContextProviders` collect context for model prompts.
- `AgentTools` expose callable tools, but long-lived IDE operations should prefer `AgentOperationService`.
- Agent writes should create `ChangeSet` values and rely on user approval before apply.
- Agent runs should use operations internally instead of mutating documents or creating change sets directly.

## Plugin API

Plugin APIs are exposed through `ExtensionContext`. Third-party plugins should depend on service interfaces rather than concrete runtime implementations.

- `ExtensionContext` exposes workspace, command, language, build, execution, run, index, capability, job, agent, and change-set APIs.
- Plugins may contribute setting nodes and setting definitions, but plugin identity is an owner, not a settings scope.
- Built-in plugins may run in process.
- Third-party plugins should run out of process unless explicitly trusted.
- Hot unload must release registrations tracked by plugin activation state.

## Built-In Implementation Details

Built-ins can provide default implementations, but core services should not depend on one built-in implementation directly.

- CMake is a build/project model provider that produces `BuildPlan` steps.
- clice is a C++ language/index provider.
- Git is a source-control provider.
- LSP is a language-service adapter.

## UI-Facing Facades

UI code should prefer stable query APIs and event streams rather than reaching into implementation details.

- Background UI should read `JobService` and `WorkspaceInitializationPipeline`.
- Readiness UI should read `CapabilityRegistry`.
- Settings UI should read `SettingsService` nodes, definitions, scope descriptors, and search results without depending on a fixed UI layout.
- Index UI should read `IndexCoordinator`.
- Agent timeline UI should read `AgentOperationJournal`.
- Diff/approval UI should read `ChangeSetService`.
