#pragma once

#include <cstdint>

#include "vanta/workspace/document_service.h"
#include "vanta/language/language_service.h"

namespace vanta {

class DocumentLanguageSynchronizer {
public:
    DocumentLanguageSynchronizer(DocumentService& documents, DefaultLanguageRegistry& languages);
    ~DocumentLanguageSynchronizer();

    void start();
    void stop();

private:
    void handleChange(const DocumentChangeEvent& event);

    DocumentService& documents_;
    DefaultLanguageRegistry& languages_;
    std::uint64_t listenerId_ = 0;
};

}
