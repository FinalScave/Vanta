#include "vanta/language/lsp_language_service.h"

#include <utility>

namespace vanta {
namespace {

std::string lspUriForFile(const VirtualFile& file) {
    return file.toUri().string();
}

LanguageRequestTrace traceFromLsp(const LspRequestResult& result) {
    return {
        .id = result.id,
        .method = result.method,
        .rawRequest = result.rawRequest,
        .rawResponse = result.rawResponse,
    };
}

Json parseRawResponse(const LspRequestResult& result) {
    if (result.rawResponse.empty()) {
        return Json(nullptr);
    }
    try {
        return Json::parse(result.rawResponse);
    } catch (const JsonError&) {
        return Json(result.rawResponse);
    }
}

const Json& lspResultValue(const Json& payload) {
    if (payload.isObject() && payload.contains("result")) {
        return payload["result"];
    }
    return payload;
}

TextPosition positionFromLsp(const Json& value) {
    TextPosition position;
    if (value.isObject()) {
        if (value.contains("line") && value["line"].isInt()) {
            position.line = static_cast<int>(value["line"].asInt());
        }
        if (value.contains("character") && value["character"].isInt()) {
            position.character = static_cast<int>(value["character"].asInt());
        }
    }
    return position;
}

TextRange rangeFromLsp(const Json& value) {
    TextRange range;
    if (value.isObject()) {
        range.start = positionFromLsp(value["start"]);
        range.end = positionFromLsp(value["end"]);
    }
    return range;
}

std::string markedStringValue(const Json& value) {
    if (value.isString()) {
        return value.asString();
    }
    if (value.isObject()) {
        if (auto text = value.stringValue("value")) {
            return *text;
        }
        if (auto text = value.stringValue("language")) {
            return *text;
        }
    }
    return value.isNull() ? "" : value.dump();
}

std::string documentationFromLsp(const Json& item) {
    if (!item.isObject() || !item.contains("documentation")) {
        return "";
    }
    return markedStringValue(item["documentation"]);
}

CompletionItem completionItemFromLsp(const Json& item) {
    CompletionItem completion;
    if (!item.isObject()) {
        return completion;
    }
    completion.label = item.stringValue("label").value_or("");
    completion.insertText = item.stringValue("insertText").value_or(completion.label);
    completion.detail = item.stringValue("detail").value_or("");
    completion.documentation = documentationFromLsp(item);
    return completion;
}

void appendLspLocation(const Json& value, std::vector<Location>& locations) {
    if (!value.isObject()) {
        return;
    }
    const std::string uri = value.stringValue("uri").value_or(value.stringValue("targetUri").value_or(""));
    if (uri.empty()) {
        return;
    }
    const Json& rangeJson = value.contains("range") ? value["range"] : value["targetSelectionRange"];
    locations.push_back({
        .file = VirtualFile(Uri::parse(uri), nullptr),
        .range = rangeFromLsp(rangeJson),
    });
}

CompletionList completionFromLsp(const LspRequestResult& result) {
    CompletionList completion;
    completion.trace = traceFromLsp(result);
    completion.raw = parseRawResponse(result);
    completion.ok = !result.timedOut;
    completion.error = result.timedOut ? "Language server request timed out" : "";
    const Json& value = lspResultValue(completion.raw);
    const Json* items = nullptr;
    if (value.isArray()) {
        items = &value;
    } else if (value.isObject()) {
        completion.incomplete = value.contains("isIncomplete") && value["isIncomplete"].isBool() && value["isIncomplete"].asBool();
        if (value.contains("items") && value["items"].isArray()) {
            items = &value["items"];
        }
    }
    if (items != nullptr) {
        for (const Json& item : items->asArray()) {
            CompletionItem completionItem = completionItemFromLsp(item);
            if (!completionItem.label.empty()) {
                completion.items.push_back(std::move(completionItem));
            }
        }
    }
    return completion;
}

HoverResult hoverFromLsp(const LspRequestResult& result) {
    HoverResult hover;
    hover.trace = traceFromLsp(result);
    hover.raw = parseRawResponse(result);
    hover.ok = !result.timedOut;
    hover.error = result.timedOut ? "Language server request timed out" : "";
    const Json& value = lspResultValue(hover.raw);
    if (value.isObject() && value.contains("contents")) {
        hover.contents = markedStringValue(value["contents"]);
    } else {
        hover.contents = markedStringValue(value);
    }
    return hover;
}

LocationResult definitionFromLsp(const LspRequestResult& result) {
    LocationResult definition;
    definition.trace = traceFromLsp(result);
    definition.raw = parseRawResponse(result);
    definition.ok = !result.timedOut;
    definition.error = result.timedOut ? "Language server request timed out" : "";
    const Json& value = lspResultValue(definition.raw);
    if (value.isArray()) {
        for (const Json& item : value.asArray()) {
            appendLspLocation(item, definition.locations);
        }
    } else {
        appendLspLocation(value, definition.locations);
    }
    return definition;
}

SemanticTokens semanticTokensFromLsp(const LspRequestResult& result) {
    SemanticTokens tokens;
    tokens.trace = traceFromLsp(result);
    tokens.raw = parseRawResponse(result);
    tokens.ok = !result.timedOut;
    tokens.error = result.timedOut ? "Language server request timed out" : "";
    const Json& value = lspResultValue(tokens.raw);
    if (value.isObject() && value.contains("data") && value["data"].isArray()) {
        for (const Json& item : value["data"].asArray()) {
            if (item.isInt()) {
                tokens.data.push_back(item.asInt());
            }
        }
    }
    return tokens;
}

}

LspLanguageService::LspLanguageService(std::filesystem::path serverPath, std::filesystem::path workspaceRoot)
    : serverPath_(std::move(serverPath)), workspaceRoot_(std::move(workspaceRoot)) {}

bool LspLanguageService::start(std::string* errorMessage) {
    if (serverPath_.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = "language server path is not configured";
        }
        return false;
    }
    if (!client_.start(serverPath_, workspaceRoot_, errorMessage)) {
        return false;
    }
    client_.initialize(workspaceRoot_);
    return true;
}

bool LspLanguageService::running() const {
    return client_.running();
}

void LspLanguageService::stop() {
    client_.stop();
}

void LspLanguageService::didOpen(const TextDocument& document) {
    Json::Object textDocument;
    textDocument["uri"] = Json(lspUriForFile(document.file));
    textDocument["languageId"] = Json("cpp");
    textDocument["version"] = Json(static_cast<std::int64_t>(document.version));
    textDocument["text"] = Json(document.text);
    client_.notify("textDocument/didOpen", Json::object({{"textDocument", Json::object(std::move(textDocument))}}));
}

void LspLanguageService::didChange(const TextDocument& document) {
    Json::Object textDocument;
    textDocument["uri"] = Json(lspUriForFile(document.file));
    textDocument["version"] = Json(static_cast<std::int64_t>(document.version));
    Json::Array changes;
    changes.push_back(Json::object({{"text", Json(document.text)}}));
    client_.notify("textDocument/didChange", Json::object({
        {"textDocument", Json::object(std::move(textDocument))},
        {"contentChanges", Json::array(std::move(changes))},
    }));
}

void LspLanguageService::didSave(const TextDocument& document) {
    client_.notify("textDocument/didSave", Json::object({
        {"textDocument", Json::object({{"uri", Json(lspUriForFile(document.file))}})},
        {"text", Json(document.text)},
    }));
}

void LspLanguageService::didClose(const VirtualFile& file) {
    client_.notify("textDocument/didClose", Json::object({
        {"textDocument", Json::object({{"uri", Json(lspUriForFile(file))}})},
    }));
}

CompletionList LspLanguageService::completion(const TextDocumentPosition& request) {
    return completionFromLsp(client_.completion(request.document.file.toUri(), request.position));
}

HoverResult LspLanguageService::hover(const TextDocumentPosition& request) {
    return hoverFromLsp(client_.hover(request.document.file.toUri(), request.position));
}

LocationResult LspLanguageService::definition(const TextDocumentPosition& request) {
    return definitionFromLsp(client_.definition(request.document.file.toUri(), request.position));
}

SemanticTokens LspLanguageService::semanticTokensFull(const TextDocumentIdentifier& document) {
    return semanticTokensFromLsp(client_.semanticTokensFull(document.file.toUri()));
}

}
