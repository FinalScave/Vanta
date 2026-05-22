#include "vanta/language/lsp_language_service.h"

#include <utility>

#include "vanta/core/json_codec.h"

namespace vanta {
namespace {

std::string LspUriForFile(const VirtualFile& file) {
    return file.ToUri().ToString();
}

LanguageRequestTrace TraceFromLsp(const LspRequestResult& result) {
    return {
        .id = result.id,
        .method = result.method,
        .raw_request = result.raw_request,
        .raw_response = result.raw_response,
    };
}

Value ParseRawResponse(const LspRequestResult& result) {
    if (result.raw_response.empty()) {
        return Value(nullptr);
    }
    Result<Value> parsed = ValueFromJsonText(result.raw_response);
    return parsed ? parsed.Value() : Value(result.raw_response);
}

const Value& LspResultValue(const Value& payload) {
    if (payload.IsObject() && payload.Contains("result")) {
        return payload["result"];
    }
    return payload;
}

TextPosition PositionFromLsp(const Value& value) {
    TextPosition position;
    if (value.IsObject()) {
        if (value.Contains("line") && value["line"].IsInt()) {
            position.line = static_cast<int>(value["line"].AsInt());
        }
        if (value.Contains("character") && value["character"].IsInt()) {
            position.character = static_cast<int>(value["character"].AsInt());
        }
    }
    return position;
}

TextRange RangeFromLsp(const Value& value) {
    TextRange range;
    if (value.IsObject()) {
        range.start = PositionFromLsp(value["start"]);
        range.end = PositionFromLsp(value["end"]);
    }
    return range;
}

std::string MarkedStringValue(const Value& value) {
    if (value.IsString()) {
        return value.AsString();
    }
    if (value.IsObject()) {
        if (auto text = value.StringValue("value")) {
            return *text;
        }
        if (auto text = value.StringValue("language")) {
            return *text;
        }
    }
    return value.IsNull() ? "" : ValueToJsonText(value);
}

std::string DocumentationFromLsp(const Value& item) {
    if (!item.IsObject() || !item.Contains("documentation")) {
        return "";
    }
    return MarkedStringValue(item["documentation"]);
}

CompletionItem CompletionItemFromLsp(const Value& item) {
    CompletionItem completion;
    if (!item.IsObject()) {
        return completion;
    }
    completion.label = item.StringValue("label").value_or("");
    completion.insert_text = item.StringValue("insertText").value_or(completion.label);
    completion.detail = item.StringValue("detail").value_or("");
    completion.documentation = DocumentationFromLsp(item);
    return completion;
}

void AppendLspLocation(const Value& value, std::vector<Location>& locations) {
    if (!value.IsObject()) {
        return;
    }
    const std::string uri = value.StringValue("uri").value_or(value.StringValue("targetUri").value_or(""));
    if (uri.empty()) {
        return;
    }
    const Value& range_json = value.Contains("range") ? value["range"] : value["targetSelectionRange"];
    locations.push_back({
        .file = VirtualFile(Uri::Parse(uri), nullptr),
        .range = RangeFromLsp(range_json),
    });
}

CompletionList CompletionFromLsp(const LspRequestResult& result) {
    CompletionList completion;
    completion.trace = TraceFromLsp(result);
    const Value raw = ParseRawResponse(result);
    completion.ok = !result.timed_out;
    completion.error = result.timed_out ? "Language server request timed out" : "";
    const Value& value = LspResultValue(raw);
    const Value* items = nullptr;
    if (value.IsArray()) {
        items = &value;
    } else if (value.IsObject()) {
        completion.incomplete = value.Contains("isIncomplete") && value["isIncomplete"].IsBool() && value["isIncomplete"].AsBool();
        if (value.Contains("items") && value["items"].IsArray()) {
            items = &value["items"];
        }
    }
    if (items != nullptr) {
        for (const Value& item : items->AsArray()) {
            CompletionItem completion_item = CompletionItemFromLsp(item);
            if (!completion_item.label.empty()) {
                completion.items.push_back(std::move(completion_item));
            }
        }
    }
    return completion;
}

HoverResult HoverFromLsp(const LspRequestResult& result) {
    HoverResult hover;
    hover.trace = TraceFromLsp(result);
    const Value raw = ParseRawResponse(result);
    hover.ok = !result.timed_out;
    hover.error = result.timed_out ? "Language server request timed out" : "";
    const Value& value = LspResultValue(raw);
    if (value.IsObject() && value.Contains("contents")) {
        hover.contents = MarkedStringValue(value["contents"]);
    } else {
        hover.contents = MarkedStringValue(value);
    }
    return hover;
}

LocationResult DefinitionFromLsp(const LspRequestResult& result) {
    LocationResult definition;
    definition.trace = TraceFromLsp(result);
    const Value raw = ParseRawResponse(result);
    definition.ok = !result.timed_out;
    definition.error = result.timed_out ? "Language server request timed out" : "";
    const Value& value = LspResultValue(raw);
    if (value.IsArray()) {
        for (const Value& item : value.AsArray()) {
            AppendLspLocation(item, definition.locations);
        }
    } else {
        AppendLspLocation(value, definition.locations);
    }
    return definition;
}

SemanticTokens SemanticTokensFromLsp(const LspRequestResult& result) {
    SemanticTokens tokens;
    tokens.trace = TraceFromLsp(result);
    const Value raw = ParseRawResponse(result);
    tokens.ok = !result.timed_out;
    tokens.error = result.timed_out ? "Language server request timed out" : "";
    const Value& value = LspResultValue(raw);
    if (value.IsObject() && value.Contains("data") && value["data"].IsArray()) {
        for (const Value& item : value["data"].AsArray()) {
            if (item.IsInt()) {
                tokens.data.push_back(item.AsInt());
            }
        }
    }
    return tokens;
}

}

LspLanguageService::LspLanguageService(std::filesystem::path server_path, std::filesystem::path workspace_root)
    : server_path_(std::move(server_path)), workspace_root_(std::move(workspace_root)) {}

bool LspLanguageService::Start(std::string* error_message) {
    if (server_path_.empty()) {
        if (error_message != nullptr) {
            *error_message = "language server path is not configured";
        }
        return false;
    }
    if (!client_.Start(server_path_, workspace_root_, error_message)) {
        return false;
    }
    client_.Initialize(workspace_root_);
    return true;
}

bool LspLanguageService::Running() const {
    return client_.Running();
}

void LspLanguageService::Stop() {
    client_.Stop();
}

void LspLanguageService::DidOpen(const TextDocument& document) {
    Value::Object text_document;
    text_document["uri"] = Value(LspUriForFile(document.file));
    text_document["languageId"] = Value("cpp");
    text_document["version"] = Value(static_cast<std::int64_t>(document.version));
    text_document["text"] = Value(document.text);
    client_.Notify("text_document/didOpen", Value::ObjectValue({{"textDocument", Value::ObjectValue(std::move(text_document))}}));
}

void LspLanguageService::DidChange(const TextDocument& document) {
    Value::Object text_document;
    text_document["uri"] = Value(LspUriForFile(document.file));
    text_document["version"] = Value(static_cast<std::int64_t>(document.version));
    Value::Array changes;
    changes.push_back(Value::ObjectValue({{"text", Value(document.text)}}));
    client_.Notify("text_document/didChange", Value::ObjectValue({
        {"textDocument", Value::ObjectValue(std::move(text_document))},
        {"contentChanges", Value::ArrayValue(std::move(changes))},
    }));
}

void LspLanguageService::DidSave(const TextDocument& document) {
    client_.Notify("text_document/didSave", Value::ObjectValue({
        {"textDocument", Value::ObjectValue({{"uri", Value(LspUriForFile(document.file))}})},
        {"text", Value(document.text)},
    }));
}

void LspLanguageService::DidClose(const VirtualFile& file) {
    client_.Notify("text_document/didClose", Value::ObjectValue({
        {"textDocument", Value::ObjectValue({{"uri", Value(LspUriForFile(file))}})},
    }));
}

CompletionList LspLanguageService::Completion(const TextDocumentPosition& request) {
    return CompletionFromLsp(client_.Completion(request.document.file.ToUri(), request.position));
}

HoverResult LspLanguageService::Hover(const TextDocumentPosition& request) {
    return HoverFromLsp(client_.Hover(request.document.file.ToUri(), request.position));
}

LocationResult LspLanguageService::Definition(const TextDocumentPosition& request) {
    return DefinitionFromLsp(client_.Definition(request.document.file.ToUri(), request.position));
}

SemanticTokens LspLanguageService::SemanticTokensFull(const TextDocumentIdentifier& document) {
    return SemanticTokensFromLsp(client_.SemanticTokensFull(document.file.ToUri()));
}

}
