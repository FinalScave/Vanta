#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/registration.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

class FileSystemProvider {
public:
    virtual ~FileSystemProvider() = default;

    virtual bool Exists(const Uri& uri) const = 0;
    virtual FileStat Stat(const Uri& uri) const = 0;
    virtual std::optional<Uri> Parent(const Uri& uri) const = 0;
    virtual std::vector<Uri> ListChildren(const Uri& uri) const = 0;
    virtual std::optional<std::string> ReadText(const Uri& uri) const = 0;
    virtual bool WriteText(const Uri& uri, const std::string& text, std::string* error_message = nullptr) const = 0;
    virtual std::optional<std::filesystem::path> LocalPath(const Uri& uri) const = 0;
};

class VirtualFileSystem {
public:
    static constexpr const char* kServiceId = "vanta.fileSystems";

    VirtualFileSystem();

    RegistrationHandle RegisterProvider(std::string scheme, std::unique_ptr<FileSystemProvider> provider);
    void RemoveProvider(const std::string& scheme);
    std::vector<std::string> ProviderSchemes() const;
    VirtualFile File(Uri uri) const;
    VirtualFile LocalFile(const std::filesystem::path& path) const;

    bool Exists(const Uri& uri) const;
    FileStat Stat(const Uri& uri) const;
    std::optional<Uri> Parent(const Uri& uri) const;
    std::vector<Uri> ListChildren(const Uri& uri) const;
    std::optional<std::string> ReadText(const Uri& uri) const;
    bool WriteText(const Uri& uri, const std::string& text, std::string* error_message = nullptr) const;
    std::optional<std::filesystem::path> LocalPath(const Uri& uri) const;

private:
    const FileSystemProvider* ProviderFor(const Uri& uri) const;

    std::map<std::string, std::unique_ptr<FileSystemProvider>> providers_;
};

class LocalFileSystemProvider final : public FileSystemProvider {
public:
    bool Exists(const Uri& uri) const override;
    FileStat Stat(const Uri& uri) const override;
    std::optional<Uri> Parent(const Uri& uri) const override;
    std::vector<Uri> ListChildren(const Uri& uri) const override;
    std::optional<std::string> ReadText(const Uri& uri) const override;
    bool WriteText(const Uri& uri, const std::string& text, std::string* error_message = nullptr) const override;
    std::optional<std::filesystem::path> LocalPath(const Uri& uri) const override;

private:
    static std::filesystem::path PathFromUri(const Uri& uri);
};

}
