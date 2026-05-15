#include "vanta/vfs/virtual_file_system.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace vanta {

VirtualFileSystem::VirtualFileSystem() {
    registerProvider("file", std::make_unique<LocalFileSystemProvider>());
}

void VirtualFileSystem::registerProvider(std::string scheme, std::unique_ptr<FileSystemProvider> provider) {
    providers_[std::move(scheme)] = std::move(provider);
}

VirtualFile VirtualFileSystem::file(Uri uri) const {
    return VirtualFile(std::move(uri), this);
}

VirtualFile VirtualFileSystem::localFile(const std::filesystem::path& path) const {
    return file(Uri::fromLocalPath(path));
}

bool VirtualFileSystem::exists(const Uri& uri) const {
    const FileSystemProvider* provider = providerFor(uri);
    return provider != nullptr && provider->exists(uri);
}

FileStat VirtualFileSystem::stat(const Uri& uri) const {
    const FileSystemProvider* provider = providerFor(uri);
    return provider == nullptr ? FileStat{} : provider->stat(uri);
}

std::optional<Uri> VirtualFileSystem::parent(const Uri& uri) const {
    const FileSystemProvider* provider = providerFor(uri);
    return provider == nullptr ? std::nullopt : provider->parent(uri);
}

std::vector<Uri> VirtualFileSystem::listChildren(const Uri& uri) const {
    const FileSystemProvider* provider = providerFor(uri);
    return provider == nullptr ? std::vector<Uri>{} : provider->listChildren(uri);
}

std::optional<std::string> VirtualFileSystem::readText(const Uri& uri) const {
    const FileSystemProvider* provider = providerFor(uri);
    return provider == nullptr ? std::nullopt : provider->readText(uri);
}

bool VirtualFileSystem::writeText(const Uri& uri, const std::string& text, std::string* errorMessage) const {
    const FileSystemProvider* provider = providerFor(uri);
    if (provider == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "No virtual file system provider is registered for URI scheme";
        }
        return false;
    }
    return provider->writeText(uri, text, errorMessage);
}

std::optional<std::filesystem::path> VirtualFileSystem::localPath(const Uri& uri) const {
    const FileSystemProvider* provider = providerFor(uri);
    return provider == nullptr ? std::nullopt : provider->localPath(uri);
}

const FileSystemProvider* VirtualFileSystem::providerFor(const Uri& uri) const {
    auto it = providers_.find(uri.scheme());
    return it == providers_.end() ? nullptr : it->second.get();
}

bool LocalFileSystemProvider::exists(const Uri& uri) const {
    return std::filesystem::exists(pathFromUri(uri));
}

FileStat LocalFileSystemProvider::stat(const Uri& uri) const {
    std::error_code error;
    const std::filesystem::path path = pathFromUri(uri);
    FileStat stat;
    if (std::filesystem::is_regular_file(path, error)) {
        stat.kind = VirtualFileKind::File;
        stat.size = std::filesystem::file_size(path, error);
    } else if (std::filesystem::is_directory(path, error)) {
        stat.kind = VirtualFileKind::Directory;
    }
    return stat;
}

std::optional<Uri> LocalFileSystemProvider::parent(const Uri& uri) const {
    const std::filesystem::path parentPath = pathFromUri(uri).parent_path();
    if (parentPath.empty()) {
        return std::nullopt;
    }
    return Uri::fromLocalPath(parentPath);
}

std::vector<Uri> LocalFileSystemProvider::listChildren(const Uri& uri) const {
    std::vector<Uri> result;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(pathFromUri(uri), error)) {
        if (!error) {
            result.push_back(Uri::fromLocalPath(entry.path()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<std::string> LocalFileSystemProvider::readText(const Uri& uri) const {
    std::ifstream input(pathFromUri(uri));
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

bool LocalFileSystemProvider::writeText(const Uri& uri, const std::string& text, std::string* errorMessage) const {
    const std::filesystem::path path = pathFromUri(uri);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to create parent directory";
        }
        return false;
    }
    std::ofstream output(path);
    if (!output) {
        if (errorMessage != nullptr) {
            *errorMessage = "Failed to open file for writing";
        }
        return false;
    }
    output << text;
    return true;
}

std::optional<std::filesystem::path> LocalFileSystemProvider::localPath(const Uri& uri) const {
    return pathFromUri(uri);
}

std::filesystem::path LocalFileSystemProvider::pathFromUri(const Uri& uri) {
    return uri.path();
}

}
