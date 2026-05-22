# Agent-Native IDE Platform

Vanta is an agent-native IDE platform. The goal is not to embed a chat agent
inside a C++ editor, but to expose structured IDE semantics to agents so they
can reason about projects through the same core model that powers the UI.

The first strong scenario is C++ because C++ makes the value of structured IDE
context obvious: compiler arguments, include paths, macros, generated files,
multi-target build graphs, diagnostics, and module boundaries all affect the
correctness of an edit. A grep-and-LSP agent can help in this environment, but
it usually lacks the project semantics needed to act reliably.

## Product Position

Vanta should be understood as:

> An agent-native IDE platform where AI agents operate on structured IDE
> semantics instead of raw files and shell output.

C++ is the first proving ground, not the platform boundary. Future language,
build-system, and project-family support should be contributed through the same
provider and plugin model used by the built-in C++, CMake, Python, clice, and
Git integrations.

## Core Principle

Vanta Core is the source of truth. UI clients and agents are clients of the
same core state and command surface.

```text
Vanta Core
  Workspace / Project / Document / Language / Index
  Build / Execution / Job / Debug / Git
  Agent / ChangeSet / Settings / Plugin lifecycle

Clients
  Desktop UI
  CLI
  Headless agent session
  Remote or CI session

Plugins
  Language providers
  Build providers
  Project model providers
  Execution providers
  Index providers
  Agent tools and context providers
  UI and command extension points
```

Agents should operate on Vanta Core, not on UI automation. The UI reacts to core
events and state projections. When an agent opens context, proposes edits, runs
builds, or requests approval, it should do so through core APIs. The desktop UI
then renders the relevant state.

## Architectural Rules

- Core-first: capabilities that an agent, CLI, remote session, or CI job can use
  belong in Vanta Core. UI code should render and dispatch, not own core IDE
  behavior.
- Agent-visible semantics: project models, language resolution, indexes,
  diagnostics, build plans, execution targets, run configurations, jobs, debug
  state, Git state, settings, and change sets must be queryable without a UI.
- ChangeSet-first editing: agent edits should produce auditable `ChangeSet`
  values. Applying changes is a separate approval and execution step.
- Provider-based platform: CMake, clice, C++, Python, Git, and future Android or
  Java support are providers or built-in plugins, not hard-coded assumptions in
  the workspace kernel.
- Headless-ready: Vanta Core must run without a desktop UI. A headless process
  should be able to open a workspace, resolve project context, run indexing,
  execute builds, collect diagnostics, and produce change sets.
- UI state isolation: agents may maintain their own session state without
  stealing the user's editor focus, tabs, selection, or active diff. Shared UI
  state is synchronized only when the user needs to see or approve something.
- Stable plugin boundary: in-process built-ins may use C++ interfaces directly.
  Third-party plugin ABI should use C handles or out-of-process RPC rather than
  C++ STL, smart pointer, or virtual-class binary boundaries.

## Agent Capabilities

The agent should be able to ask high-level questions that are hard to answer
with raw grep:

- Which project model owns this file?
- Which build target or compile command applies to this source?
- Which headers, source roots, generated files, and excluded roots affect this
  module?
- Which diagnostics belong to the active file, target, or recent build?
- Which symbols, references, call sites, include edges, and index hits are
  relevant to the requested edit?
- Which tests or run configurations are related to the changed files?
- Which change set is pending approval, and what is its inverse operation?

These questions should be answered through typed Vanta services and providers,
not by scraping UI state or guessing from shell output.

## Agent Efficiency And Reliability Directions

Vanta should improve agents by moving them away from raw text exploration and
toward direct operations on IDE-level project state. The following directions
describe the capability surface that can make agents faster, safer, and more
trustworthy.

### Structured Context

Agents should be able to collect precise engineering context without reading the
whole repository or repeatedly searching for text.

- Symbol context: definitions, declarations, references, implementations,
  overrides, imports, and exports.
- Type context: resolved types, inferred types, generic constraints, protocol or
  interface conformance, and API compatibility.
- Call and dependency context: call hierarchy, type hierarchy, include edges,
  module dependencies, package dependencies, and cross-language edges.
- Project context: source roots, test roots, generated roots, excluded roots,
  build targets, package boundaries, workspace packages, and project facets.
- Editor context: open files, cursor position, selections, visible diagnostics,
  unsaved document overlays, recent user edits, and pending diffs.
- Runtime context: running jobs, dev servers, debug sessions, terminal output,
  logs, crash reports, browser console output, and network failures.
- Git context: current branch, dirty files, staged files, recent commits, merge
  base, pull request diff, and user-owned changes.
- Configuration context: compiler flags, language versions, build settings,
  formatter settings, lint rules, CI workflows, environment variables, and SDK
  versions.
- Local style context: naming patterns, directory conventions, test style,
  error-handling style, component patterns, and established abstractions.

### Context Retrieval

Vanta should turn repository understanding into a reusable index rather than a
per-agent discovery cost.

- Maintain an incremental project knowledge graph across files, symbols,
  targets, diagnostics, tests, and runtime jobs.
- Return minimal relevant slices such as symbol summaries, AST ranges,
  diagnostic groups, and impacted files instead of whole files by default.
- Combine semantic search with symbol search so agents can find concepts by
  natural language and then confirm them through typed references.
- Deduplicate context across repeated tool calls and long-running sessions.
- Preserve reusable findings across agent operations when they are still valid
  for the current index revision.
- Support cross-language lookups, such as a frontend caller linked to a backend
  route or a native bridge implementation.
- Recommend the next most relevant files, symbols, tests, and commands for the
  current task.

### Semantic Refactoring

Agents should use IDE refactoring operations instead of text replacement when a
semantic operation exists.

- Rename symbols across declarations, references, imports, tests, and generated
  metadata when supported.
- Move files, classes, functions, components, and modules while updating imports
  and package paths.
- Extract functions, methods, components, constants, interfaces, and helper
  modules.
- Inline variables, functions, wrappers, and redundant abstractions.
- Change signatures and update call sites with safe default migrations.
- Add, remove, reorder, or rename parameters while preserving behavior where
  possible.
- Organize imports, remove unused symbols, and apply language-specific cleanup
  actions.
- Run safe delete analysis before removing symbols, files, or modules.
- Convert patterns through structured code actions, such as callback to async,
  class component to function component, or concrete type to interface.
- Preview refactor impact before applying it and expose the affected symbols,
  files, diagnostics, and tests.

### Safe Editing

Agent edits should be auditable workspace transactions, not unstructured file
patches.

- Represent proposed changes as `ChangeSet` values with preview, approval,
  apply, inverse edits, and undo tokens.
- Prefer AST-aware or range-stable edits when language services can provide
  them.
- Detect conflicts with unsaved buffers, user edits, generated files, and files
  outside the intended ownership scope.
- Minimize diffs and avoid unrelated formatting churn.
- Validate multi-file edits before apply so partial workspace mutation is
  avoided.
- Attach explanations to changed symbols, behavioral surfaces, and migration
  risks rather than only to text hunks.
- Keep agent session state separate from UI state until a user needs to review,
  approve, or inspect the result.

### Testing And Verification

Vanta should help agents select the cheapest verification path that still gives
confidence.

- Select impacted tests from changed files, symbols, project targets, dependency
  edges, and historical failures.
- Run targeted type checks, lint checks, builds, and tests through typed build
  and execution services.
- Map failures back to symbols, diagnostics, stack frames, and changed hunks.
- Identify missing tests for new behavior, edge cases, public APIs, and
  regression-prone paths.
- Generate tests using existing test style, fixtures, helpers, and project
  conventions.
- Detect flaky tests and separate infrastructure failures from code failures.
- Track coverage gaps where the changed behavior is not exercised.
- Summarize verification status in a structured result that can be rendered by
  UI clients and consumed by other agents.

### Debugging And Runtime Context

Agents should be able to reason from live runtime state instead of only static
source files.

- Read stack traces, source maps, locals, watches, breakpoints, and debug
  session state.
- Link runtime errors to source files, symbols, build targets, and recent
  changes.
- Insert temporary probes, logpoints, or instrumentation through auditable
  changes and clean them up after debugging.
- Capture reproduction context such as commands, arguments, environment
  variables, seeds, input payloads, and device or browser targets.
- Inspect browser console messages, network requests, layout failures, and
  accessibility issues for UI applications.
- Connect profiling output to hot symbols, slow tests, expensive renders, memory
  growth, and repeated work.
- Cluster repeated crashes or diagnostics that share a likely root cause.

### Code Generation Quality

Generated code should be grounded in the current workspace model.

- Resolve existing APIs, schemas, types, and conventions before generating new
  code.
- Reuse established modules, helpers, components, error handling, logging, and
  test utilities.
- Check whether a similar implementation already exists before creating another
  abstraction.
- Generate imports, namespaces, package declarations, registrations, and build
  metadata through project-aware services.
- Use design-system components, tokens, and interaction patterns for UI work.
- Use API contracts, database schemas, migrations, route definitions, protocol
  files, and fixtures for backend or data work.
- Run formatting, cleanup, type checks, and targeted verification after
  generation.
- Prefer official project creation and registration paths over ad hoc file
  creation.

### Architecture Awareness

Vanta should expose architectural boundaries so agents can avoid changes that
fit locally but violate the system.

- Identify module boundaries, layering rules, public and private APIs, feature
  ownership, and provider responsibilities.
- Detect circular dependencies, layering violations, private API leaks, and
  duplicate implementations.
- Show canonical implementations and nearby patterns before an agent adds a new
  one.
- Explain where a requirement should land based on project structure and
  service boundaries.
- Trace schema, route, protocol, UI, and test impacts across the workspace.
- Recognize dead code, shadow APIs, stale migrations, and code paths that are no
  longer reachable.
- Persist project-specific rules and preferences that should influence future
  agent actions.

### Task Planning And Multi-Agent Work

Agent planning should be driven by the real workspace graph and current
workspace state.

- Decompose tasks by files, symbols, modules, targets, and verification scope.
- Identify required reads, likely edit locations, related tests, documentation,
  schemas, fixtures, and generated artifacts.
- Estimate blast radius before choosing between a narrow edit, a semantic
  refactor, or a broader migration.
- Allocate non-overlapping ownership scopes for parallel agents.
- Share symbol-level findings, impacted files, pending changes, and verification
  results between agents.
- Detect edit conflicts early and merge non-overlapping change sets when safe.
- Keep task status tied to concrete workspace evidence such as diagnostics,
  test results, builds, and pending approvals.

### Review And Documentation

Vanta should turn review and documentation into structured follow-up on real
changes.

- Review changed symbols, public APIs, migrations, tests, and generated metadata
  instead of only changed lines.
- Detect accidental formatting churn, dead imports, unreachable branches,
  missing states, missing error handling, and behavioral regressions.
- Check whether tests cover changed behavior and whether the selected
  verification path was sufficient.
- Generate pull request summaries, risk notes, test plans, release notes, and
  migration notes from the actual change set.
- Update README files, architecture docs, API docs, typed docs, schemas, and
  examples when behavior or contracts change.
- Detect documentation drift where comments, docs, or examples no longer match
  implementation.
- Attach review comments and follow-up tasks to concrete symbols, diagnostics,
  or diff ranges.

### Environment, Security, And Performance

The IDE core should reduce operational uncertainty around agent actions.

- Identify the correct package manager, build tool, workspace package, target,
  test command, SDK, and device or runtime target.
- Reuse running dev servers, build caches, test caches, indexes, and background
  jobs where possible.
- Manage long-running commands through `JobService` with cancellation, progress,
  logs, and structured results.
- Diagnose missing environment variables, incompatible SDK versions, stale
  lockfiles, and dependency mismatch.
- Track dependency upgrade impact across imports, lockfiles, build targets,
  tests, and runtime behavior.
- Detect secrets, dangerous commands, broad file access, permission boundary
  issues, and sensitive data flow.
- Surface dependency vulnerabilities, license risks, injection risks, and auth
  or authorization boundary mistakes.
- Prefer incremental indexing, targeted verification, and batched operations to
  reduce latency and token usage.

### Agent-Native IDE APIs

The public agent surface should expose high-level IDE primitives rather than
forcing agents to emulate a human using an editor.

- `findSymbol`, `findReferences`, `definition`, `implementation`,
  `callHierarchy`, and `typeHierarchy`.
- `collectTaskContext`, `relatedFiles`, `relatedTests`, `impactedTargets`, and
  `diagnosticsForScope`.
- `renameSymbol`, `moveSymbol`, `changeSignature`, `extractFunction`,
  `organizeImports`, and other semantic refactor operations.
- `createChangeSet`, `previewChangeSet`, `preflightChangeSet`,
  `applyChangeSet`, and `undoChangeSet`.
- `runImpactedTests`, `runTargetedBuild`, `runConfiguration`,
  `attachDebugger`, and `collectRuntimeEvidence`.
- `reviewChangeSet`, `summarizeChangeSet`, `generateTestPlan`, and
  `generateDocumentationUpdate`.

Each API should return structured results that can be consumed by agents, UI
clients, headless sessions, and CI workflows. The platform advantage comes from
letting agents operate on symbols, types, diagnostics, refactors, tests,
runtime state, and auditable change transactions instead of raw files and shell
text.

## UI Synchronization

The UI should be a client of core state:

```text
Core event
  -> UiStateStore refresh
  -> UI renderer update
  -> user action
  -> command or typed service call
  -> Core state change
```

An agent session follows the same model:

```text
Agent request
  -> WorkspaceContext query
  -> Project / index / language / diagnostic context
  -> Agent operation
  -> ChangeSet, build, test, or explanation
  -> UI projection or approval request when needed
```

This keeps UI state and agent state separate while still letting the UI stay
live and synchronized.

## C++ Proving Ground

The C++ integration should demonstrate the platform advantage over grep/LSP
agents:

- clice or another compiler-index provider should feed semantic index data into
  Vanta's own services.
- CMake should contribute project model, build provider, build directory, target
  data, and compile database context.
- Language service adapters should remain adapters. Vanta's `LanguageRegistry`
  and `CodeIntelligenceService` are the platform API.
- Build and test loops should report structured diagnostics into
  `DiagnosticService` and `JobService`.
- Agent fixes should be generated as `ChangeSet` values, previewed through diff
  UI, and applied only after approval.

If this loop works well for C++, the same platform shape can support Android,
Java, Python, embedded projects, remote targets, and other project families.
