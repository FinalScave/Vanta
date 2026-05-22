#include "vanta/language/lsp_client.h"

#include <chrono>
#include <sstream>
#include <thread>
#include <utility>

#include "vanta/core/json_codec.h"

namespace vanta {

bool LspClient::Start(const std::filesystem::path& server_path, const std::filesystem::path& workspace_root, std::string* error_message) {
    CommandSpec spec;
    spec.executable = server_path.string();
    spec.working_directory = workspace_root;
    return process_.Start(spec, error_message);
}

bool LspClient::Running() const {
    return process_.Running();
}

void LspClient::Stop() {
    process_.Terminate();
}

LspRequestResult LspClient::Initialize(const std::filesystem::path& workspace_root) {
    Value::Object params;
    params["processId"] = Value(nullptr);
    params["rootUri"] = Value(FileUri(workspace_root));
    params["capabilities"] = Value::ObjectValue();
    return SendRequest("initialize", Value::ObjectValue(std::move(params)));
}

LspRequestResult LspClient::Completion(const Uri& file, TextPosition position) {
    return SendRequest("text_document/completion", TextDocumentPositionParams(file, position));
}

LspRequestResult LspClient::Hover(const Uri& file, TextPosition position) {
    return SendRequest("text_document/hover", TextDocumentPositionParams(file, position));
}

LspRequestResult LspClient::Definition(const Uri& file, TextPosition position) {
    return SendRequest("text_document/definition", TextDocumentPositionParams(file, position));
}

LspRequestResult LspClient::SemanticTokensFull(const Uri& file) {
    Value::Object params;
    params["textDocument"] = Value::ObjectValue({{"uri", Value(file.ToString())}});
    return SendRequest("text_document/semantic_tokens/full", Value::ObjectValue(std::move(params)));
}

void LspClient::Notify(std::string method, Value params) {
    std::lock_guard<std::mutex> lock(mutex_);
    Value::Object payload;
    payload["jsonrpc"] = Value("2.0");
    payload["method"] = Value(std::move(method));
    payload["params"] = std::move(params);

    const std::string body = ValueToJsonText(Value::ObjectValue(std::move(payload)));
    std::ostringstream request;
    request << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    process_.WriteStdin(request.str());
}

std::vector<Value> LspClient::DrainNotifications() {
    std::vector<Value> result = std::move(notifications_);
    notifications_.clear();
    return result;
}

LspRequestResult LspClient::SendRequest(std::string method, Value params) {
    std::lock_guard<std::mutex> lock(mutex_);
    LspRequestResult result;
    result.id = next_request_id_++;
    result.method = method;

    Value::Object payload;
    payload["jsonrpc"] = Value("2.0");
    payload["id"] = Value(static_cast<std::int64_t>(result.id));
    payload["method"] = Value(method);
    payload["params"] = std::move(params);

    const std::string body = ValueToJsonText(Value::ObjectValue(std::move(payload)));
    std::ostringstream request;
    request << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    result.raw_request = request.str();

    if (!process_.WriteStdin(result.raw_request)) {
        result.timed_out = true;
        return result;
    }

    if (auto response = WaitForResponseBody(result.id)) {
        result.raw_response = *response;
    } else {
        result.timed_out = true;
    }
    return result;
}

std::optional<std::string> LspClient::ReadMessageBody() {
    read_buffer_ += process_.ReadStdoutAvailable();

    const std::string separator = "\r\n\r\n";
    const std::size_t header_end = read_buffer_.find(separator);
    if (header_end == std::string::npos) {
        return std::nullopt;
    }

    const std::string headers = read_buffer_.substr(0, header_end);
    const std::string length_header = "Content-Length:";
    const std::size_t length_start = headers.find(length_header);
    if (length_start == std::string::npos) {
        read_buffer_.erase(0, header_end + separator.size());
        return std::nullopt;
    }

    std::size_t value_start = length_start + length_header.size();
    while (value_start < headers.size() && headers[value_start] == ' ') {
        ++value_start;
    }
    std::size_t value_end = headers.find("\r\n", value_start);
    if (value_end == std::string::npos) {
        value_end = headers.size();
    }

    const std::size_t ContentLength = static_cast<std::size_t>(std::stoul(headers.substr(value_start, value_end - value_start)));
    const std::size_t body_start = header_end + separator.size();
    if (read_buffer_.size() < body_start + ContentLength) {
        return std::nullopt;
    }

    std::string body = read_buffer_.substr(body_start, ContentLength);
    read_buffer_.erase(0, body_start + ContentLength);
    return body;
}

std::optional<std::string> LspClient::WaitForResponseBody(int request_id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        while (auto body = ReadMessageBody()) {
            Result<Value> message = ValueFromJsonText(*body);
            if (message) {
                const Value& value = message.Value();
                if (value.IsObject() && value.Contains("id") && value["id"].IsInt() && value["id"].AsInt() == request_id) {
                    return body;
                }
                notifications_.push_back(std::move(message.Value()));
            } else {
                return body;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return std::nullopt;
}

Value LspClient::TextDocumentPositionParams(const Uri& file, TextPosition position) {
    Value::Object params;
    params["textDocument"] = Value::ObjectValue({{"uri", Value(file.ToString())}});
    params["position"] = Value::ObjectValue({
        {"line", Value(static_cast<std::int64_t>(position.line))},
        {"character", Value(static_cast<std::int64_t>(position.character))},
    });
    return Value::ObjectValue(std::move(params));
}

std::string LspClient::FileUri(const std::filesystem::path& file) {
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
