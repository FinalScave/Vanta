#include "vanta/workspace/document_service.h"

#include <algorithm>

namespace vanta {

TextDocument* DocumentService::OpenDocument(const VirtualFile& file, std::string* error_message) {
    const Uri key = file.ToUri();
    auto existing = documents_.find(key);
    if (existing != documents_.end()) {
        return &existing->second;
    }

    auto text = file.ReadText();
    if (!text) {
        if (error_message != nullptr) {
            *error_message = "Document is not readable";
        }
        return nullptr;
    }

    TextDocument document;
    document.file = file;
    document.text = *text;
    auto [it, inserted] = documents_.emplace(key, std::move(document));
    (void)inserted;
    PublishChange(it->second, DocumentChangeKind::Opened);
    return &it->second;
}

bool DocumentService::CloseDocument(const VirtualFile& file) {
    const Uri key = file.ToUri();
    auto it = documents_.find(key);
    if (it == documents_.end()) {
        return false;
    }
    PublishChange(it->second, DocumentChangeKind::Closed);
    documents_.erase(it);
    return true;
}

const TextDocument* DocumentService::Document(const VirtualFile& file) const {
    auto it = documents_.find(file.ToUri());
    return it == documents_.end() ? nullptr : &it->second;
}

TextDocument* DocumentService::Document(const VirtualFile& file) {
    auto it = documents_.find(file.ToUri());
    return it == documents_.end() ? nullptr : &it->second;
}

std::optional<TextDocument> DocumentService::Snapshot(const VirtualFile& file) const {
    const TextDocument* found = Document(file);
    if (found == nullptr) {
        return std::nullopt;
    }
    return *found;
}

std::optional<DocumentSnapshot> DocumentService::ReadSnapshot(const VirtualFile& file) const {
    if (const TextDocument* found = Document(file)) {
        return DocumentSnapshot{
            .file = found->file,
            .text = found->text,
            .version = found->version,
            .open = true,
            .dirty = found->dirty,
        };
    }

    auto text = file.ReadText();
    if (!text) {
        return std::nullopt;
    }
    return DocumentSnapshot{
        .file = file,
        .text = *text,
        .version = 0,
        .open = false,
        .dirty = false,
    };
}

std::optional<std::string> DocumentService::ReadText(const VirtualFile& file) const {
    auto read = ReadSnapshot(file);
    if (!read) {
        return std::nullopt;
    }
    return read->text;
}

std::vector<VirtualFile> DocumentService::OpenDocuments() const {
    std::vector<VirtualFile> result;
    for (const auto& [uri, document] : documents_) {
        (void)uri;
        result.push_back(document.file);
    }
    return result;
}

bool DocumentService::SetText(const VirtualFile& file, std::string text, std::uint64_t expected_version, std::string* error_message) {
    TextDocument* found = Document(file);
    if (found == nullptr) {
        if (error_message != nullptr) {
            *error_message = "Document is not open";
        }
        return false;
    }
    if (expected_version != 0 && found->version != expected_version) {
        if (error_message != nullptr) {
            *error_message = "Document version changed";
        }
        return false;
    }

    found->text = std::move(text);
    ++found->version;
    found->dirty = true;
    PublishChange(*found, DocumentChangeKind::Changed);
    return true;
}

bool DocumentService::ApplyEdit(const VirtualFile& file, const TextEdit& edit, std::uint64_t expected_version, std::string* error_message) {
    TextDocument* found = Document(file);
    if (found == nullptr) {
        if (error_message != nullptr) {
            *error_message = "Document is not open";
        }
        return false;
    }
    return SetText(file, ApplyTextEdit(found->text, edit), expected_version, error_message);
}

bool DocumentService::ApplyEdits(const VirtualFile& file, const std::vector<TextEdit>& edits, std::uint64_t expected_version, std::string* error_message) {
    TextDocument* found = Document(file);
    if (found == nullptr) {
        if (error_message != nullptr) {
            *error_message = "Document is not open";
        }
        return false;
    }
    if (expected_version != 0 && found->version != expected_version) {
        if (error_message != nullptr) {
            *error_message = "Document version changed";
        }
        return false;
    }

    std::string text = found->text;
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        text = ApplyTextEdit(text, *it);
    }
    return SetText(file, std::move(text), expected_version, error_message);
}

bool DocumentService::SaveDocument(const VirtualFile& file, std::string* error_message) {
    TextDocument* found = Document(file);
    if (found == nullptr) {
        if (error_message != nullptr) {
            *error_message = "Document is not open";
        }
        return false;
    }
    if (!found->file.WriteText(found->text, error_message)) {
        return false;
    }
    found->dirty = false;
    PublishChange(*found, DocumentChangeKind::Saved);
    return true;
}

std::uint64_t DocumentService::OnDidChangeDocument(EventBus<DocumentChangeEvent>::Listener listener) {
    return on_did_change_.Subscribe(std::move(listener));
}

void DocumentService::RemoveDocumentListener(std::uint64_t listener_id) {
    on_did_change_.Unsubscribe(listener_id);
}

void DocumentService::PublishChange(const TextDocument& document, DocumentChangeKind kind) {
    on_did_change_.Publish({
        .file = document.file,
        .kind = kind,
        .version = document.version,
        .dirty = document.dirty,
    });
}

}
