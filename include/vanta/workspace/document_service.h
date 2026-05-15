#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/text.h"
#include "vanta/platform/async.h"
#include "vanta/vfs/virtual_file.h"

namespace vanta {

struct TextDocument {
    VirtualFile file;
    std::string text;
    std::uint64_t version = 1;
    bool dirty = false;
};

struct DocumentSnapshot {
    VirtualFile file;
    std::string text;
    std::uint64_t version = 0;
    bool open = false;
    bool dirty = false;
};

enum class DocumentChangeKind {
    Opened,
    Changed,
    Saved,
    Closed,
};

struct DocumentChangeEvent {
    VirtualFile file;
    DocumentChangeKind kind = DocumentChangeKind::Changed;
    std::uint64_t version = 0;
    bool dirty = false;
};

class DocumentService {
public:
    TextDocument* openDocument(const VirtualFile& file, std::string* errorMessage = nullptr);
    bool closeDocument(const VirtualFile& file);
    const TextDocument* document(const VirtualFile& file) const;
    TextDocument* document(const VirtualFile& file);
    std::optional<TextDocument> snapshot(const VirtualFile& file) const;
    std::optional<DocumentSnapshot> readSnapshot(const VirtualFile& file) const;
    std::optional<std::string> readText(const VirtualFile& file) const;
    std::vector<VirtualFile> openDocuments() const;

    bool setText(const VirtualFile& file, std::string text, std::uint64_t expectedVersion, std::string* errorMessage = nullptr);
    bool applyEdit(const VirtualFile& file, const TextEdit& edit, std::uint64_t expectedVersion, std::string* errorMessage = nullptr);
    bool applyEdits(const VirtualFile& file, const std::vector<TextEdit>& edits, std::uint64_t expectedVersion, std::string* errorMessage = nullptr);
    bool saveDocument(const VirtualFile& file, std::string* errorMessage = nullptr);

    std::uint64_t onDidChangeDocument(EventBus<DocumentChangeEvent>::Listener listener);
    void removeDocumentListener(std::uint64_t listenerId);

private:
    void publishChange(const TextDocument& document, DocumentChangeKind kind);

    std::map<Uri, TextDocument> documents_;
    EventBus<DocumentChangeEvent> onDidChange_;
};

}
