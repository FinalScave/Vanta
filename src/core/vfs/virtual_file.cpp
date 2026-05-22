#include "vanta/vfs/virtual_file.h"

#include "vanta/vfs/virtual_file_system.h"

namespace vanta {

VirtualFile::VirtualFile(Uri uri, const VirtualFileSystem* vfs)
    : uri_(std::move(uri)), vfs_(vfs) {}

bool VirtualFile::Valid() const noexcept {
    return vfs_ != nullptr && !uri_.Empty();
}

const Uri& VirtualFile::UriValue() const noexcept {
    return uri_;
}

Uri VirtualFile::ToUri() const {
    return uri_;
}

std::string VirtualFile::DisplayName() const {
    return uri_.Filename();
}

std::string VirtualFile::Extension() const {
    return uri_.Extension();
}

bool VirtualFile::Exists() const {
    return Valid() && vfs_->Exists(uri_);
}

FileStat VirtualFile::Stat() const {
    return Valid() ? vfs_->Stat(uri_) : FileStat{};
}

std::optional<VirtualFile> VirtualFile::Parent() const {
    if (!Valid()) {
        return std::nullopt;
    }
    auto parent_uri = vfs_->Parent(uri_);
    if (!parent_uri) {
        return std::nullopt;
    }
    return vfs_->File(*parent_uri);
}

std::vector<VirtualFile> VirtualFile::ListChildren() const {
    std::vector<VirtualFile> result;
    if (!Valid()) {
        return result;
    }
    for (const Uri& child : vfs_->ListChildren(uri_)) {
        result.push_back(vfs_->File(child));
    }
    return result;
}

std::optional<std::string> VirtualFile::ReadText() const {
    return Valid() ? vfs_->ReadText(uri_) : std::nullopt;
}

bool VirtualFile::WriteText(const std::string& text, std::string* error_message) const {
    if (!Valid()) {
        if (error_message != nullptr) {
            *error_message = "Virtual file is not valid";
        }
        return false;
    }
    return vfs_->WriteText(uri_, text, error_message);
}

std::optional<std::filesystem::path> VirtualFile::LocalPath() const {
    return Valid() ? vfs_->LocalPath(uri_) : std::nullopt;
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
