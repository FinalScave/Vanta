#include "vanta/vfs/virtual_file.h"

#include "vanta/vfs/virtual_file_system.h"

namespace vanta {

VirtualFile::VirtualFile(Uri uri, const VirtualFileSystem* vfs)
    : uri_(std::move(uri)), vfs_(vfs) {}

bool VirtualFile::valid() const noexcept {
    return vfs_ != nullptr && !uri_.empty();
}

const Uri& VirtualFile::uri() const noexcept {
    return uri_;
}

Uri VirtualFile::toUri() const {
    return uri_;
}

std::string VirtualFile::displayName() const {
    return uri_.filename();
}

std::string VirtualFile::extension() const {
    return uri_.extension();
}

bool VirtualFile::exists() const {
    return valid() && vfs_->exists(uri_);
}

FileStat VirtualFile::stat() const {
    return valid() ? vfs_->stat(uri_) : FileStat{};
}

std::optional<VirtualFile> VirtualFile::parent() const {
    if (!valid()) {
        return std::nullopt;
    }
    auto parentUri = vfs_->parent(uri_);
    if (!parentUri) {
        return std::nullopt;
    }
    return vfs_->file(*parentUri);
}

std::vector<VirtualFile> VirtualFile::listChildren() const {
    std::vector<VirtualFile> result;
    if (!valid()) {
        return result;
    }
    for (const Uri& child : vfs_->listChildren(uri_)) {
        result.push_back(vfs_->file(child));
    }
    return result;
}

std::optional<std::string> VirtualFile::readText() const {
    return valid() ? vfs_->readText(uri_) : std::nullopt;
}

bool VirtualFile::writeText(const std::string& text, std::string* errorMessage) const {
    if (!valid()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Virtual file is not valid";
        }
        return false;
    }
    return vfs_->writeText(uri_, text, errorMessage);
}

std::optional<std::filesystem::path> VirtualFile::localPath() const {
    return valid() ? vfs_->localPath(uri_) : std::nullopt;
}

bool VirtualFile::operator==(const VirtualFile& other) const noexcept {
    return uri_ == other.uri_;
}

bool VirtualFile::operator!=(const VirtualFile& other) const noexcept {
    return !(*this == other);
}

bool VirtualFile::operator<(const VirtualFile& other) const noexcept {
    return uri_ < other.uri_;
}

}
