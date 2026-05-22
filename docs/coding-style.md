# Vanta C++ Coding Style

Vanta uses a C++-oriented style that keeps platform APIs readable while keeping
data names close to standard C++ conventions.

## Naming

- Types use `PascalCase`.
- Functions and methods use `PascalCase`.
- Variables, parameters, and struct fields use `snake_case`.
- Private data members use `snake_case_`.
- Constants use `kPascalCase`.
- Enum class values use `kPascalCase`.
- Filenames use `snake_case.h` and `snake_case.cpp`.
- Vanta-owned JSON, plugin RPC, manifests, persisted state, and schema fields
  use `lowerCamel`.
- Setting ids, command ids, capability ids, and kind strings use dotted
  `lowerCamel` when Vanta owns the schema.

Examples:

```cpp
class ProjectManager {
public:
    RegistrationHandle RegisterModelProvider(std::unique_ptr<ProjectModelProvider> provider);
    std::vector<ProjectViewNode> TopLevelNodes(WorkspaceContext& context, const std::string& view_id);

private:
    std::map<std::string, std::unique_ptr<ProjectModelProvider>> model_providers_;
};

struct ProjectViewNode {
    std::string id;
    std::string label;
    bool has_children = false;
};

namespace ProjectViewNodeKind {
inline constexpr std::string_view kDirectory = "vanta.directory";
}
```

Code should follow this style unless a name intentionally matches an external
protocol, external serialized field, file format, or third-party API.
