#include "vanta/language/lsp_client.h"

#include <chrono>
#include <sstream>
#include <thread>

namespace vanta {

bool LspClient::start(const std::filesystem::path& serverPath, const std::filesystem::path& workspaceRoot, std::string* errorMessage) {
    CommandSpec spec;
    spec.executable = serverPath.string();
    spec.workingDirectory = workspaceRoot;
    return process_.start(spec, errorMessage);
}

bool LspClient::running() const {
    return process_.running();
}

void LspClient::stop() {
    process_.terminate();
}

LspRequestResult LspClient::initialize(const std::filesystem::path& workspaceRoot) {
    Json::Object params;
    params["processId"] = Json(nullptr);
    params["rootUri"] = Json(fileUri(workspaceRoot));
    params["capabilities"] = Json::object();
    return sendRequest("initialize", Json::object(std::move(params)));
}

LspRequestResult LspClient::completion(const Uri& file, TextPosition position) {
    return sendRequest("textDocument/completion", textDocumentPositionParams(file, position));
}

LspRequestResult LspClient::hover(const Uri& file, TextPosition position) {
    return sendRequest("textDocument/hover", textDocumentPositionParams(file, position));
}

LspRequestResult LspClient::definition(const Uri& file, TextPosition position) {
    return sendRequest("textDocument/definition", textDocumentPositionParams(file, position));
}

LspRequestResult LspClient::semanticTokensFull(const Uri& file) {
    Json::Object params;
    params["textDocument"] = Json::object({{"uri", Json(file.string())}});
    return sendRequest("textDocument/semanticTokens/full", Json::object(std::move(params)));
}

void LspClient::completionAsync(AsyncRuntime& runtime, Uri file, TextPosition position, LspResultCallback callback) {
    runtime.postWorker([this, &runtime, file, position, callback = std::move(callback)] {
        LspRequestResult result = completion(file, position);
        runtime.postMain([callback, result = std::move(result)] {
            callback(result);
        });
    });
}

void LspClient::hoverAsync(AsyncRuntime& runtime, Uri file, TextPosition position, LspResultCallback callback) {
    runtime.postWorker([this, &runtime, file, position, callback = std::move(callback)] {
        LspRequestResult result = hover(file, position);
        runtime.postMain([callback, result = std::move(result)] {
            callback(result);
        });
    });
}

void LspClient::notify(std::string method, Json params) {
    std::lock_guard<std::mutex> lock(mutex_);
    Json::Object payload;
    payload["jsonrpc"] = Json("2.0");
    payload["method"] = Json(std::move(method));
    payload["params"] = std::move(params);

    const std::string body = Json::object(std::move(payload)).dump();
    std::ostringstream request;
    request << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    process_.writeStdin(request.str());
}

std::vector<Json> LspClient::drainNotifications() {
    std::vector<Json> result = std::move(notifications_);
    notifications_.clear();
    return result;
}

LspRequestResult LspClient::sendRequest(std::string method, Json params) {
    std::lock_guard<std::mutex> lock(mutex_);
    LspRequestResult result;
    result.id = nextRequestId_++;
    result.method = method;

    Json::Object payload;
    payload["jsonrpc"] = Json("2.0");
    payload["id"] = Json(static_cast<std::int64_t>(result.id));
    payload["method"] = Json(method);
    payload["params"] = std::move(params);

    const std::string body = Json::object(std::move(payload)).dump();
    std::ostringstream request;
    request << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    result.rawRequest = request.str();

    if (!process_.writeStdin(result.rawRequest)) {
        result.timedOut = true;
        return result;
    }

    if (auto response = waitForResponseBody(result.id)) {
        result.rawResponse = *response;
    } else {
        result.timedOut = true;
    }
    return result;
}

std::optional<std::string> LspClient::readMessageBody() {
    readBuffer_ += process_.readStdoutAvailable();

    const std::string separator = "\r\n\r\n";
    const std::size_t headerEnd = readBuffer_.find(separator);
    if (headerEnd == std::string::npos) {
        return std::nullopt;
    }

    const std::string headers = readBuffer_.substr(0, headerEnd);
    const std::string lengthHeader = "Content-Length:";
    const std::size_t lengthStart = headers.find(lengthHeader);
    if (lengthStart == std::string::npos) {
        readBuffer_.erase(0, headerEnd + separator.size());
        return std::nullopt;
    }

    std::size_t valueStart = lengthStart + lengthHeader.size();
    while (valueStart < headers.size() && headers[valueStart] == ' ') {
        ++valueStart;
    }
    std::size_t valueEnd = headers.find("\r\n", valueStart);
    if (valueEnd == std::string::npos) {
        valueEnd = headers.size();
    }

    const std::size_t contentLength = static_cast<std::size_t>(std::stoul(headers.substr(valueStart, valueEnd - valueStart)));
    const std::size_t bodyStart = headerEnd + separator.size();
    if (readBuffer_.size() < bodyStart + contentLength) {
        return std::nullopt;
    }

    std::string body = readBuffer_.substr(bodyStart, contentLength);
    readBuffer_.erase(0, bodyStart + contentLength);
    return body;
}

std::optional<std::string> LspClient::waitForResponseBody(int requestId) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        while (auto body = readMessageBody()) {
            try {
                Json message = Json::parse(*body);
                if (message.isObject() && message.contains("id") && message["id"].isInt() && message["id"].asInt() == requestId) {
                    return body;
                }
                notifications_.push_back(std::move(message));
            } catch (const JsonError&) {
                return body;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

Json LspClient::textDocumentPositionParams(const Uri& file, TextPosition position) {
    Json::Object params;
    params["textDocument"] = Json::object({{"uri", Json(file.string())}});
    params["position"] = Json::object({
        {"line", Json(static_cast<std::int64_t>(position.line))},
        {"character", Json(static_cast<std::int64_t>(position.character))},
    });
    return Json::object(std::move(params));
}

std::string LspClient::fileUri(const std::filesystem::path& file) {
    std::string path = std::filesystem::absolute(file).string();
    std::string uri = "file://";
    for (char ch : path) {
        if (ch == ' ') {
            uri += "%20";
        } else {
            uri.push_back(ch);
        }
    }
    return uri;
}

}
