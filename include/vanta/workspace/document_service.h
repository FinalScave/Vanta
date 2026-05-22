#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vanta/core/event.h"
#include "vanta/core/text.h"
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
    static constexpr const char* kServiceId = "vanta.documents";

    TextDocument* OpenDocument(const VirtualFile& file, std::string* error_message = nullptr);
    bool CloseDocument(const VirtualFile& file);
    const TextDocument* Document(const VirtualFile& file) const;
    TextDocument* Document(const VirtualFile& file);
    std::optional<TextDocument> Snapshot(const VirtualFile& file) const;
    std::optional<DocumentSnapshot> ReadSnapshot(const VirtualFile& file) const;
    std::optional<std::string> ReadText(const VirtualFile& file) const;
    std::vector<VirtualFile> OpenDocuments() const;

    bool SetText(const VirtualFile& file, std::string text, std::uint64_t expected_version, std::string* error_message = nullptr);
    bool ApplyEdit(const VirtualFile& file, const TextEdit& edit, std::uint64_t expected_version, std::string* error_message = nullptr);
    bool ApplyEdits(const VirtualFile& file, const std::vector<TextEdit>& edits, std::uint64_t expected_version, std::string* error_message = nullptr);
    bool SaveDocument(const VirtualFile& file, std::string* error_message = nullptr);

    std::uint64_t OnDidChangeDocument(EventBus<DocumentChangeEvent>::Listener listener);
    void RemoveDocumentListener(std::uint64_t listener_id);

private:
    void PublishChange(const TextDocument& document, DocumentChangeKind kind);

    std::map<Uri, TextDocument> documents_;
    EventBus<DocumentChangeEvent> on_did_change_;
};

}
