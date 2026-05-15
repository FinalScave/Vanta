#include "vanta/workspace/workspace.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace vanta {
namespace {

bool shouldSkipDirectory(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return name == ".git" || name == ".vanta" || name == "build" || name == ".cache";
}

std::string indent(std::size_t depth) {
    return std::string(depth * 2, ' ');
}

void renderNode(const FileTreeNode& node, std::ostringstream& stream, std::size_t depth, std::size_t maxDepth) {
    stream << indent(depth) << (node.directory ? "[D] " : "[F] ") << node.name << '\n';
    if (depth >= maxDepth) {
        return;
    }
    for (const FileTreeNode& child : node.children) {
        renderNode(child, stream, depth + 1, maxDepth);
    }
}

}

void Workspace::bindFileSystem(const VirtualFileSystem& vfs) {
    vfs_ = &vfs;
}

bool Workspace::open(const std::filesystem::path& rootPath, std::string* errorMessage) {
    std::error_code error;
    const auto canonicalRoot = std::filesystem::weakly_canonical(rootPath, error);
    if (error || !std::filesystem::exists(canonicalRoot) || !std::filesystem::is_directory(canonicalRoot)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Workspace path is not a readable directory";
        }
        return false;
    }

    info_.rootPath = canonicalRoot;
    info_.name = canonicalRoot.filename().string();
    refreshFileTree();
    open_ = true;
    return true;
}

const WorkspaceInfo& Workspace::info() const {
    return info_;
}

const FileTreeNode& Workspace::fileTree() const {
    return fileTree_;
}

bool Workspace::isOpen() const {
    return open_;
}

std::filesystem::path Workspace::resolve(const std::filesystem::path& path) const {
    if (path.is_absolute()) {
        return path;
    }
    return info_.rootPath / path;
}

VirtualFile Workspace::file(const std::filesystem::path& path) const {
    if (vfs_ == nullptr) {
        return {};
    }
    std::error_code error;
    const std::filesystem::path resolved = resolve(path);
    const auto normalized = std::filesystem::weakly_canonical(resolved, error);
    return vfs_->localFile(error ? resolved : normalized);
}

VirtualFile Workspace::rootFile() const {
    if (vfs_ == nullptr) {
        return {};
    }
    return vfs_->localFile(info_.rootPath);
}

std::optional<std::string> Workspace::readTextFile(const std::filesystem::path& path) const {
    std::ifstream input(resolve(path));
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

bool Workspace::writeTextFile(const std::filesystem::path& path, const std::string& text, std::string* errorMessage) {
    const std::filesystem::path absolutePath = resolve(path);
    std::error_code error;
    std::filesystem::create_directories(absolutePath.parent_path(), error);
    if (error) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create parent directory";
        }
        return false;
    }

    std::ofstream output(absolutePath);
    if (!output) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to open file for writing";
        }
        return false;
    }
    output << text;
    refreshFileTree();
    return true;
}

void Workspace::refreshFileTree() {
    fileTree_ = buildFileTree(info_.rootPath);
}

FileTreeNode Workspace::buildFileTree(const std::filesystem::path& rootPath) const {
    FileTreeNode node;
    node.file = vfs_ == nullptr ? VirtualFile{} : vfs_->localFile(rootPath);
    node.name = rootPath.filename().empty() ? rootPath.string() : rootPath.filename().string();
    node.directory = std::filesystem::is_directory(rootPath);

    if (!node.directory || shouldSkipDirectory(rootPath)) {
        return node;
    }

    std::vector<std::filesystem::directory_entry> entries;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(rootPath, error)) {
        if (!error) {
            entries.push_back(entry);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        const bool leftDir = left.is_directory();
        const bool rightDir = right.is_directory();
        if (leftDir != rightDir) {
            return leftDir > rightDir;
        }
        return left.path().filename().string() < right.path().filename().string();
    });

    for (const auto& entry : entries) {
        if (entry.is_directory() && shouldSkipDirectory(entry.path())) {
            continue;
        }
        node.children.push_back(buildFileTree(entry.path()));
    }

    return node;
}

std::string renderFileTree(const FileTreeNode& node, std::size_t maxDepth) {
    std::ostringstream stream;
    renderNode(node, stream, 0, maxDepth);
    return stream.str();
}

}
