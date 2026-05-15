#pragma once

#include <functional>
#include <memory>
#include <string>

#include "vanta/vfs/virtual_file.h"
#include "vanta/vfs/virtual_file_system.h"

namespace vanta {

enum class VirtualFileChangeKind {
    Created,
    Modified,
    Deleted,
};

struct VirtualFileChangeEvent {
    VirtualFile file;
    VirtualFileChangeKind kind = VirtualFileChangeKind::Modified;
};

using FileWatchCallback = std::function<void(const VirtualFileChangeEvent&)>;

class FileWatcher {
public:
    virtual ~FileWatcher() = default;

    virtual bool start(const VirtualFile& root, FileWatchCallback callback, std::string* errorMessage = nullptr) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
};

std::unique_ptr<FileWatcher> createPlatformFileWatcher(const VirtualFileSystem& vfs);
std::string toString(VirtualFileChangeKind kind);

}
