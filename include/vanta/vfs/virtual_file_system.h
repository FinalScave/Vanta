#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/vfs/virtual_file.h"

namespace vanta {

class FileSystemProvider {
public:
    virtual ~FileSystemProvider() = default;

    virtual bool exists(const Uri& uri) const = 0;
    virtual FileStat stat(const Uri& uri) const = 0;
    virtual std::optional<Uri> parent(const Uri& uri) const = 0;
    virtual std::vector<Uri> listChildren(const Uri& uri) const = 0;
    virtual std::optional<std::string> readText(const Uri& uri) const = 0;
    virtual bool writeText(const Uri& uri, const std::string& text, std::string* errorMessage = nullptr) const = 0;
    virtual std::optional<std::filesystem::path> localPath(const Uri& uri) const = 0;
};

class VirtualFileSystem {
public:
    VirtualFileSystem();

    void registerProvider(std::string scheme, std::unique_ptr<FileSystemProvider> provider);
    VirtualFile file(Uri uri) const;
    VirtualFile localFile(const std::filesystem::path& path) const;

    bool exists(const Uri& uri) const;
    FileStat stat(const Uri& uri) const;
    std::optional<Uri> parent(const Uri& uri) const;
    std::vector<Uri> listChildren(const Uri& uri) const;
    std::optional<std::string> readText(const Uri& uri) const;
    bool writeText(const Uri& uri, const std::string& text, std::string* errorMessage = nullptr) const;
    std::optional<std::filesystem::path> localPath(const Uri& uri) const;

private:
    const FileSystemProvider* providerFor(const Uri& uri) const;

    std::map<std::string, std::unique_ptr<FileSystemProvider>> providers_;
};

class LocalFileSystemProvider final : public FileSystemProvider {
public:
    bool exists(const Uri& uri) const override;
    FileStat stat(const Uri& uri) const override;
    std::optional<Uri> parent(const Uri& uri) const override;
    std::vector<Uri> listChildren(const Uri& uri) const override;
    std::optional<std::string> readText(const Uri& uri) const override;
    bool writeText(const Uri& uri, const std::string& text, std::string* errorMessage = nullptr) const override;
    std::optional<std::filesystem::path> localPath(const Uri& uri) const override;

private:
    static std::filesystem::path pathFromUri(const Uri& uri);
};

}
