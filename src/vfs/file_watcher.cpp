#include "vanta/vfs/file_watcher.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <vector>

#if defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vanta {
namespace {

#if defined(__APPLE__)

class FseventsFileWatcher final : public FileWatcher {
public:
    explicit FseventsFileWatcher(const VirtualFileSystem& vfs) : vfs_(vfs) {}

    ~FseventsFileWatcher() override {
        stop();
    }

    bool start(const VirtualFile& root, FileWatchCallback callback, std::string* errorMessage) override {
        auto localPath = root.localPath();
        if (!localPath) {
            if (errorMessage != nullptr) {
                *errorMessage = "File watcher requires a local root";
            }
            return false;
        }
        if (running_.load()) {
            return true;
        }

        callback_ = std::move(callback);
        rootPath_ = *localPath;

        if (!createStream(errorMessage)) {
            return false;
        }
        running_ = true;
        if (!FSEventStreamStart(stream_)) {
            running_ = false;
            releaseStream();
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to start FSEvents stream";
            }
            return false;
        }
        return true;
    }

    void stop() override {
        if (!running_.load()) {
            return;
        }
        running_ = false;
        releaseStream();
    }

    bool running() const override {
        return running_.load();
    }

private:
    static void callback(
        ConstFSEventStreamRef,
        void* clientCallBackInfo,
        std::size_t numEvents,
        void* eventPaths,
        const FSEventStreamEventFlags eventFlags[],
        const FSEventStreamEventId[]) {
        auto* watcher = static_cast<FseventsFileWatcher*>(clientCallBackInfo);
        if (watcher == nullptr || !watcher->running_.load()) {
            return;
        }

        auto** paths = static_cast<char**>(eventPaths);
        for (std::size_t i = 0; i < numEvents; ++i) {
            watcher->publish(paths[i], eventFlags[i]);
        }
    }

    bool createStream(std::string* errorMessage) {
        const std::string root = rootPath_.string();
        CFStringRef rootString = CFStringCreateWithCString(nullptr, root.c_str(), kCFStringEncodingUTF8);
        if (rootString == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to create file watcher path";
            }
            return false;
        }
        CFArrayRef paths = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&rootString), 1, &kCFTypeArrayCallBacks);
        CFRelease(rootString);
        if (paths == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to create file watcher path list";
            }
            return false;
        }

        FSEventStreamContext context{};
        context.info = this;
        stream_ = FSEventStreamCreate(
            nullptr,
            &FseventsFileWatcher::callback,
            &context,
            paths,
            kFSEventStreamEventIdSinceNow,
            0.25,
            kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
        CFRelease(paths);

        if (stream_ == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = "Failed to create FSEvents stream";
            }
            return false;
        }

        queue_ = dispatch_queue_create("dev.vanta.file-watcher", DISPATCH_QUEUE_SERIAL);
        FSEventStreamSetDispatchQueue(stream_, queue_);
        return true;
    }

    void releaseStream() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stream_ != nullptr) {
            FSEventStreamStop(stream_);
            FSEventStreamInvalidate(stream_);
            FSEventStreamRelease(stream_);
            stream_ = nullptr;
        }
        if (queue_ != nullptr) {
            dispatch_release(queue_);
            queue_ = nullptr;
        }
    }

    void publish(const char* path, FSEventStreamEventFlags flags) const {
        if (callback_ == nullptr || path == nullptr) {
            return;
        }

        VirtualFileChangeKind kind = VirtualFileChangeKind::Modified;
        if ((flags & kFSEventStreamEventFlagItemCreated) != 0) {
            kind = VirtualFileChangeKind::Created;
        } else if ((flags & kFSEventStreamEventFlagItemRemoved) != 0) {
            kind = VirtualFileChangeKind::Deleted;
        }
        callback_({
            .file = vfs_.localFile(std::filesystem::path(path)),
            .kind = kind,
        });
    }

    const VirtualFileSystem& vfs_;
    FileWatchCallback callback_;
    std::filesystem::path rootPath_;
    mutable std::mutex mutex_;
    FSEventStreamRef stream_ = nullptr;
    dispatch_queue_t queue_ = nullptr;
    std::atomic_bool running_ = false;
};

class DispatchDirectoryFileWatcher final : public FileWatcher {
public:
    explicit DispatchDirectoryFileWatcher(const VirtualFileSystem& vfs) : vfs_(vfs) {}

    ~DispatchDirectoryFileWatcher() override {
        stop();
    }

    bool start(const VirtualFile& root, FileWatchCallback callback, std::string* errorMessage) override {
        auto localPath = root.localPath();
        if (!localPath) {
            if (errorMessage != nullptr) {
                *errorMessage = "Directory watcher requires a local root";
            }
            return false;
        }
        if (running_.load()) {
            return true;
        }

        callback_ = std::move(callback);
        rootPath_ = *localPath;
        queue_ = dispatch_queue_create("dev.vanta.directory-watcher", DISPATCH_QUEUE_SERIAL);
        running_ = true;
        registerTree(rootPath_);

        if (sources_.empty()) {
            running_ = false;
            releaseSources();
            if (errorMessage != nullptr) {
                *errorMessage = "No directories could be watched";
            }
            return false;
        }
        return true;
    }

    void stop() override {
        if (!running_.load() && sources_.empty()) {
            return;
        }
        running_ = false;
        releaseSources();
    }

    bool running() const override {
        return running_.load();
    }

private:
    struct WatchContext {
        DispatchDirectoryFileWatcher* watcher = nullptr;
        dispatch_source_t source = nullptr;
        int fd = -1;
        std::filesystem::path path;
    };

    static void handleEvent(void* value) {
        auto* context = static_cast<WatchContext*>(value);
        if (context == nullptr || context->watcher == nullptr || !context->watcher->running_.load()) {
            return;
        }

        const unsigned long flags = dispatch_source_get_data(context->source);
        VirtualFileChangeKind kind = VirtualFileChangeKind::Modified;
        if ((flags & DISPATCH_VNODE_DELETE) != 0 || (flags & DISPATCH_VNODE_RENAME) != 0) {
            kind = VirtualFileChangeKind::Deleted;
        }
        context->watcher->publish(context->path, kind);
    }

    static void handleCancel(void* value) {
        auto* context = static_cast<WatchContext*>(value);
        if (context == nullptr) {
            return;
        }
        if (context->fd >= 0) {
            close(context->fd);
            context->fd = -1;
        }
        delete context;
    }

    void registerTree(const std::filesystem::path& root) {
        registerDirectory(root);
        std::error_code error;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
            if (error) {
                break;
            }
            std::error_code statusError;
            if (entry.is_directory(statusError) && !statusError) {
                registerDirectory(entry.path());
            }
        }
    }

    void registerDirectory(const std::filesystem::path& path) {
        const int fd = open(path.c_str(), O_EVTONLY);
        if (fd < 0) {
            return;
        }

        const unsigned long mask = DISPATCH_VNODE_WRITE | DISPATCH_VNODE_DELETE | DISPATCH_VNODE_RENAME |
                                   DISPATCH_VNODE_EXTEND | DISPATCH_VNODE_ATTRIB | DISPATCH_VNODE_LINK;
        dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE, static_cast<uintptr_t>(fd), mask, queue_);
        if (source == nullptr) {
            close(fd);
            return;
        }

        auto* context = new WatchContext{this, source, fd, path};
        dispatch_set_context(source, context);
        dispatch_source_set_event_handler_f(source, &DispatchDirectoryFileWatcher::handleEvent);
        dispatch_source_set_cancel_handler_f(source, &DispatchDirectoryFileWatcher::handleCancel);
        sources_.push_back(source);
        dispatch_resume(source);
    }

    void releaseSources() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (dispatch_source_t source : sources_) {
            dispatch_source_cancel(source);
            dispatch_release(source);
        }
        sources_.clear();
        if (queue_ != nullptr) {
            dispatch_release(queue_);
            queue_ = nullptr;
        }
    }

    void publish(const std::filesystem::path& path, VirtualFileChangeKind kind) const {
        if (callback_ == nullptr) {
            return;
        }
        callback_({
            .file = vfs_.localFile(path),
            .kind = kind,
        });
    }

    const VirtualFileSystem& vfs_;
    FileWatchCallback callback_;
    std::filesystem::path rootPath_;
    mutable std::mutex mutex_;
    dispatch_queue_t queue_ = nullptr;
    std::vector<dispatch_source_t> sources_;
    std::atomic_bool running_ = false;
};

class MacFileWatcher final : public FileWatcher {
public:
    explicit MacFileWatcher(const VirtualFileSystem& vfs)
        : fsevents_(vfs), directories_(vfs) {}

    bool start(const VirtualFile& root, FileWatchCallback callback, std::string* errorMessage) override {
        std::string fseventsError;
        FileWatchCallback fallbackCallback = callback;
        if (fsevents_.start(root, std::move(callback), &fseventsError)) {
            active_ = &fsevents_;
            return true;
        }

        std::string directoryError;
        if (directories_.start(root, std::move(fallbackCallback), &directoryError)) {
            active_ = &directories_;
            return true;
        }

        if (errorMessage != nullptr) {
            *errorMessage = fseventsError.empty() ? directoryError : fseventsError + "; " + directoryError;
        }
        return false;
    }

    void stop() override {
        if (active_ != nullptr) {
            active_->stop();
            active_ = nullptr;
        }
    }

    bool running() const override {
        return active_ != nullptr && active_->running();
    }

private:
    FseventsFileWatcher fsevents_;
    DispatchDirectoryFileWatcher directories_;
    FileWatcher* active_ = nullptr;
};

#else

class UnsupportedFileWatcher final : public FileWatcher {
public:
    bool start(const VirtualFile&, FileWatchCallback, std::string* errorMessage) override {
        if (errorMessage != nullptr) {
            *errorMessage = "Platform file watcher is not implemented";
        }
        return false;
    }

    void stop() override {}
    bool running() const override { return false; }
};

#endif

}

std::unique_ptr<FileWatcher> createPlatformFileWatcher(const VirtualFileSystem& vfs) {
#if defined(__APPLE__)
    return std::make_unique<MacFileWatcher>(vfs);
#else
    (void)vfs;
    return std::make_unique<UnsupportedFileWatcher>();
#endif
}

std::string toString(VirtualFileChangeKind kind) {
    switch (kind) {
    case VirtualFileChangeKind::Created:
        return "created";
    case VirtualFileChangeKind::Modified:
        return "modified";
    case VirtualFileChangeKind::Deleted:
        return "deleted";
    }
    return "modified";
}

}
