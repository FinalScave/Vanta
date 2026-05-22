#include "ui/editor.h"

#include <algorithm>

namespace vanta {

const std::vector<EditorTab>& EditorWorkspace::Tabs() const {
    return tabs_;
}

const EditorTab* EditorWorkspace::ActiveTab() const {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [](const EditorTab& tab) {
        return tab.active;
    });
    return it == tabs_.end() ? nullptr : &*it;
}

EditorTab& EditorWorkspace::OpenFile(const VirtualFile& file) {
    if (auto existing = FindTab(file)) {
        ActivateTab(*existing);
        auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const EditorTab& tab) {
            return tab.id == *existing;
        });
        return *it;
    }

    for (EditorTab& tab : tabs_) {
        tab.active = false;
    }

    EditorTab tab;
    tab.id = next_tab_id_++;
    tab.file = file;
    tab.title = file.DisplayName();
    tab.active = true;
    tabs_.push_back(std::move(tab));
    return tabs_.back();
}

bool EditorWorkspace::CloseTab(std::uint64_t id) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const EditorTab& tab) {
        return tab.id == id;
    });
    if (it == tabs_.end()) {
        return false;
    }

    const bool was_active = it->active;
    tabs_.erase(it);
    if (was_active && !tabs_.empty()) {
        tabs_.back().active = true;
    }
    return true;
}

bool EditorWorkspace::ActivateTab(std::uint64_t id) {
    bool found = false;
    for (EditorTab& tab : tabs_) {
        tab.active = tab.id == id;
        found = found || tab.active;
    }
    return found;
}

std::optional<std::uint64_t> EditorWorkspace::FindTab(const VirtualFile& file) const {
    auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const EditorTab& tab) {
        return tab.file == file;
    });
    if (it == tabs_.end()) {
        return std::nullopt;
    }
    return it->id;
}

EditorTab* EditorWorkspace::OpenDiagnostic(const Diagnostic& diagnostic) {
    EditorTab& tab = OpenFile(diagnostic.location.file);
    return &tab;
}

}
