#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "vanta/vfs/uri.h"

namespace vanta {

class VirtualFileSystem;

enum class VirtualFileKind {
    Unknown,
    File,
    Directory,
};

struct FileStat {
    VirtualFileKind kind = VirtualFileKind::Unknown;
    std::uint64_t size = 0;
};

class VirtualFile {
public:
    VirtualFile() = default;
    VirtualFile(Uri uri, const VirtualFileSystem* vfs);

    bool valid() const noexcept;
    const Uri& uri() const noexcept;
    Uri toUri() const;
    std::string displayName() const;
    std::string extension() const;

    bool exists() const;
    FileStat stat() const;
    std::optional<VirtualFile> parent() const;
    std::vector<VirtualFile> listChildren() const;
    std::optional<std::string> readText() const;
    bool writeText(const std::string& text, std::string* errorMessage = nullptr) const;
    std::optional<std::filesystem::path> localPath() const;

    bool operator==(const VirtualFile& other) const noexcept;
    bool operator!=(const VirtualFile& other) const noexcept;
    bool operator<(const VirtualFile& other) const noexcept;

private:
    Uri uri_;
    const VirtualFileSystem* vfs_ = nullptr;
};

}
