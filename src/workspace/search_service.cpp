#include "vanta/workspace/search_service.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace vanta {
namespace {

std::string lowercase(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

int fileScore(const SearchIndexEntry& entry, const std::string& query) {
    if (query.empty()) {
        return 1;
    }
    const std::string name = lowercase(entry.name);
    const std::string target = lowercase(entry.file.toUri().string());
    const std::string needle = lowercase(query);
    if (name == needle) {
        return 100;
    }
    if (name.rfind(needle, 0) == 0) {
        return 80;
    }
    if (name.find(needle) != std::string::npos) {
        return 60;
    }
    if (target.find(needle) != std::string::npos) {
        return 30;
    }
    return 0;
}

std::string trimPreview(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = line.find_last_not_of(" \t");
    return line.substr(first, last - first + 1);
}

}

void SearchService::rebuild(const Workspace& workspace) {
    entries_.clear();
    if (workspace.isOpen()) {
        addTree(workspace.fileTree());
    }
}

void SearchService::clear() {
    entries_.clear();
}

std::vector<SearchHit> SearchService::searchFiles(const std::string& query, std::size_t limit) const {
    std::vector<SearchHit> hits;
    for (const SearchIndexEntry& entry : entries_) {
        const int score = fileScore(entry, query);
        if (score == 0) {
            continue;
        }
        hits.push_back({
            .file = entry.file,
            .line = 0,
            .column = 0,
            .preview = entry.name,
            .score = score,
        });
    }
    std::sort(hits.begin(), hits.end(), [](const SearchHit& left, const SearchHit& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.file.toUri().string() < right.file.toUri().string();
    });
    if (hits.size() > limit) {
        hits.resize(limit);
    }
    return hits;
}

std::vector<SearchHit> SearchService::searchText(const std::string& query, std::size_t limit) const {
    std::vector<SearchHit> hits;
    if (query.empty()) {
        return hits;
    }

    const std::string needle = lowercase(query);
    for (const SearchIndexEntry& entry : entries_) {
        if (entry.directory) {
            continue;
        }
        auto text = entry.file.readText();
        if (!text) {
            continue;
        }

        std::istringstream stream(*text);
        std::string line;
        int lineNumber = 1;
        while (std::getline(stream, line)) {
            const std::string loweredLine = lowercase(line);
            const std::size_t found = loweredLine.find(needle);
            if (found != std::string::npos) {
                hits.push_back({
                    .file = entry.file,
                    .line = lineNumber,
                    .column = static_cast<int>(found + 1),
                    .preview = trimPreview(line),
                    .score = 100,
                });
                if (hits.size() >= limit) {
                    return hits;
                }
            }
            ++lineNumber;
        }
    }
    return hits;
}

const std::vector<SearchIndexEntry>& SearchService::entries() const {
    return entries_;
}

void SearchService::addTree(const FileTreeNode& node) {
    entries_.push_back({
        .file = node.file,
        .name = node.name,
        .directory = node.directory,
    });
    for (const FileTreeNode& child : node.children) {
        addTree(child);
    }
}

}
