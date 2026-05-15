#include "vanta/language/document_language_sync.h"

namespace vanta {

DocumentLanguageSynchronizer::DocumentLanguageSynchronizer(DocumentService& documents, DefaultLanguageRegistry& languages)
    : documents_(documents), languages_(languages) {}

DocumentLanguageSynchronizer::~DocumentLanguageSynchronizer() {
    stop();
}

void DocumentLanguageSynchronizer::start() {
    if (listenerId_ != 0) {
        return;
    }
    listenerId_ = documents_.onDidChangeDocument([this](const DocumentChangeEvent& event) {
        handleChange(event);
    });
}

void DocumentLanguageSynchronizer::stop() {
    if (listenerId_ == 0) {
        return;
    }
    documents_.removeDocumentListener(listenerId_);
    listenerId_ = 0;
}

void DocumentLanguageSynchronizer::handleChange(const DocumentChangeEvent& event) {
    LanguageService* service = languages_.serviceForDocument(event.file);
    if (service == nullptr) {
        return;
    }

    if (event.kind == DocumentChangeKind::Closed) {
        service->didClose(event.file);
        return;
    }

    auto snapshot = documents_.snapshot(event.file);
    if (!snapshot) {
        return;
    }

    switch (event.kind) {
    case DocumentChangeKind::Opened:
        service->didOpen(*snapshot);
        break;
    case DocumentChangeKind::Changed:
        service->didChange(*snapshot);
        break;
    case DocumentChangeKind::Saved:
        service->didSave(*snapshot);
        break;
    case DocumentChangeKind::Closed:
        break;
    }
}

}
