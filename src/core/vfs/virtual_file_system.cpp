#include "vanta/vfs/virtual_file_system.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace vanta {

VirtualFileSystem::VirtualFileSystem() {
    providers_["file"] = std::make_unique<LocalFileSystemProvider>();
}

RegistrationHandle VirtualFileSystem::RegisterProvider(std::string scheme, std::unique_ptr<FileSystemProvider> provider) {
    if (scheme.empty() || scheme == "file" || provider == nullptr) {
        return {};
    }
    const std::string provider_scheme = scheme;
    providers_[std::move(scheme)] = std::move(provider);
    return RegistrationHandle([this, provider_scheme] {
        RemoveProvider(provider_scheme);
    });
}

void VirtualFileSystem::RemoveProvider(const std::string& scheme) {
    if (scheme == "file") {
        return;
    }
    providers_.erase(scheme);
}

std::vector<std::string> VirtualFileSystem::ProviderSchemes() const {
    std::vector<std::string> schemes;
    for (const auto& [scheme, provider] : providers_) {
        (void)provider;
        schemes.push_back(scheme);
    }
    return schemes;
}

VirtualFile VirtualFileSystem::File(Uri uri) const {
    return VirtualFile(std::move(uri), this);
}

VirtualFile VirtualFileSystem::LocalFile(const std::filesystem::path& path) const {
    return File(Uri::FromLocalPath(path));
}

bool VirtualFileSystem::Exists(const Uri& uri) const {
    const FileSystemProvider* provider = ProviderFor(uri);
    return provider != nullptr && provider->Exists(uri);
}

FileStat VirtualFileSystem::Stat(const Uri& uri) const {
    const FileSystemProvider* provider = ProviderFor(uri);
    return provider == nullptr ? FileStat{} : provider->Stat(uri);
}

std::optional<Uri> VirtualFileSystem::Parent(const Uri& uri) const {
    const FileSystemProvider* provider = ProviderFor(uri);
    return provider == nullptr ? std::nullopt : provider->Parent(uri);
}

std::vector<Uri> VirtualFileSystem::ListChildren(const Uri& uri) const {
    const FileSystemProvider* provider = ProviderFor(uri);
    return provider == nullptr ? std::vector<Uri>{} : provider->ListChildren(uri);
}

std::optional<std::string> VirtualFileSystem::ReadText(const Uri& uri) const {
    const FileSystemProvider* provider = ProviderFor(uri);
    return provider == nullptr ? std::nullopt : provider->ReadText(uri);
}

bool VirtualFileSystem::WriteText(const Uri& uri, const std::string& text, std::string* error_message) const {
    const FileSystemProvider* provider = ProviderFor(uri);
    if (provider == nullptr) {
        if (error_message != nullptr) {
            *error_message = "No virtual file system provider is registered for URI scheme";
        }
        return false;
    }
    return provider->WriteText(uri, text, error_message);
}

std::optional<std::filesystem::path> VirtualFileSystem::LocalPath(const Uri& uri) const {
    const FileSystemProvider* provider = ProviderFor(uri);
    return provider == nullptr ? std::nullopt : provider->LocalPath(uri);
}

const FileSystemProvider* VirtualFileSystem::ProviderFor(const Uri& uri) const {
    auto it = providers_.find(uri.Scheme());
    return it == providers_.end() ? nullptr : it->second.get();
}

bool LocalFileSystemProvider::Exists(const Uri& uri) const {
    return std::filesystem::exists(PathFromUri(uri));
}

FileStat LocalFileSystemProvider::Stat(const Uri& uri) const {
    std::error_code error;
    const std::filesystem::path path = PathFromUri(uri);
    FileStat stat;
    if (std::filesystem::is_regular_file(path, error)) {
        stat.kind = VirtualFileKind::File;
        stat.size = std::filesystem::file_size(path, error);
    } else if (std::filesystem::is_directory(path, error)) {
        stat.kind = VirtualFileKind::Directory;
    }
    return stat;
}

std::optional<Uri> LocalFileSystemProvider::Parent(const Uri& uri) const {
    const std::filesystem::path parent_path = PathFromUri(uri).parent_path();
    if (parent_path.empty()) {
        return std::nullopt;
    }
    return Uri::FromLocalPath(parent_path);
}

std::vector<Uri> LocalFileSystemProvider::ListChildren(const Uri& uri) const {
    std::vector<Uri> result;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(PathFromUri(uri), error)) {
        if (!error) {
            result.push_back(Uri::FromLocalPath(entry.path()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<std::string> LocalFileSystemProvider::ReadText(const Uri& uri) const {
    std::ifstream input(PathFromUri(uri));
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

bool LocalFileSystemProvider::WriteText(const Uri& uri, const std::string& text, std::string* error_message) const {
    const std::filesystem::path path = PathFromUri(uri);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (error_message != nullptr) {
            *error_message = "Failed to create parent directory";
        }
        return false;
    }
    std::ofstream output(path);
    if (!output) {
        if (error_message != nullptr) {
            *error_message = "Failed to open file for writing";
        }
        return false;
    }
    output << text;
    return true;
}

std::optional<std::filesystem::path> LocalFileSystemProvider::LocalPath(const Uri& uri) const {
    return PathFromUri(uri);
}

std::filesystem::path LocalFileSystemProvider::PathFromUri(const Uri& uri) {
    return uri.Path();
}

}
