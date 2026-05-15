#include "vanta/workspace/editor.h"

#include <algorithm>

namespace vanta {

std::string toString(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Note:
        return "note";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }
    return "note";
}

const std::vector<EditorTab>& EditorWorkspace::tabs() const {
    return tabs_;
}

const EditorTab* EditorWorkspace::activeTab() const {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [](const EditorTab& tab) {
        return tab.active;
    });
    return it == tabs_.end() ? nullptr : &*it;
}

EditorTab& EditorWorkspace::openFile(const VirtualFile& file) {
    if (auto existing = findTab(file)) {
        activateTab(*existing);
        auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const EditorTab& tab) {
            return tab.id == *existing;
        });
        return *it;
    }

    for (EditorTab& tab : tabs_) {
        tab.active = false;
    }

    EditorTab tab;
    tab.id = nextTabId_++;
    tab.file = file;
    tab.title = file.displayName();
    tab.active = true;
    tabs_.push_back(std::move(tab));
    return tabs_.back();
}

bool EditorWorkspace::closeTab(std::uint64_t id) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const EditorTab& tab) {
        return tab.id == id;
    });
    if (it == tabs_.end()) {
        return false;
    }

    const bool wasActive = it->active;
    tabs_.erase(it);
    if (wasActive && !tabs_.empty()) {
        tabs_.back().active = true;
    }
    return true;
}

bool EditorWorkspace::activateTab(std::uint64_t id) {
    bool found = false;
    for (EditorTab& tab : tabs_) {
        tab.active = tab.id == id;
        found = found || tab.active;
    }
    return found;
}

std::optional<std::uint64_t> EditorWorkspace::findTab(const VirtualFile& file) const {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const EditorTab& tab) {
        return tab.file == file;
    });
    if (it == tabs_.end()) {
        return std::nullopt;
    }
    return it->id;
}

EditorTab* EditorWorkspace::openDiagnostic(const Diagnostic& diagnostic) {
    EditorTab& tab = openFile(diagnostic.location.file);
    return &tab;
}

}
