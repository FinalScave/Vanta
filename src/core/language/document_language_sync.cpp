#include "vanta/language/document_language_sync.h"

namespace vanta {

DocumentLanguageSynchronizer::DocumentLanguageSynchronizer(DocumentService& documents, LanguageRegistry& languages)
    : documents_(documents), languages_(languages) {}

DocumentLanguageSynchronizer::~DocumentLanguageSynchronizer() {
    Stop();
}

void DocumentLanguageSynchronizer::Start() {
    if (listener_id_ != 0) {
        return;
    }
    listener_id_ = documents_.OnDidChangeDocument([this](const DocumentChangeEvent& event) {
        HandleChange(event);
    });
}

void DocumentLanguageSynchronizer::Stop() {
    if (listener_id_ == 0) {
        return;
    }
    documents_.RemoveDocumentListener(listener_id_);
    listener_id_ = 0;
}

void DocumentLanguageSynchronizer::HandleChange(const DocumentChangeEvent& event) {
    LanguageService* service = languages_.ServiceForDocument(event.file);
    if (service == nullptr) {
        return;
    }

    if (event.kind == DocumentChangeKind::Closed) {
        service->DidClose(event.file);
        return;
    }

    auto snapshot = documents_.Snapshot(event.file);
    if (!snapshot) {
        return;
    }

    switch (event.kind) {
    case DocumentChangeKind::Opened:
        service->DidOpen(*snapshot);
        break;
    case DocumentChangeKind::Changed:
        service->DidChange(*snapshot);
        break;
    case DocumentChangeKind::Saved:
        service->DidSave(*snapshot);
        break;
    case DocumentChangeKind::Closed:
        break;
    }
}

}
