#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/diagnostic.h"

namespace vanta {

struct EditorTab {
    std::uint64_t id = 0;
    VirtualFile file;
    std::string title;
    bool active = false;
    bool dirty = false;
    bool placeholder_editor = true;
};

class EditorWorkspace {
public:
    const std::vector<EditorTab>& Tabs() const;
    const EditorTab* ActiveTab() const;

    EditorTab& OpenFile(const VirtualFile& file);
    bool CloseTab(std::uint64_t id);
    bool ActivateTab(std::uint64_t id);
    std::optional<std::uint64_t> FindTab(const VirtualFile& file) const;
    EditorTab* OpenDiagnostic(const Diagnostic& diagnostic);

private:
    std::vector<EditorTab> tabs_;
    std::uint64_t next_tab_id_ = 1;
};

}
