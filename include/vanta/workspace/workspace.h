#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "vanta/vfs/virtual_file.h"
#include "vanta/vfs/virtual_file_system.h"

namespace vanta {

struct FileTreeNode {
    VirtualFile file;
    std::string name;
    bool directory = false;
    std::vector<FileTreeNode> children;
};

struct WorkspaceInfo {
    std::filesystem::path rootPath;
    std::string name;
};

class Workspace {
public:
    void bindFileSystem(const VirtualFileSystem& vfs);
    bool open(const std::filesystem::path& rootPath, std::string* errorMessage = nullptr);

    const WorkspaceInfo& info() const;
    const FileTreeNode& fileTree() const;
    bool isOpen() const;

    std::filesystem::path resolve(const std::filesystem::path& path) const;
    VirtualFile file(const std::filesystem::path& path) const;
    VirtualFile rootFile() const;
    std::optional<std::string> readTextFile(const std::filesystem::path& path) const;
    bool writeTextFile(const std::filesystem::path& path, const std::string& text, std::string* errorMessage = nullptr);
    void refreshFileTree();

private:
    FileTreeNode buildFileTree(const std::filesystem::path& rootPath) const;

    WorkspaceInfo info_;
    FileTreeNode fileTree_;
    const VirtualFileSystem* vfs_ = nullptr;
    bool open_ = false;
};

std::string renderFileTree(const FileTreeNode& node, std::size_t maxDepth = 4);

}
