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
    bool placeholderEditor = true;
};

class EditorWorkspace {
public:
    const std::vector<EditorTab>& tabs() const;
    const EditorTab* activeTab() const;

    EditorTab& openFile(const VirtualFile& file);
    bool closeTab(std::uint64_t id);
    bool activateTab(std::uint64_t id);
    std::optional<std::uint64_t> findTab(const VirtualFile& file) const;
    EditorTab* openDiagnostic(const Diagnostic& diagnostic);

private:
    std::vector<EditorTab> tabs_;
    std::uint64_t nextTabId_ = 1;
};

}
