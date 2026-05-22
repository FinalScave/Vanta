#include "vanta/workspace/workspace.h"

#include <fstream>
#include <sstream>

namespace vanta {

void Workspace::BindFileSystem(const VirtualFileSystem& vfs) {
    vfs_ = &vfs;
}

bool Workspace::Open(const std::filesystem::path& root_path, std::string* error_message) {
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root_path, error);
    if (error || !std::filesystem::exists(canonical_root) || !std::filesystem::is_directory(canonical_root)) {
        if (error_message != nullptr) {
            *error_message = "Workspace path is not a readable directory";
        }
        return false;
    }

    info_.root_path = canonical_root;
    info_.name = canonical_root.filename().string();
    open_ = true;
    return true;
}

const WorkspaceInfo& Workspace::Info() const {
    return info_;
}

bool Workspace::IsOpen() const {
    return open_;
}

std::filesystem::path Workspace::Resolve(const std::filesystem::path& path) const {
    if (path.is_absolute()) {
        return path;
    }
    return info_.root_path / path;
}

VirtualFile Workspace::File(const std::filesystem::path& path) const {
    if (vfs_ == nullptr) {
        return {};
    }
    std::error_code error;
    const std::filesystem::path resolved = Resolve(path);
    const auto normalized = std::filesystem::weakly_canonical(resolved, error);
    return vfs_->LocalFile(error ? resolved : normalized);
}

VirtualFile Workspace::RootFile() const {
    if (vfs_ == nullptr) {
        return {};
    }
    return vfs_->LocalFile(info_.root_path);
}

std::optional<std::string> Workspace::ReadTextFile(const std::filesystem::path& path) const {
    std::ifstream input(Resolve(path));
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

bool Workspace::WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string* error_message) {
    const std::filesystem::path absolute_path = Resolve(path);
    std::error_code error;
    std::filesystem::create_directories(absolute_path.parent_path(), error);
    if (error) {
        if (error_message != nullptr) {
            *error_message = "Failed to create parent directory";
        }
        return false;
    }

    std::ofstream output(absolute_path);
    if (!output) {
        if (error_message != nullptr) {
            *error_message = "Failed to open file for writing";
        }
        return false;
    }
    output << text;
    return true;
}

}
