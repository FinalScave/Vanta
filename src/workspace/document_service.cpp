#include "vanta/workspace/document_service.h"

#include <algorithm>

namespace vanta {

TextDocument* DocumentService::openDocument(const VirtualFile& file, std::string* errorMessage) {
    const Uri key = file.toUri();
    auto existing = documents_.find(key);
    if (existing != documents_.end()) {
        return &existing->second;
    }

    auto text = file.readText();
    if (!text) {
        if (errorMessage != nullptr) {
            *errorMessage = "Document is not readable";
        }
        return nullptr;
    }

    TextDocument document;
    document.file = file;
    document.text = *text;
    auto [it, inserted] = documents_.emplace(key, std::move(document));
    (void)inserted;
    publishChange(it->second, DocumentChangeKind::Opened);
    return &it->second;
}

bool DocumentService::closeDocument(const VirtualFile& file) {
    const Uri key = file.toUri();
    auto it = documents_.find(key);
    if (it == documents_.end()) {
        return false;
    }
    publishChange(it->second, DocumentChangeKind::Closed);
    documents_.erase(it);
    return true;
}

const TextDocument* DocumentService::document(const VirtualFile& file) const {
    auto it = documents_.find(file.toUri());
    return it == documents_.end() ? nullptr : &it->second;
}

TextDocument* DocumentService::document(const VirtualFile& file) {
    auto it = documents_.find(file.toUri());
    return it == documents_.end() ? nullptr : &it->second;
}

std::optional<TextDocument> DocumentService::snapshot(const VirtualFile& file) const {
    const TextDocument* found = document(file);
    if (found == nullptr) {
        return std::nullopt;
    }
    return *found;
}

std::optional<DocumentSnapshot> DocumentService::readSnapshot(const VirtualFile& file) const {
    if (const TextDocument* found = document(file)) {
        return DocumentSnapshot{
            .file = found->file,
            .text = found->text,
            .version = found->version,
            .open = true,
            .dirty = found->dirty,
        };
    }

    auto text = file.readText();
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

std::optional<std::string> DocumentService::readText(const VirtualFile& file) const {
    auto read = readSnapshot(file);
    if (!read) {
        return std::nullopt;
    }
    return read->text;
}

std::vector<VirtualFile> DocumentService::openDocuments() const {
    std::vector<VirtualFile> result;
    for (const auto& [uri, document] : documents_) {
        (void)uri;
        result.push_back(document.file);
    }
    return result;
}

bool DocumentService::setText(const VirtualFile& file, std::string text, std::uint64_t expectedVersion, std::string* errorMessage) {
    TextDocument* found = document(file);
    if (found == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Document is not open";
        }
        return false;
    }
    if (expectedVersion != 0 && found->version != expectedVersion) {
        if (errorMessage != nullptr) {
            *errorMessage = "Document version changed";
        }
        return false;
    }

    found->text = std::move(text);
    ++found->version;
    found->dirty = true;
    publishChange(*found, DocumentChangeKind::Changed);
    return true;
}

bool DocumentService::applyEdit(const VirtualFile& file, const TextEdit& edit, std::uint64_t expectedVersion, std::string* errorMessage) {
    TextDocument* found = document(file);
    if (found == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Document is not open";
        }
        return false;
    }
    return setText(file, applyTextEdit(found->text, edit), expectedVersion, errorMessage);
}

bool DocumentService::applyEdits(const VirtualFile& file, const std::vector<TextEdit>& edits, std::uint64_t expectedVersion, std::string* errorMessage) {
    TextDocument* found = document(file);
    if (found == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Document is not open";
        }
        return false;
    }
    if (expectedVersion != 0 && found->version != expectedVersion) {
        if (errorMessage != nullptr) {
            *errorMessage = "Document version changed";
        }
        return false;
    }

    std::string text = found->text;
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        text = applyTextEdit(text, *it);
    }
    return setText(file, std::move(text), expectedVersion, errorMessage);
}

bool DocumentService::saveDocument(const VirtualFile& file, std::string* errorMessage) {
    TextDocument* found = document(file);
    if (found == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Document is not open";
        }
        return false;
    }
    if (!found->file.writeText(found->text, errorMessage)) {
        return false;
    }
    found->dirty = false;
    publishChange(*found, DocumentChangeKind::Saved);
    return true;
}

std::uint64_t DocumentService::onDidChangeDocument(EventBus<DocumentChangeEvent>::Listener listener) {
    return onDidChange_.subscribe(std::move(listener));
}

void DocumentService::removeDocumentListener(std::uint64_t listenerId) {
    onDidChange_.unsubscribe(listenerId);
}

void DocumentService::publishChange(const TextDocument& document, DocumentChangeKind kind) {
    onDidChange_.publish({
        .file = document.file,
        .kind = kind,
        .version = document.version,
        .dirty = document.dirty,
    });
}

}
