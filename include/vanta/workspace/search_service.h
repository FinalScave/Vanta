#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vanta/workspace/workspace.h"

namespace vanta {

struct SearchIndexEntry {
    VirtualFile file;
    std::string name;
    bool directory = false;
};

struct SearchHit {
    VirtualFile file;
    int line = 0;
    int column = 0;
    std::string preview;
    int score = 0;
};

class SearchService {
public:
    void rebuild(const Workspace& workspace);
    void clear();

    std::vector<SearchHit> searchFiles(const std::string& query, std::size_t limit = 50) const;
    std::vector<SearchHit> searchText(const std::string& query, std::size_t limit = 50) const;
    const std::vector<SearchIndexEntry>& entries() const;

private:
    void addTree(const FileTreeNode& node);

    std::vector<SearchIndexEntry> entries_;
};

}
