# Settings

Vanta settings are UI-independent. The platform stores typed values, setting definitions, organization nodes, and scope resolution rules. UI code can render these concepts as a tree, tabs, search-first pages, or any other presentation.

## Concepts

- `SettingNode` organizes settings into a domain hierarchy. It is not a UI page.
- `SettingDefinition` describes one typed setting and the scopes where it can be configured.
- `SettingScope` describes where a value applies: `Ide`, `Workspace`, `Project`, or `Language`.
- `ownerId` identifies the plugin or platform module that contributed a setting. It is not a scope.
- `SettingsStore` persists values for one concrete scope.
- `SettingQuery` provides the current workspace, project, and language for resolution.
- `SettingResolution` returns the effective value and the scope that supplied it.

## Scope Resolution

Each setting declares `supportedScopes` and may declare `resolutionOrder`.

Examples:

- `editor.formatOnSave`: `Language > Workspace > Ide`
- `cmake.buildDirectory`: `Project > Workspace`
- `ai.agent.model`: `Project > Workspace > Ide`

If no scoped value exists, the setting default is used.

## Search

`SettingsService::search` is presentation-neutral. It searches setting id, title, description, tags, aliases, owner id, and node path. Results include the setting id, node id, domain path, score, and matched fields.

## Persistence

The API exposes `SettingValue`, not raw JSON. JSON is only the serialization format used by `SettingsStore`.

Stored values include their type:

```json
{
  "editor.fontSize": {"type": "int", "value": 16},
  "editor.formatOnSave": {"type": "bool", "value": true}
}
```

Plugins should use `ExtensionContext::settings()` to contribute setting nodes and definitions. Plugin-private caches and state should use plugin storage or component state, not settings.
